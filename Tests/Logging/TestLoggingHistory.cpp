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

TEST_CASE("History captures hidden records and replays the fixed startup format", "[logging][history]")
{
    Runtime run;
    run.options.history.enabled = true;
    run.options.format.pattern  = "history:{message}:{sequence}";
    run.options.sinks[0].format = LogTextFormat{.pattern = "live:{message}"};
    run.Start();
    LL_LOG(run.category, LogLevel::Debug, "hidden");
    REQUIRE(Logger::Flush() == LogResult::Success);
    CHECK(run.sink->Read().empty());
    CHECK(Logger::SetFormat({.pattern = "new:{threadid}:{message}"}, 0) == LogResult::Success);
    LL_LOG(run.category, LogLevel::Error, "failure");
    REQUIRE(Logger::Flush() == LogResult::Success);
    const auto output = run.sink->Read();
    REQUIRE(output.size() == 4);
    CHECK(output[0].marker);
    CHECK(output[1].replay);
    CHECK(output[1].text == "history:hidden:1");
    CHECK(output[1].record.captured == LogFields::None);
    CHECK_THAT(output[3].text, ContainsSubstring("new:"));
    CHECK(output[3].record.sequence == 2);
}

TEST_CASE("History retention releases admission charges and marks eviction and omission", "[logging][history]")
{
    Runtime run;
    run.options.messageBytes = 64;
    run.options.history      = {.enabled = true, .records = 1, .bytes = 400, .historyMinimumLevel = LogLevel::Debug};
    run.Start();
    LL_LOG(run.category, LogLevel::Debug, "{}", std::string(200, 'a'));
    LL_LOG(run.category, LogLevel::Debug, "{}", std::string(300, 'b'));
    LL_LOG(run.category, LogLevel::Debug, "{}", std::string(600, 'c'));
    CHECK(Logger::DumpHistory() == LogResult::Success);
    REQUIRE(Logger::Flush() == LogResult::Success);
    const auto output = run.sink->Read();
    REQUIRE(output.size() == 3);
    CHECK_THAT(output[0].text, ContainsSubstring("evicted 1, omitted 1"));
    CHECK(output[1].record.message == std::string(300, 'b'));
}

TEST_CASE("History-only capture does not arm periodic flushing", "[logging][history][flush]")
{
    Runtime run;
    run.options.history.enabled = true;
    run.options.flushInterval   = 20ms;
    run.Start();
    LL_LOG(run.category, LogLevel::Debug, "hidden");
    CHECK_FALSE(run.sink->WaitFlushes(1, 80ms));
    CHECK(Logger::DumpHistory() == LogResult::Success);
    REQUIRE(run.sink->WaitFlushes(1));
}

TEST_CASE("History with no selected destination is retained for a later dump", "[logging][history]")
{
    Runtime run;
    run.options.history.enabled           = true;
    run.options.sinks[0].sinkMinimumLevel = LogLevel::Off;
    run.Start();
    LL_LOG(run.category, LogLevel::Debug, "retained");
    CHECK(Logger::DumpHistory() == LogResult::Success);
    REQUIRE(Logger::Flush() == LogResult::Success);
    CHECK(run.sink->Read().empty());
    CHECK(Logger::SetSinkMinimumLevel(0, LogLevel::Trace) == LogResult::Success);
    CHECK(Logger::DumpHistory() == LogResult::Success);
    REQUIRE(Logger::Flush() == LogResult::Success);
    REQUIRE(run.sink->Read().size() == 3);
    CHECK(run.sink->Read()[1].record.message == "retained");
    CHECK(Logger::DumpHistory() == LogResult::Success);
    REQUIRE(Logger::Flush() == LogResult::Success);
    CHECK(run.sink->Read().size() == 5);  // A consumed dump has only its begin/end markers.
}

TEST_CASE("History is consumed globally only when a destination completes the dump", "[logging][history]")
{
    Runtime run;
    run.options.history.enabled = true;
    auto failing                = std::make_shared<CaptureSink>();
    failing->onWrite            = [failing]
    {
        if (failing->Read().size() == 2)
            failing->failWrite = true;
    };
    run.options.sinks.push_back({failing});
    run.Start();
    LL_LOG(run.category, LogLevel::Debug, "context");
    CHECK(Logger::DumpHistory() == LogResult::Success);
    CHECK(Logger::Flush() == LogResult::OutputFailure);
    REQUIRE(run.sink->Read().size() == 3);
    CHECK(Logger::DumpHistory() == LogResult::Success);
    CHECK(Logger::Flush() == LogResult::OutputFailure);
    CHECK(run.sink->Read().size() == 5);
    failing->onWrite = {};  // Break the test callback's shared ownership cycle.
}

TEST_CASE("Default live Off leaves history enabled while explicit category Off stops capture", "[logging][history]")
{
    Runtime run;
    run.options.globalMinimumLevel = LogLevel::Off;
    run.options.history.enabled    = true;
    run.Start();
    unsigned arguments = 0;
    LL_LOG(run.category, LogLevel::Debug, "context {}", ++arguments);
    CHECK(Logger::SetCategoryMinimumLevel(run.category, LogLevel::Off) == LogResult::Success);
    LL_LOG(run.category, LogLevel::Critical, "disabled {}", ++arguments);
    CHECK(arguments == 1);
    CHECK(Logger::DumpHistory() == LogResult::Success);
    REQUIRE(Logger::Flush() == LogResult::Success);
    REQUIRE(run.sink->Read().size() == 3);
    CHECK(run.sink->Read()[1].record.message == "context 1");
}

TEST_CASE("Automatic history boundaries capture metadata even when the trigger is not retained", "[logging][history]")
{
    Runtime run;
    run.options.history.enabled             = true;
    run.options.history.historyMinimumLevel = GENERATE(LogLevel::Critical, LogLevel::Off);
    run.options.format.pattern              = "{date}:{threadid}:{operationid}:{message}";
    run.options.sinks[0].format             = LogTextFormat{.pattern = "{message}"};
    run.Start();
    const auto id     = OperationId::Create();
    const auto before = std::chrono::system_clock::now();
    {
        const OperationScope operation(id);
        LL_LOG(run.category, LogLevel::Error, "failure");
    }
    REQUIRE(Logger::Flush() == LogResult::Success);
    const auto output = run.sink->Read();
    REQUIRE(output.size() == 3);
    for (const auto index : {0u, 1u})
    {
        const auto& marker = output[index];
        CHECK(marker.replay);
        CHECK(marker.marker);
        CHECK(marker.record.captured.test(LogFields::Timestamp | LogFields::Thread | LogFields::Operation));
        CHECK(marker.record.timestamp >= before);
        CHECK(marker.record.threadId == PlatformUtility::GetCurrentThreadId());
        CHECK(marker.record.operationId == id);
        CHECK(marker.text.find("1970-") == std::string::npos);
    }
    CHECK(output[2].text == "failure");
    REQUIRE(Logger::DumpHistory() == LogResult::Success);
    REQUIRE(Logger::Flush() == LogResult::Success);
    CHECK(run.sink->Read().size() == 5);
}
