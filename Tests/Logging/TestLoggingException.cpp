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

TEST_CASE("Exception records carry the operation scope active when the diagnostic was created", "[logging][exception]")
{
    Runtime run;
    run.sink->text   = false;
    run.sink->fields = LogFields::All;
    run.Start();
    const auto outerId = OperationId::Create();
    const auto innerId = OperationId::Create();
    {
        OperationScope outer(outerId);
        {
            // The innermost scope wins, so this must be attributed to innerId and not to the
            // enclosing outerId. This is the assertion that catches a scope-restore regression.
            OperationScope inner(innerId);
            // Mode::Error with a deep callStackLevel keeps the report minimal, so the record does
            // not depend on how many frames the test framework added above this line.
            Exception nested(Exception::ErrorCode::RuntimeError, "nested", "raised in inner scope", false,
                             Exception::Mode::Error, 64);
        }
        try
        {
            OperationScope inner(innerId);
            throw std::runtime_error("unwind");
        }
        catch (const std::exception&)
        {
            // Unwinding already destroyed the inner scope, so this must fall back to outerId. The
            // ordinary catch-boundary case, and the one that needs correlation to stay correct.
            Exception afterUnwind(Exception::ErrorCode::RuntimeError, "afterUnwind", "raised in outer scope", false,
                                 Exception::Mode::Error, 64);
        }
    }
    // No scope is open, so the fallback reads zero and {operationid} renders as absent.
    Exception unscoped(Exception::ErrorCode::RuntimeError, "unscoped", "raised without a scope", false,
                       Exception::Mode::Error, 64);
    CHECK(OperationScope::Current().value == 0);

    REQUIRE(Logger::Flush() == LogResult::Success);
    const auto output = run.sink->Read();
    REQUIRE(output.size() == 3);
    CHECK(output[0].record.operationId == innerId);
    CHECK(output[1].record.operationId == outerId);
    CHECK(output[2].record.operationId.value == 0);
    CHECK(output[0].record.threadId == PlatformUtility::GetCurrentThreadId());
}

TEST_CASE("Old exception snapshots cannot access a stopped or replacement session", "[logging][exception][concurrency]")
{
    Runtime run;
    std::latch entered(1), release(1);
    auto blocker = Exception::OnException.Subscribe(
        [&](const auto&)
        {
            entered.count_down();
            release.wait();
        });
    run.Start();
    std::jthread notification([] { Exception::OnException.Raise(Exception::EventArgs{.description = "late"}); });
    entered.wait();
    blocker.Unsubscribe();
    CHECK(Logger::Shutdown() == LogResult::Success);
    run.Start();
    release.count_down();
    notification.join();
    REQUIRE(Logger::Flush() == LogResult::Success);
    CHECK(run.sink->Read().empty());
}

TEST_CASE("Logging-originated LLUtils exceptions bypass ordinary exception callbacks", "[logging][exception]")
{
    Runtime run;
    run.Start();
    unsigned observed = 0;
    auto subscription = Exception::OnException.Subscribe([&](const auto&) { ++observed; });
    CHECK(Logger::Log(run.category, LogLevel::Info,
                      []() -> std::string { LL_EXCEPTION(Exception::ErrorCode::RuntimeError, "formatter failure"); }) ==
          LogResult::OutputFailure);
    CHECK(observed == 0);
    CHECK(run.sink->Read().empty());
    LL_EXCEPTION_DONT_THROW(Exception::ErrorCode::RuntimeError, "ordinary failure");
    REQUIRE(Logger::Flush() == LogResult::Success);
    CHECK(observed == 1);
    REQUIRE(run.sink->Read().size() == 1);
    CHECK(run.sink->Read()[0].record.category.Name() == "LLUtils.Exception");
}
