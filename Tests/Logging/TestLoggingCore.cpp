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

LLUtils::LogCategory OtherLoggingCategory();
void LogFromOtherTranslationUnit();

struct RecursiveFormat
{
    LLUtils::LogCategory category;
    unsigned* arguments;
};
template <>
struct std::formatter<RecursiveFormat> : std::formatter<std::string_view>
{
    auto format(const RecursiveFormat& value, std::format_context& context) const
    {
        LL_LOG(value.category, LLUtils::LogLevel::Info, "nested {}", ++*value.arguments);
        return std::formatter<std::string_view>::format("formatted", context);
    }
};

namespace
{
    using namespace LLUtils;
    using namespace LLUtils::TestSupport;
    using namespace std::chrono_literals;
    using Catch::Matchers::ContainsSubstring;
}  // namespace

TEST_CASE("Logging categories and runtime are shared across translation units", "[logging]")
{
    Runtime run;
    run.Start();
    CHECK(OtherLoggingCategory().Identity() == run.category.Identity());
    CHECK(Logger::FindCategory("Test.Logging").Identity() == run.category.Identity());
    CHECK(Logger::RegisterCategory("test.logging").Identity() != run.category.Identity());
    LogFromOtherTranslationUnit();
    REQUIRE(Logger::Flush() == LogResult::Success);
    REQUIRE(run.sink->Read().size() == 1);
    CHECK(run.sink->Read()[0].record.sequence == 1);
    CHECK(run.sink->Read()[0].text == "other translation unit");
    CHECK(Logger::Shutdown() == LogResult::Success);
    run.Start();
    LL_LOG(run.category, LogLevel::Info, "restart");
    REQUIRE(Logger::Flush() == LogResult::Success);
    CHECK(run.sink->Read().back().record.sequence == 1);
}

TEST_CASE("Filtering and repetition skip message arguments and reset per session", "[logging]")
{
    Runtime run;
    run.Start();
    unsigned arguments = 0;
    LL_LOG(run.category, LogLevel::Debug, "{}", ++arguments);
    CHECK(arguments == 0);
    for (unsigned i = 0; i < 8; ++i)
        CountLimited(run.category, arguments);
    TimeLimited(run.category, arguments);
    TimeLimited(run.category, arguments);
    CHECK(arguments == 4);
    REQUIRE(Logger::Flush() == LogResult::Success);
    auto output = run.sink->Read();
    REQUIRE(output.size() == 4);
    CHECK(output[1].record.skipped == 2);
    CHECK_THAT(output[1].text, ContainsSubstring("skipped 2 calls"));
    CHECK(Logger::SetCategoryMinimumLevel(run.category, LogLevel::Off) == LogResult::Success);
    LL_LOG(run.category, LogLevel::Critical, "{}", ++arguments);
    CHECK(arguments == 4);
    CHECK(Logger::Shutdown() == LogResult::Success);
    run.Start();
    CountLimited(run.category, arguments);
    TimeLimited(run.category, arguments);
    CHECK(arguments == 6);
}

TEST_CASE("Logging validates formats before publication and inserts messages as data", "[logging]")
{
    Runtime run;
    run.options.format.pattern = "{{{loglevel:*>8}}}{message}";
    run.Start();
    CHECK(Logger::SetFormat({.pattern = "{unknown}"}) == LogResult::InvalidConfiguration);
    CHECK(Logger::SetFormat({.pattern = "{message:1025}"}) == LogResult::InvalidConfiguration);
    CHECK(Logger::SetFormat({.pattern = "{message:{line}}"}) == LogResult::InvalidConfiguration);
    CHECK(Logger::SetFormat({.pattern = "{time:%Q}"}) == LogResult::InvalidConfiguration);
    LL_LOG(run.category, LogLevel::Info, "{}", "{not a field}");
    REQUIRE(Logger::Flush() == LogResult::Success);
    CHECK(run.sink->Read()[0].text == "{****Info}{not a field}");
    CHECK(Logger::SetFormat({.pattern = "{date:%Y-%m-%d} {time:%H:%M:%S} {threadid} {file}:{line} {operationid}"}) ==
          LogResult::Success);
    {
        OperationScope scope(OperationId::Create());
        LL_LOG(run.category, LogLevel::Info, "calendar");
    }
    REQUIRE(Logger::Flush() == LogResult::Success);
    const auto record = run.sink->Read().back().record;
    CHECK(record.captured == LogFields::All);
    CHECK(record.threadId == PlatformUtility::GetCurrentThreadId());
    CHECK(record.source.line() != 0);
    CHECK(record.operationId.value != 0);
}

