#pragma once
#include "LogConfiguration.h"
#include "LogPattern.h"
#include "../BoundedWorkQueue.h"
#include "../Emergency.h"
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <deque>
#include <format>
#include <future>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace LLUtils::LoggingDetail
{
    // Owned event and captured routing; the queue independently tracks its in-flight admission charge.
    struct DataCommand
    {
        LogRecord record;                             // Metadata and message captured on the producer.
        std::shared_ptr<const Configuration> config;  // Captured filtering/layout snapshot.

        bool history;  // Eligible for independent diagnostic retention after processing.
    };
    // Ordered queue barrier: flushes earlier output and reports completion to its waiting caller.
    struct FlushCommand
    {
        std::shared_ptr<std::promise<LogResult>> completion;  // Writer resolves after releasing queue accounting.
    };
    // Ordered explicit-history request; its delivery snapshot is selected at admission.
    struct DumpCommand
    {
        LogRecord trigger;                            // Origin used by generated history markers.
        std::shared_ptr<const Configuration> config;  // Layouts/destination selection for the dump.
    };
    using Command = std::variant<DataCommand, FlushCommand, DumpCommand>;
    using Emergency = EmergencyDetail::Emergency;

    // Writer-only delivery engine. Configure before starting Run, and clear targets only after joining.
    // The referenced settings must outlive the writer. Producer-facing state remains in Logger's session.
    class LogWriter
    {
      public:

        explicit LogWriter(const LoggerOptions& settings)
            : options(settings), historyPattern(std::make_shared<const Pattern>(settings.format))
        {
        }
        const std::shared_ptr<const Pattern>& HistoryPattern() const noexcept { return historyPattern; }
        void AddSink(std::shared_ptr<LogSink> sink, LogFieldFlags fields, bool text)
        {
            sinks.push_back({std::move(sink)});
            historyFields.set(fields.get());
            if (text)
                historyFields.set(historyPattern->Fields().get());
        }
        LogFieldFlags HistoryFields() const noexcept { return historyFields; }
        bool Failed() const noexcept { return failed; }
        void ClearSinks() noexcept { sinks.clear(); }
        void Run(BoundedWorkQueue<Command>& queue) noexcept
        {
            const Emergency::Scope scope;
            using Status  = BoundedWorkQueue<Command>::WaitStatus;
            bool stopping = false;
            while (!stopping)
            {
                auto work = queue.WaitUntil(flushDue);
                stopping  = work.Status() == Status::Closed;
                if (work)
                {
                    auto& command = work.Value();
                    try
                    {
                        std::visit([&](auto& value) { Process(value); }, command);
                    }
                    catch (...)
                    {
                        failed = true;
                        Emergency::Write("Logging writer processing failed\n");
                    }
                    work.Complete();
                    if (const auto* flush = std::get_if<FlushCommand>(&command))
                        flush->completion->set_value(failed ? LogResult::OutputFailure : LogResult::Success);
                }
                if (flushDue && std::chrono::steady_clock::now() >= *flushDue)
                    FlushSinks(false);
            }
            FlushSinks();
        }

      private:

        // Mutable destination state, accessed only by the session writer.
        struct SinkState
        {
            std::shared_ptr<LogSink> sink;  // Owned callback target.
            bool enabled = true;   // Write/flush failure disables this destination for the rest of the session.
            bool pending = false;  // Successful output since last flush; arms periodic flushing.
        };
        // Reusable writer scratch; sinks sharing a pattern reuse its rendering for the current delivery.
        struct Rendered
        {
            const Pattern* pattern = nullptr;  // Borrowed key valid during the current delivery.
            std::string text;                  // Buffer retains capacity between deliveries; no pattern ownership.
        };
        // Borrowed selection context used only during one synchronous history dump.
        struct ReplayContext
        {
            const LogRecord& trigger;  // Automatic replay selects destinations using this event.
            bool manual;               // Explicit dumps select all enabled destinations whose threshold is not Off.
        };
        // One overload per command alternative: the variant performs the dispatch, so no tag chain is needed.
        void Process(DataCommand& command)
        {
            if (Eligible(command.record.level, LogDefaults::HistoryTriggerSeverity))
                Replay(command.record, *command.config);
            const bool delivered = Deliver(command.record, *command.config);
            if (delivered && Eligible(command.record.level, options.flushSeverity))
                FlushSinks();
            if (command.history)
                Retain(std::move(command.record));
        }
        void Process(FlushCommand&)
        {
            // The caller resolves the barrier promise after releasing queue accounting.
            FlushSinks();
        }
        void Process(DumpCommand& command) { Replay(command.trigger, *command.config, true); }
        void FlushSinks(bool force = true)
        {
            for (auto& sink : sinks)
            {
                if (sink.enabled && (force || sink.pending))
                {
                    try
                    {
                        if (!sink.sink->Flush())
                            Disable(sink);
                    }
                    catch (...)
                    {
                        Disable(sink);
                    }
                    sink.pending = false;
                }
            }
            flushDue.reset();
        }
        void Disable(SinkState& sink) noexcept
        {
            sink.enabled = false;
            sink.pending = false;
            failed       = true;
            Emergency::Write("Logging sink failed; disabled for this session\n");
        }
        bool Deliver(const LogRecord& record, const Configuration& config, const ReplayContext* replay = nullptr,
                     bool marker = false)
        {
            // Destination indexes address the sink vector; a mismatch would silently misroute records.
            assert(config.destinations.size() == sinks.size());
            std::size_t renderedCount = 0;
            bool delivered            = false;
            const auto& trigger       = replay ? replay->trigger : record;
            // Loop invariant: the effective category filter does not depend on the destination being tested.
            const auto categoryMinimumLevel = EffectiveCategoryMinimumLevel(config, trigger.category);
            for (std::size_t i = 0; i < sinks.size(); ++i)
            {
                auto& sink              = sinks[i];
                const auto& destination = config.destinations[i];
                const bool selected     = SelectsDestination(destination, trigger.level, categoryMinimumLevel,
                                                              replay && replay->manual);
                if (sink.enabled && selected)
                {
                    try
                    {
                        std::string_view text;
                        if (destination.text)
                        {
                            const auto* pattern  = replay ? historyPattern.get() : destination.pattern.get();
                            const auto available = std::span(rendered).first(renderedCount);
                            const auto found     = std::ranges::find(available, pattern, &Rendered::pattern);
                            if (found != available.end())
                                text = found->text;
                            else
                            {
                                if (renderedCount == rendered.size())
                                    rendered.emplace_back();
                                auto& buffer = rendered[renderedCount];
                                pattern->RenderInto(record, buffer.text);
                                buffer.pattern = pattern;
                                ++renderedCount;
                                text = buffer.text;
                            }
                        }
                        if (sink.sink->Write({record, text, replay != nullptr, marker}))
                        {
                            delivered    = true;
                            sink.pending = true;
                            if (!flushDue)
                                flushDue = std::chrono::steady_clock::now() + options.flushInterval;
                        }
                        else
                            Disable(sink);
                    }
                    catch (...)
                    {
                        Disable(sink);
                    }
                }
            }
            if (std::ranges::none_of(sinks, [](const auto& sink) { return sink.enabled && sink.pending; }))
                flushDue.reset();
            return delivered;
        }
        void Replay(const LogRecord& trigger, const Configuration& config, bool manual = false)
        {
            if (!options.history.enabled)
                return;
            const ReplayContext replay{trigger, manual};
            LogRecord marker = trigger;
            marker.sequence  = 0;
            marker.message   = std::format("--- history begin (evicted {}, omitted {}) ---", evicted, omitted);
            Deliver(marker, config, &replay, true);
            for (const auto& record : history)
                Deliver(record, config, &replay);
            marker.message = "--- history end ---";
            // A sink accepting the end marker accepted the whole dump: any earlier failure disabled it.
            if (Deliver(marker, config, &replay, true))
            {
                history.clear();
                historyBytes = 0;
                evicted      = 0;
                omitted      = 0;
            }
        }
        void Retain(LogRecord record)
        {
            const auto charge = record.message.capacity();
            if (charge > options.history.bytes)
            {
                ++omitted;
                return;
            }
            while (!history.empty() &&
                   (history.size() >= options.history.records || charge > options.history.bytes - historyBytes))
            {
                historyBytes -= history.front().message.capacity();
                history.pop_front();
                ++evicted;
            }
            history.push_back(std::move(record));
            historyBytes += charge;
        }

        const LoggerOptions& options;    // Immutable session settings, owned by the enclosing session.
        std::vector<SinkState> sinks;    // Fixed destinations with writer-only failure and pending state.
        std::vector<Rendered> rendered;  // Reusable delivery scratch.
        bool failed = false;             // Sticky error reported by barriers and shutdown.
        std::optional<std::chrono::steady_clock::time_point> flushDue;  // Absent while all sinks are clean.
        const std::shared_ptr<const Pattern> historyPattern;            // Fixed startup history layout.
        LogFieldFlags historyFields = LogFields::None;  // Startup capture requirements; read by producers.
        std::deque<LogRecord> history;                           // Retained owned records in admission order.
        std::size_t historyBytes = 0, evicted = 0, omitted = 0;  // Retention accounting since last consumed dump.
    };
}  // namespace LLUtils::LoggingDetail