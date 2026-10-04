#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <LLUtils/Logging/Logger.h>
#include "../Support/LoggingFixtures.h"
#include "../Support/TempFolder.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <future>
#include <latch>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using namespace LLUtils;
    using namespace LLUtils::TestSupport;
    using namespace std::chrono_literals;
    using Catch::Matchers::ContainsSubstring;
}  // namespace

TEST_CASE("Periodic flushing is armed by writes and disarmed after flushing", "[logging][flush]")
{
    Runtime run;
    run.options.flushInterval = 20ms;
    run.Start();
    CHECK_FALSE(run.sink->WaitFlushes(1, 80ms));
    LL_LOG(run.category, LogLevel::Info, "one message then idle");
    REQUIRE(run.sink->WaitFlushes(1));
    CHECK_FALSE(run.sink->WaitFlushes(2, 80ms));
    LL_LOG(run.category, LogLevel::Info, "rearm");
    REQUIRE(run.sink->WaitFlushes(2));
    LL_LOG(run.category, LogLevel::Error, "severity flush");
    REQUIRE(run.sink->WaitFlushes(3));
    CHECK_FALSE(run.sink->WaitFlushes(4, 80ms));
}

TEST_CASE("Continuous arrivals cannot postpone the flush deadline", "[logging][flush]")
{
    Runtime run;
    run.options.flushInterval = 20ms;
    run.Start();
    std::jthread producer(
        [&](std::stop_token stop)
        {
            while (!stop.stop_requested())
            {
                LL_LOG(run.category, LogLevel::Info, "busy");
                std::this_thread::yield();
            }
        });
    const bool flushed = run.sink->WaitFlushes(1);
    producer.request_stop();
    producer.join();
    CHECK(flushed);
}

TEST_CASE("Flush waits for sink processing and reports failed destinations", "[logging][flush]")
{
    Runtime run;
    run.sink->block = true;
    run.Start();
    LL_LOG(run.category, LogLevel::Info, "held");
    REQUIRE(run.sink->WaitWrites(1));
    auto flush = std::async(std::launch::async, [] { return Logger::Flush(); });
    CHECK(flush.wait_for(30ms) == std::future_status::timeout);
    run.sink->Release();
    CHECK(flush.get() == LogResult::Success);
    run.sink->failFlush = true;
    CHECK(Logger::Flush() == LogResult::OutputFailure);
    LL_LOG(run.category, LogLevel::Info, "disabled");
    CHECK(Logger::Flush() == LogResult::OutputFailure);
    CHECK(run.sink->Read().size() == 1);
}

TEST_CASE("Disabled sinks cancel the only pending periodic deadline", "[logging][flush]")
{
    Runtime run;
    run.options.flushInterval = 20ms;
    run.sink->failWrite       = true;
    run.Start();
    LL_LOG(run.category, LogLevel::Info, "fails");
    REQUIRE(run.sink->WaitWrites(1));
    CHECK_FALSE(run.sink->WaitFlushes(1, 80ms));
    CHECK(Logger::Flush() == LogResult::OutputFailure);
}