TEST_CASE("Raw sinks declare metadata and do not trigger text rendering", "[logging]")
{
    Runtime run;
    run.sink->text             = false;
    run.sink->fields           = LogFields::Operation;
    run.options.format.pattern = "{date}{threadid}{file}";
    run.Start();
    const auto id = OperationId::Create();
    {
        OperationScope scope(id);
        LL_LOG(run.category, LogLevel::Info, "raw");
    }
    REQUIRE(Logger::Flush() == LogResult::Success);
    const auto output = run.sink->Read()[0];
    CHECK(output.text.empty());
    CHECK(output.record.captured == LogFields::Operation);
    CHECK(output.record.operationId == id);
    CHECK(output.record.threadId == 0);
}

TEST_CASE("Reentry bypasses message arguments and rejects writer waits", "[logging]")
{
    Runtime run;
    std::atomic<unsigned> nested{0};
    std::atomic<LogResult> flush{LogResult::Success}, shutdown{LogResult::Success};
    run.sink->onWrite = [&]
    {
        LL_LOG(run.category, LogLevel::Info, "nested {}", ++nested);
        flush    = Logger::Flush();
        shutdown = Logger::Shutdown();
    };
    run.Start();
    CHECK(Logger::Log(run.category, LogLevel::Info,
                      [&]
                      {
                          LL_LOG(run.category, LogLevel::Info, "nested {}", ++nested);
                          return std::string("outer");
                      }) == LogResult::Success);
    REQUIRE(Logger::Flush() == LogResult::Success);
    CHECK(nested == 0);
    CHECK(flush == LogResult::Reentrant);
    CHECK(shutdown == LogResult::Reentrant);
    CHECK(run.sink->Read().size() == 1);
}

TEST_CASE("Failed destinations do not prevent delivery to healthy destinations", "[logging]")
{
    Runtime run;
    auto failing       = std::make_shared<CaptureSink>();
    failing->failWrite = true;
    run.options.sinks.push_back({failing});
    run.Start();
    LL_LOG(run.category, LogLevel::Info, "first");
    CHECK(Logger::Flush() == LogResult::OutputFailure);
    LL_LOG(run.category, LogLevel::Info, "second");
    CHECK(Logger::Flush() == LogResult::OutputFailure);
    CHECK(failing->Read().size() == 1);
    CHECK(run.sink->Read().size() == 2);
}

TEST_CASE("Formatting failure releases the emitter and does not prevent later logging", "[logging]")
{
    Runtime run;
    run.Start();
    CHECK(Logger::Log(run.category, LogLevel::Info, []() -> std::string { throw std::bad_alloc(); }) ==
          LogResult::OutputFailure);
    LL_LOG(run.category, LogLevel::Info, "after failure");
    CHECK(Logger::Shutdown() == LogResult::Success);  // Failure released the local producer registration.
    CHECK(run.sink->Read().size() == 1);
    unsigned evaluated = 0;
    CHECK(Logger::Log(run.category, LogLevel::Info,
                      [&]
                      {
                          ++evaluated;
                          return std::string("inactive");
                      }) == LogResult::Inactive);
    CHECK(evaluated == 0);
}

TEST_CASE("Macro preparation failures do not escape or evaluate message arguments", "[logging]")
{
    Runtime run;
    run.Start();
    unsigned arguments  = 0;
    const auto category = []() -> LogCategory { throw std::bad_alloc(); };
    CHECK_NOTHROW([&] { LL_LOG(category(), LogLevel::Info, "{}", ++arguments); }());
    CHECK_NOTHROW([&] { LL_LOG_EVERY_N(category(), LogLevel::Info, 2, "{}", ++arguments); }());
    CHECK(arguments == 0);
    LL_LOG(run.category, LogLevel::Info, "still working");
    REQUIRE(Logger::Flush() == LogResult::Success);
    CHECK(run.sink->Read().size() == 1);
}

