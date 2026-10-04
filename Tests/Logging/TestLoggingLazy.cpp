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

TEST_CASE("Lazy logging calls its factory only for eligible consumers", "[logging][lazy]")
{
    Runtime run;
    run.Start();
    unsigned calls = 0;
    auto message   = [&]
    {
        ++calls;
        return std::string("lazy");
    };
    CHECK(Logger::Log(run.category, LogLevel::Debug, message) == LogResult::Inactive);
    CHECK(calls == 0);
    CHECK(Logger::Log(run.category, LogLevel::Info, message) == LogResult::Success);
    CHECK(calls == 1);
    REQUIRE(Logger::Flush() == LogResult::Success);
    CHECK(run.sink->Read()[0].record.message == "lazy");
}

TEST_CASE("Shutdown waits for an active factory and rejects its later admission", "[logging][lazy][concurrency]")
{
    Runtime run;
    run.Start();
    std::latch entered(1), resume(1);
    auto producer = std::async(std::launch::async,
                               [&]
                               {
                                   return Logger::Log(run.category, LogLevel::Info,
                                                      [&]
                                                      {
                                                          entered.count_down();
                                                          resume.wait();
                                                          return std::string("late");
                                                      });
                               });
    entered.wait();
    auto shutdown       = std::async(std::launch::async, [] { return Logger::Shutdown(); });
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (Logger::IsActive() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    CHECK_FALSE(Logger::IsActive());
    CHECK(shutdown.wait_for(30ms) == std::future_status::timeout);
    resume.count_down();
    CHECK(producer.get() == LogResult::Inactive);
    CHECK(shutdown.get() == LogResult::Success);
    CHECK(run.sink->Read().empty());
}
