#pragma once
#include <catch2/catch_test_macros.hpp>
#include <LLUtils/Logging/Logger.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <type_traits>
#include <vector>

namespace LLUtils::TestSupport
{
    using namespace LLUtils;
    using namespace std::chrono_literals;

    static_assert(std::is_same_v<std::underlying_type_t<LogFields>, std::uint32_t>);
    static_assert(
        []
        {
            LogFieldFlags fields{LogFields::None};
            fields.set(LogFields::Timestamp | LogFields::Operation);
            const bool composed = fields.test(LogFields::Timestamp | LogFields::Operation) &&
                                  !fields.testAny(LogFields::Thread | LogFields::Source);
            fields.clear(LogFields::Timestamp);
            return composed && fields.get() == LogFields::Operation;
        }());

    struct Output
    {
        LogRecord record;
        std::string text;
        bool replay, marker;
    };
    // In-memory sink that can block, fail or withhold a flush, and that keeps delivered records for assertions.
    class CaptureSink final : public LogSink
    {
      public:

        bool text            = true;
        LogFieldFlags fields = LogFields::None;
        std::atomic<bool> block{false}, failWrite{false}, failFlush{false};
        std::function<void()> onWrite;
        bool NeedsText() const noexcept override { return text; }
        LogFieldFlags RequiredFields() const noexcept override { return fields; }
        bool Write(const LogDelivery& delivery) override
        {
            std::unique_lock lock(mutex);
            output.push_back({delivery.record, std::string(delivery.text), delivery.replay, delivery.marker});
            changed.notify_all();
            changed.wait(lock, [&] { return !block; });
            lock.unlock();
            if (onWrite)
                onWrite();
            return !failWrite;
        }
        bool Flush() override
        {
            const std::lock_guard lock(mutex);
            ++flushes;
            changed.notify_all();
            return !failFlush;
        }
        bool WaitWrites(std::size_t count)
        {
            std::unique_lock lock(mutex);
            return changed.wait_for(lock, 5s, [&] { return output.size() >= count; });
        }
        bool WaitFlushes(unsigned count, std::chrono::milliseconds timeout = 5000ms)
        {
            std::unique_lock lock(mutex);
            return changed.wait_for(lock, timeout, [&] { return flushes >= count; });
        }
        unsigned Flushes()
        {
            const std::lock_guard lock(mutex);
            return flushes;
        }
        std::vector<Output> Read()
        {
            const std::lock_guard lock(mutex);
            return output;
        }
        void Release()
        {
            const std::lock_guard lock(mutex);
            block = false;
            changed.notify_all();
        }

      private:

        std::mutex mutex;
        std::condition_variable changed;
        std::vector<Output> output;
        unsigned flushes = 0;
    };
    // One initialized session per test, always shut down even when a check fails.
    struct Runtime
    {
        std::shared_ptr<CaptureSink> sink = std::make_shared<CaptureSink>();
        LoggerOptions options;
        LogCategory category = Logger::RegisterCategory("Test.Logging");
        Runtime()
        {
            options.format.pattern = "{message}";
            options.flushInterval  = 1h;
            options.sinks.push_back({sink});
        }
        void Start() { REQUIRE(Logger::Initialize(options) == LogResult::Success); }
        ~Runtime()
        {
            sink->Release();
            if (Logger::IsActive())
                Logger::Shutdown();
        }
    };
    // Inline so every translation unit shares one limiter instance per call site, matching the macro contract.
    inline void CountLimited(LogCategory category, unsigned& arguments)
    {
        LL_LOG_EVERY_N(category, LogLevel::Info, 3, "count {}", ++arguments);
    }
    inline void TimeLimited(LogCategory category, unsigned& arguments)
    {
        LL_LOG_EVERY_N_SEC(category, LogLevel::Info, 3600, "time {}", ++arguments);
    }
}  // namespace LLUtils::TestSupport