TEST_CASE("Custom formatter reentry skips nested message expressions", "[logging]")
{
    Runtime run;
    run.Start();
    unsigned arguments = 0;
    LL_LOG(run.category, LogLevel::Info, "{}", RecursiveFormat{run.category, &arguments});
    REQUIRE(Logger::Flush() == LogResult::Success);
    CHECK(arguments == 0);
    REQUIRE(run.sink->Read().size() == 1);
    CHECK(run.sink->Read()[0].record.message == "formatted");
}

TEST_CASE("Sink metadata requirements reject unsupported flag bits", "[logging][configuration]")
{
    for (const auto bits : {std::uint32_t{16}, std::uint32_t{1} << 31})
    {
        LoggerOptions options;
        auto sink    = std::make_shared<CaptureSink>();
        sink->fields = static_cast<LogFields>(bits);
        options.sinks.push_back({sink});
        CHECK(Logger::Initialize(std::move(options)) == LogResult::InvalidConfiguration);
        CHECK_FALSE(Logger::IsActive());
    }
}

TEST_CASE("Live output combines the effective category minimum with each sink minimum", "[logging][filter]")
{
    Runtime run;
    auto console                          = std::make_shared<CaptureSink>();
    run.options.globalMinimumLevel        = LogLevel::Info;
    run.options.sinks[0].sinkMinimumLevel = LogLevel::Trace;
    run.options.sinks.push_back({.sink = console, .sinkMinimumLevel = LogLevel::Warning});
    run.Start();
    unsigned evaluated = 0;
    auto expensive     = [&]
    {
        ++evaluated;
        return std::string("filtered");
    };
    CHECK(Logger::Log(run.category, LogLevel::Debug, expensive) == LogResult::Inactive);
    CHECK(evaluated == 0);
    CHECK(Logger::Log(run.category, LogLevel::Info, "info") == LogResult::Success);
    CHECK(Logger::Log(run.category, LogLevel::Warning, "warning") == LogResult::Success);
    REQUIRE(Logger::Flush() == LogResult::Success);
    REQUIRE(run.sink->Read().size() == 2);
    REQUIRE(console->Read().size() == 1);
    CHECK(run.sink->Read()[0].record.level == LogLevel::Info);
    CHECK(console->Read()[0].record.level == LogLevel::Warning);

    // Category Debug replaces the global default, even after that default becomes Critical.
    REQUIRE(Logger::SetCategoryMinimumLevel(run.category, LogLevel::Debug) == LogResult::Success);
    REQUIRE(Logger::SetGlobalMinimumLevel(LogLevel::Critical) == LogResult::Success);
    CHECK(Logger::Log(run.category, LogLevel::Debug, "category debug") == LogResult::Success);
    REQUIRE(Logger::Flush() == LogResult::Success);
    CHECK(run.sink->Read().size() == 3);
    CHECK(console->Read().size() == 1);

    // The category override never bypasses an output destination's own threshold.
    REQUIRE(Logger::SetSinkMinimumLevel(1, LogLevel::Error) == LogResult::Success);
    CHECK(Logger::Log(run.category, LogLevel::Warning, "file only") == LogResult::Success);
    REQUIRE(Logger::Flush() == LogResult::Success);
    CHECK(run.sink->Read().size() == 4);
    CHECK(console->Read().size() == 1);

    REQUIRE(Logger::SetCategoryMinimumLevel(run.category, std::nullopt) == LogResult::Success);
    CHECK(Logger::Log(run.category, LogLevel::Error, expensive) == LogResult::Inactive);
    CHECK(evaluated == 0);
    CHECK(Logger::Log(run.category, LogLevel::Critical, "both outputs") == LogResult::Success);
    REQUIRE(Logger::Flush() == LogResult::Success);
    CHECK(run.sink->Read().size() == 5);
    CHECK(console->Read().size() == 2);
}
