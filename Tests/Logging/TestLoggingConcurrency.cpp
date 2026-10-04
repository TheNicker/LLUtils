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

TEST_CASE("Logging retains selected configuration while formatting is paused", "[logging][concurrency]")
{
    Runtime run;
    run.options.format.pattern = "old:{message}";
    run.Start();
    std::latch formatted(1), release(1);
    auto producer = std::async(std::launch::async,
                               [&]
                               {
                                   return Logger::Log(run.category, LogLevel::Info,
                                                      [&]
                                                      {
                                                          formatted.count_down();
                                                          release.wait();
                                                          return std::string("first");
                                                      });
                               });
    formatted.wait();
    CHECK(Logger::Flush() == LogResult::Success);
    CHECK(run.sink->Read().empty());
    CHECK(Logger::SetFormat({.pattern = "new:{message}"}) == LogResult::Success);
    CHECK(Logger::SetGlobalMinimumLevel(LogLevel::Off) == LogResult::Success);
    release.count_down();
    CHECK(producer.get() == LogResult::Success);
    CHECK(Logger::Flush() == LogResult::Success);
    REQUIRE(run.sink->Read().size() == 1);
    CHECK(run.sink->Read()[0].text == "old:first");
}

TEST_CASE("Message budget remains charged while the writer processes a record", "[logging][concurrency]")
{
    Runtime run;
    run.options.messageBytes = 150;
    run.sink->block          = true;
    run.Start();
    LL_LOG(run.category, LogLevel::Info, "{}", std::string(100, 'a'));
    REQUIRE(run.sink->WaitWrites(1));
    std::latch formatted(1);
    auto producer = std::async(std::launch::async,
                               [&]
                               {
                                   return Logger::Log(run.category, LogLevel::Info,
                                                      [&]
                                                      {
                                                          formatted.count_down();
                                                          return std::string(100, 'b');
                                                      });
                               });
    formatted.wait();
    CHECK(producer.wait_for(30ms) == std::future_status::timeout);
    run.sink->Release();
    CHECK(producer.get() == LogResult::Success);
    REQUIRE(Logger::Flush() == LogResult::Success);
    CHECK(run.sink->Read().size() == 2);
}

TEST_CASE("Oversized admission preserves full records and permits normal records", "[logging][concurrency]")
{
    Runtime run;
    run.options.messageBytes = 64;
    run.sink->block          = true;
    run.Start();
    LL_LOG(run.category, LogLevel::Info, "{}", std::string(200, 'a'));
    REQUIRE(run.sink->WaitWrites(1));
    std::latch formatted(1);
    auto oversized = std::async(std::launch::async,
                                [&]
                                {
                                    return Logger::Log(run.category, LogLevel::Info,
                                                       [&]
                                                       {
                                                           formatted.count_down();
                                                           return std::string(300, 'b');
                                                       });
                                });
    formatted.wait();
    CHECK(oversized.wait_for(30ms) == std::future_status::timeout);
    CHECK(Logger::Log(run.category, LogLevel::Info, "normal") == LogResult::Success);
    run.sink->Release();
    CHECK(oversized.get() == LogResult::Success);
    REQUIRE(Logger::Flush() == LogResult::Success);
    const auto output = run.sink->Read();
    REQUIRE(output.size() == 3);
    CHECK(output[0].record.message.size() == 200);
    CHECK(output[1].record.message == "normal");
    CHECK(output[2].record.message.size() == 300);
}

TEST_CASE("Full queue control and producer admissions wake safely during shutdown", "[logging][concurrency]")
{
    Runtime run;
    run.options.queueRecords = 1;
    run.sink->block          = true;
    run.Start();
    LL_LOG(run.category, LogLevel::Info, "processing");
    REQUIRE(run.sink->WaitWrites(1));
    LL_LOG(run.category, LogLevel::Info, "queued");
    auto producer = std::async(std::launch::async,
                               [&] { return Logger::Log(run.category, LogLevel::Info, "waiting"); });
    auto flush    = std::async(std::launch::async, [] { return Logger::Flush(); });
    CHECK(producer.wait_for(30ms) == std::future_status::timeout);
    CHECK(flush.wait_for(30ms) == std::future_status::timeout);
    auto shutdown = std::async(std::launch::async, [] { return Logger::Shutdown(); });
    CHECK(producer.get() == LogResult::Inactive);
    CHECK(flush.get() == LogResult::Inactive);
    run.sink->Release();
    CHECK(shutdown.get() == LogResult::Success);
    CHECK(run.sink->Read().size() == 2);
}

TEST_CASE("Concurrent producers preserve admission sequence and own borrowed messages", "[logging][concurrency]")
{
    Runtime run;
    run.Start();
    std::vector<std::jthread> producers;
    for (unsigned thread = 0; thread < 6; ++thread)
        producers.emplace_back(
            [&]
            {
                const auto category = Logger::RegisterCategory("Test.Logging");
                for (unsigned i = 0; i < 50; ++i)
                {
                    std::string message = "owned";
                    Logger::Log(category, LogLevel::Info, message);
                    message.assign("changed");
                }
            });
    producers.clear();
    REQUIRE(Logger::Flush() == LogResult::Success);
    const auto output = run.sink->Read();
    REQUIRE(output.size() == 300);
    for (std::size_t i = 0; i < output.size(); ++i)
    {
        CHECK(output[i].record.sequence == i + 1);
        CHECK(output[i].record.message == "owned");
    }
}
