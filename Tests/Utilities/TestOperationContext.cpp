#include <catch2/catch_test_macros.hpp>
#include <LLUtils/Logging/OperationContext.h>
#include <stdexcept>
#include <thread>

namespace
{
    using namespace LLUtils;
}  // namespace

TEST_CASE("Operation scopes restore the previous operation after normal exit", "[utilities][operation]")
{
    CHECK(OperationScope::Current().value == 0);
    const auto outer = OperationId::Create();
    {
        OperationScope scope(outer);
        CHECK(OperationScope::Current() == outer);
        {
            const auto inner = OperationId::Create();
            OperationScope nested(inner);
            CHECK(OperationScope::Current() == inner);
        }
        // Leaving the inner scope must restore the outer one, not the zero default.
        CHECK(OperationScope::Current() == outer);
    }
    CHECK(OperationScope::Current().value == 0);
}

TEST_CASE("Operation scopes restore the previous operation after stack unwinding", "[utilities][operation]")
{
    const auto outer = OperationId::Create();
    {
        OperationScope scope(outer);
        try
        {
            const auto inner = OperationId::Create();
            OperationScope nested(inner);
            CHECK(OperationScope::Current() == inner);
            // A standard exception is used deliberately: unwinding the inner scope is the behaviour
            // under test, so the diagnostic machinery of the library stays out of it.
            throw std::runtime_error("unwind");
        }
        catch (const std::runtime_error&)
        {
        }
        CHECK(OperationScope::Current() == outer);
    }
    CHECK(OperationScope::Current().value == 0);
}

TEST_CASE("Operation identifiers are unique and zero means absent", "[utilities][operation]")
{
    const auto first  = OperationId::Create();
    const auto second = OperationId::Create();
    CHECK(first != second);
    CHECK(first.value != 0);
    CHECK(second.value != 0);
    CHECK(OperationId{}.value == 0);
}

TEST_CASE("Operation scopes are per thread and do not leak across workers", "[utilities][operation]")
{
    const auto main = OperationId::Create();
    OperationScope scope(main);
    unsigned seen = 0;
    std::thread worker(
        [&]
        {
            // A worker must observe no operation until it installs one explicitly.
            seen += OperationScope::Current().value == 0 ? 1u : 0u;
            const auto own = OperationId::Create();
            OperationScope workerScope(own);
            seen += OperationScope::Current() == own ? 1u : 0u;
        });
    worker.join();
    CHECK(seen == 2);
    CHECK(OperationScope::Current() == main);
}