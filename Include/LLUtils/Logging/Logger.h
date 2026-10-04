/*
Copyright (c) 2019 Lior Lahav

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once
#include "LogEmergencyFile.h"
#include "LogWriter.h"
#include "../Emergency.h"
#include "../ExceptionFormatter.h"
#include "../Thread.h"
#include "../RepetitionLimiter.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <concepts>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <source_location>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>
#include <variant>

namespace LLUtils
{
    using Emergency = EmergencyDetail::Emergency;
    // One runtime per program, explicit session lifetime, permanent category identities.
    // Producers own formatting. The writer alone owns delivery, history and flushing.
    class Logger
    {
        using Configuration = LoggingDetail::Configuration;
        using Command       = LoggingDetail::Command;
        using DataCommand   = LoggingDetail::DataCommand;
        using FlushCommand  = LoggingDetail::FlushCommand;
        using DumpCommand   = LoggingDetail::DumpCommand;
        // Original producer/exception context, resolved before invoking the message factory.
        struct Origin
        {
            std::source_location source;           // Caller or original exception location.
            std::uint64_t threadId = 0;            // Zero requests the current producer thread when needed.
            std::optional<OperationId> operation;  // Empty uses current scope; present zero preserves absent context.
        };

      public:

        static LogCategory RegisterCategory(std::string_view name)
        {
            const std::lock_guard lock(sRegistryMutex);
            auto found = sCategories.find(name);
            if (found != sCategories.end())
                return found->second;
            LogCategory category(std::make_shared<const std::string>(name), ++sCategoryIdentity);
            sCategories.emplace(category.Name(), category);
            return category;
        }
        static LogCategory FindCategory(std::string_view name)
        {
            const std::lock_guard lock(sRegistryMutex);
            const auto found = sCategories.find(name);
            return found == sCategories.end() ? LogCategory{} : found->second;
        }
        static bool IsActive() noexcept { return sActive.load(std::memory_order_acquire); }
        static bool CanEnter() noexcept
        {
            if (Reentrant())
            {
                Emergency(EmergencyDetail::Emergency::RecursiveMessage);
                return false;
            }
            return IsActive();
        }
        static void Emergency(std::string_view message) noexcept { EmergencyDetail::Emergency::Write(message); }
        static LogResult Initialize(LoggerOptions options)
        {
            if (Reentrant())
                return LogResult::Reentrant;
            const DepthScope scope;
            const std::lock_guard lifecycle(sLifecycleMutex);
            if (IsActive() || sSession.load())
                return LogResult::InvalidConfiguration;
            if (!options.queueRecords || !options.messageBytes ||
                options.flushInterval <= std::chrono::steady_clock::duration{} ||
                !IsValidLevel(options.globalMinimumLevel) || !IsValidLevel(options.flushSeverity) ||
                !IsValidLevel(options.history.historyMinimumLevel) ||
                (options.history.enabled && (!options.history.records || !options.history.bytes)))
                return LogResult::InvalidConfiguration;
            std::shared_ptr<Session> session;
            try
            {
                session                    = std::make_shared<Session>(std::move(options));
                session->id                = ++sSessionIdentity;
                auto config                = std::make_shared<Configuration>();
                config->globalMinimumLevel = session->options.globalMinimumLevel;
                config->defaultPattern     = session->delivery.HistoryPattern();
                for (auto& settings : session->options.sinks)
                {
                    if (!settings.sink || !IsValidLevel(settings.sinkMinimumLevel))
                        return LogResult::InvalidConfiguration;
                    const bool text   = settings.sink->NeedsText();
                    const auto fields = settings.sink->RequiredFields();
                    if (std::to_underlying(fields.get()) > std::to_underlying(LogFields::All))
                        return LogResult::InvalidConfiguration;
                    config->destinations.push_back(
                        {settings.sinkMinimumLevel,
                         settings.format ? PatternFor(*config, *settings.format) : config->defaultPattern, fields, text,
                         settings.format.has_value()});
                    session->delivery.AddSink(std::move(settings.sink), fields, text);
                }
                session->config.store(config);
                LoggingDetail::EmergencyFile::Open(session->options.emergencyPath);
                const auto category            = RegisterCategory("LLUtils.Exception");
                session->exceptionSubscription = Exception::OnException.Subscribe(
                    [session, category](const Exception::EventArgs& args)
                    {
                        if (Reentrant())
                        {
                            Emergency(EmergencyDetail::Emergency::ExceptionMessage);
                            return;
                        }
                        try
                        {
                            LogToSession<nullptr>(
                                session, category, LogLevel::Error, [&] { return FormatException(args); },
                                // An empty .operation is the normal case for exceptions: they are observed
                                // synchronously inside their own constructor, so the ambient OperationScope is still
                                // authoritative at this instant and Capture reads it. A present value
                                // forces a specific correlation, including present zero for "none".
                                Origin{.source = args.source, .threadId = args.threadId});
                        }
                        catch (...)
                        {
                            Emergency("Exception logging failed\n");
                        }
                    });
                session->writer = StartThread([session] { session->delivery.Run(session->queue); });
                sSession.store(session);
                sActive.store(true, std::memory_order_release);
                return LogResult::Success;
            }
            catch (...)
            {
                if (!session)
                {
                    Emergency("Logging initialization failed\n");
                    return LogResult::InvalidConfiguration;
                }
                session->queue.Close();
                session->exceptionSubscription.Unsubscribe();
                Emergency("Logging initialization failed\n");
                return LogResult::InvalidConfiguration;
            }
        }
        static LogResult Shutdown()
        {
            if (Reentrant())
                return LogResult::Reentrant;
            const DepthScope scope;
            const std::lock_guard lifecycle(sLifecycleMutex);
            const auto session = sSession.load();
            if (!session)
                return LogResult::Inactive;
            sActive.store(false, std::memory_order_release);
            session->queue.Close();
            session->exceptionSubscription.Unsubscribe();
            session->queue.WaitForProducers();
            session->writer.join();
            sSession.store({});
            session->delivery.ClearSinks();
            return session->delivery.Failed() ? LogResult::OutputFailure : LogResult::Success;
        }
        static LogResult Flush() { return Control(false); }
        static LogResult DumpHistory(std::source_location source = std::source_location::current())
        {
            return Control(true, source);
        }
        static LogResult SetGlobalMinimumLevel(LogLevel globalMinimumLevel)
        {
            if (!IsValidLevel(globalMinimumLevel))
                return LogResult::InvalidConfiguration;
            return Update([&](Configuration& config) { config.globalMinimumLevel = globalMinimumLevel; });
        }
        static LogResult SetCategoryMinimumLevel(LogCategory category, std::optional<LogLevel> categoryMinimumLevel)
        {
            if (!category || (categoryMinimumLevel && !IsValidLevel(*categoryMinimumLevel)))
                return LogResult::InvalidConfiguration;
            return Update(
                [&](Configuration& config)
                {
                    if (categoryMinimumLevel)
                        config.categories[category.Identity()] = *categoryMinimumLevel;
                    else
                        config.categories.erase(category.Identity());
                });
        }
        static LogResult SetSinkMinimumLevel(std::size_t sink, LogLevel sinkMinimumLevel)
        {
            if (!IsValidLevel(sinkMinimumLevel))
                return LogResult::InvalidConfiguration;
            return Update([&](Configuration& config)
                          { config.destinations.at(sink).sinkMinimumLevel = sinkMinimumLevel; });
        }
        static LogResult SetFormat(LogTextFormat format, std::optional<std::size_t> sink = {})
        {
            return Update(
                [&](Configuration& config)
                {
                    auto pattern = PatternFor(config, std::move(format));
                    if (sink)
                    {
                        auto& destination      = config.destinations.at(*sink);
                        destination.pattern    = std::move(pattern);
                        destination.overridden = true;
                    }
                    else
                    {
                        config.defaultPattern = pattern;
                        for (auto& destination : config.destinations)
                            if (!destination.overridden)
                                destination.pattern = pattern;
                    }
                });
        }
        // Run one complete producer call. The factory is invoked at most once, only after eligibility and
        // repetition control; its owned result is queued before this call returns. Success means admission,
        // not sink completion. Macros supply a factory so formatting arguments stay unevaluated when skipped.
        template <auto Policy = nullptr, class Factory>
            requires std::invocable<Factory&> && std::same_as<std::invoke_result_t<Factory&>, std::string>
        static LogResult Log(LogCategory category, LogLevel level, Factory&& message,
                             std::source_location source = std::source_location::current()) noexcept
        {
            if (Reentrant())
            {
                Emergency(EmergencyDetail::Emergency::RecursiveMessage);
                return LogResult::Reentrant;
            }
            const auto session = ActiveSession();
            return session
                       ? LogToSession<Policy>(session, std::move(category), level, message, Origin{.source = source})
                       : LogResult::Inactive;
        }
        // Existing UTF-8 text is copied only if the call is eligible. C++ evaluates this overload's arguments
        // before entering the function; use LL_LOG or a factory when constructing the message is expensive.
        static LogResult Log(LogCategory category, LogLevel level, std::string_view message,
                             std::source_location source = std::source_location::current()) noexcept
        {
            if (Reentrant())
            {
                Emergency(EmergencyDetail::Emergency::RecursiveMessage);
                return LogResult::Reentrant;
            }
            return Log(std::move(category), level, [&] { return std::string(message); }, source);
        }

      private:

        struct Session;  // Owns the queue, configuration snapshot and writer; defined below.
        // Reentry is owned by the emergency reporter, which stays free of any logger state; ask it instead of
        // reading its depth counter, so every entry point shares one definition of "this thread is already
        // running a diagnostic pipeline".
        static bool Reentrant() noexcept { return EmergencyDetail::Emergency::Active(); }
        // The live session, or null when logging is inactive. IsActive gates the documented lifetime contract
        // that no record is delivered before Initialize returns; the null test covers the session being retired.
        static std::shared_ptr<Session> ActiveSession() noexcept { return IsActive() ? sSession.load() : nullptr; }
        // Applies this call site's repetition policy. Returns std::nullopt when the call is skipped and
        // must not be emitted, or the number of eligible calls skipped since the previous admission when
        // it is admitted, so the next record can report the backlog. A site with no policy admits every
        // call and reports no backlog.
        template <auto Policy>
        static std::optional<std::uint64_t> Admit(std::uint64_t session)
        {
            if constexpr (std::same_as<decltype(Policy), std::nullptr_t>)
                return 0;
            else
            {
                constexpr auto policy = Policy();
                static_assert(policy.count > 0 && policy.period >= std::chrono::steady_clock::duration{});
                static RepetitionLimiter limiter(policy.count, policy.period);  // One instance per macro site.
                const auto now = policy.period == std::chrono::steady_clock::duration{}
                                     ? std::chrono::steady_clock::time_point{}
                                     : std::chrono::steady_clock::now();
                return limiter.Admit(session, now);
            }
        }

      private:

        // Marks producer/writer work as diagnostic-originated for reentry and exception-dispatch protection.
        using DepthScope = EmergencyDetail::Emergency::Scope;
        // Owns producer/configuration lifetime and the queue; delivery state belongs to LogWriter.
        struct Session
        {
            explicit Session(LoggerOptions settings)
                : options(std::move(settings)), queue(options.queueRecords, options.messageBytes), delivery(options)
            {
            }
            LoggerOptions options;  // Immutable after initialization; outlives delivery.
            std::uint64_t id       = 0,
                          sequence = 0;  // Session identity; sequence changes only in the queue admission callback.
            std::mutex configMutex;      // Serializes configuration copy/update/publication.
            std::atomic<std::shared_ptr<const Configuration>> config;  // Immutable snapshot for producers.
            BoundedWorkQueue<Command> queue;    // Admission, producer registration and shutdown coordination.
            LoggingDetail::LogWriter delivery;  // Sole owner of sinks, history and flush scheduling.
            std::thread writer;                 // Runs delivery; joined before clearing targets.
            Exception::OnExceptionEventType::Subscription exceptionSubscription;  // Bridge lifetime.
        };
        template <class Change>
        static LogResult Update(Change&& change)
        {
            if (Reentrant())
                return LogResult::Reentrant;
            const auto session = ActiveSession();
            if (!session)
                return LogResult::Inactive;
            try
            {
                const std::lock_guard lock(session->configMutex);
                auto config = std::make_shared<Configuration>(*session->config.load());
                change(*config);
                session->config.store(std::move(config));
                return LogResult::Success;
            }
            catch (...)
            {
                return LogResult::InvalidConfiguration;
            }
        }
        static LogResult Control(bool dump, std::source_location source = {})
        {
            if (Reentrant())
                return LogResult::Reentrant;
            const auto session = ActiveSession();
            if (!session)
                return LogResult::Inactive;
            try
            {
                if (dump)
                {
                    LogRecord trigger;
                    trigger.category = RegisterCategory("LLUtils.History");
                    Capture(trigger, session->delivery.HistoryFields(), Origin{.source = source});
                    return Enqueue(*session, DumpCommand{std::move(trigger), {}});
                }
                auto completion   = std::make_shared<std::promise<LogResult>>();
                auto future       = completion->get_future();
                const auto result = Enqueue(*session, FlushCommand{completion});
                return result == LogResult::Success ? future.get() : result;
            }
            catch (...)
            {
                Emergency("Logging control failed\n");
                return LogResult::OutputFailure;
            }
        }
        template <class T>
        static LogResult Enqueue(Session& session, T command, std::size_t weight = 0)
        {
            const bool admitted = session.queue.Push(std::move(command), weight,
                                                     [&](Command& value) noexcept
                                                     {
                                                         if (auto* data = std::get_if<DataCommand>(&value))
                                                             data->record.sequence = ++session.sequence;
                                                         if (auto* dump = std::get_if<DumpCommand>(&value))
                                                             dump->config = session.config.load();
                                                     });
            return admitted ? LogResult::Success : LogResult::Inactive;
        }
        static std::shared_ptr<const LoggingDetail::Pattern> PatternFor(const Configuration& config,
                                                                        LogTextFormat format)
        {
            if (config.defaultPattern->Format() == format)
                return config.defaultPattern;
            const auto found = std::ranges::find_if(config.destinations, [&](const auto& destination)
                                                    { return destination.pattern->Format() == format; });
            return found != config.destinations.end()
                       ? found->pattern
                       : std::make_shared<const LoggingDetail::Pattern>(std::move(format));
        }
        static void Capture(LogRecord& record, LogFieldFlags fields, Origin origin)
        {
            record.captured = fields;
            if (fields.test(LogFields::Timestamp))
                record.timestamp = std::chrono::system_clock::now();
            if (fields.test(LogFields::Thread))
                record.threadId = origin.threadId ? origin.threadId : PlatformUtility::GetCurrentThreadId();
            if (fields.test(LogFields::Source))
                record.source = origin.source;
            if (fields.test(LogFields::Operation))
                record.operationId = origin.operation.value_or(OperationScope::Current());
        }
        template <auto Policy, class Factory>
        static LogResult LogToSession(const std::shared_ptr<Session>& session, LogCategory category, LogLevel level,
                                      Factory&& message, Origin origin) noexcept
        {
            if (Reentrant())
            {
                Emergency(EmergencyDetail::Emergency::RecursiveMessage);
                return LogResult::Reentrant;
            }
            const DepthScope scope;
            try
            {
                if (!category || level == LogLevel::Off || !IsValidLevel(level))
                    return LogResult::Inactive;
                // Retain filtering, layouts and capture requirements together, even if settings change while
                // the factory runs or admission waits. History contributes its fixed startup requirements.
                const auto config               = session->config.load();
                const auto overrideEntry        = config->categories.find(category.Identity());
                const auto categoryMinimumLevel = overrideEntry == config->categories.end() ? config->globalMinimumLevel
                                                                                            : overrideEntry->second;
                // An explicit category Off also disables history capture; a global Off does not.
                if (overrideEntry != config->categories.end() && categoryMinimumLevel == LogLevel::Off)
                    return LogResult::Inactive;
                const bool history = session->options.history.enabled &&
                                     LoggingDetail::Eligible(level, session->options.history.historyMinimumLevel);
                bool live          = false;
                // Replay boundaries use the fixed history layout even when retention excludes the trigger.
                const bool historyMetadata = history ||
                                             (session->options.history.enabled &&
                                              LoggingDetail::Eligible(level, LogDefaults::HistoryTriggerSeverity));
                LogFieldFlags fields       = historyMetadata ? session->delivery.HistoryFields()
                                                             : LogFieldFlags{LogFields::None};
                for (const auto& destination : config->destinations)
                    if (LoggingDetail::SelectsDestination(destination, level, categoryMinimumLevel))
                    {
                        live = true;
                        fields |= destination.fields;
                        if (destination.text)
                            fields |= destination.pattern->Fields();
                    }
                if (!live && !history)
                    return LogResult::Inactive;
                // The local registration covers every remaining exit, including limiter skipping and
                // factory failures. No preparation can escape to a caller and hold shutdown indefinitely.
                auto emission = session->queue.TryRegisterProducer();
                if (!emission)
                    return LogResult::Inactive;
                const auto skippedCalls = Admit<Policy>(session->id);
                if (!skippedCalls.has_value())
                    return LogResult::Inactive;
                LogRecord record;
                record.category = std::move(category);
                record.level    = level;
                Capture(record, fields, origin);
                record.message = std::invoke(message);
                record.skipped = *skippedCalls;
                if (*skippedCalls)
                    record.message += std::format(" (skipped {} calls at this site)", *skippedCalls);
                const auto charge = record.message.capacity();
                return Enqueue(*session, DataCommand{std::move(record), config, history}, charge);
            }
            catch (...)
            {
                Emergency("Logging call failed during capture, formatting or admission\n");
                return LogResult::OutputFailure;
            }
        }
        static inline constinit std::atomic<bool> sActive{false};
        static inline std::atomic<std::shared_ptr<Session>> sSession;
        static inline std::mutex sLifecycleMutex, sRegistryMutex;
        static inline std::unordered_map<std::string_view, LogCategory> sCategories;
        static inline std::uint64_t sCategoryIdentity = 0, sSessionIdentity = 0;
    };

}  // namespace LLUtils

// The macro guards category/level expressions and captures message arguments in a lazy factory.
#define LL_LOG_TRY(CALL)                                                                                               \
    do                                                                                                                 \
    {                                                                                                                  \
        try                                                                                                            \
        {                                                                                                              \
            if (::LLUtils::Logger::CanEnter())                                                                         \
                CALL;                                                                                                  \
        }                                                                                                              \
        catch (...)                                                                                                    \
        {                                                                                                              \
            ::LLUtils::Logger::Emergency(::LLUtils::Emergency::PreparationMessage);                                    \
        }                                                                                                              \
    } while (false)
#define LL_LOG(CATEGORY, LEVEL, FORMAT, ...)                                                                           \
    LL_LOG_TRY(                                                                                                        \
        (::LLUtils::Logger::Log((CATEGORY), (LEVEL), [&] { return std::format((FORMAT) __VA_OPT__(, ) __VA_ARGS__); }, \
                                std::source_location::current())))
#define LL_LOG_DETAIL_LIMITED(CATEGORY, LEVEL, N, SECONDS, FORMAT, ...)                                                \
    LL_LOG_TRY((::LLUtils::Logger::Log<[]                                                                              \
                                       {                                                                               \
                                           return ::LLUtils::LoggingDetail::Repetition{                                \
                                               (N), std::chrono::duration_cast<std::chrono::steady_clock::duration>(   \
                                                        std::chrono::duration<double>(SECONDS))};                      \
                                       }>((CATEGORY), (LEVEL),                                                         \
                                          [&] { return std::format((FORMAT) __VA_OPT__(, ) __VA_ARGS__); },            \
                                          std::source_location::current())))
#define LL_LOG_EVERY_N(CATEGORY, LEVEL, N, FORMAT, ...)                                                                \
    do                                                                                                                 \
    {                                                                                                                  \
        static_assert((N) > 0);                                                                                        \
        LL_LOG_DETAIL_LIMITED(CATEGORY, LEVEL, N, 0, FORMAT __VA_OPT__(, ) __VA_ARGS__);                               \
    } while (false)
#define LL_LOG_EVERY_N_SEC(CATEGORY, LEVEL, SECONDS, FORMAT, ...)                                                      \
    do                                                                                                                 \
    {                                                                                                                  \
        static_assert((SECONDS) > 0);                                                                                  \
        LL_LOG_DETAIL_LIMITED(CATEGORY, LEVEL, 1, SECONDS, FORMAT __VA_OPT__(, ) __VA_ARGS__);                         \
    } while (false)
