#include <catch2/catch_test_macros.hpp>
#include <LLUtils/BoundedWorkQueue.h>
#include <LLUtils/Emergency.h>
#include <LLUtils/FileHandle.h>
#include <LLUtils/ScopedFileLock.h>
#include <LLUtils/RepetitionLimiter.h>
#include "../Support/TempFolder.h"
#include <atomic>
#include <future>
#include <latch>
#include <stdexcept>
#include <thread>

namespace
{
    using namespace LLUtils;
    using namespace LLUtils::TestSupport;
    using namespace std::chrono_literals;
    using Queue = BoundedWorkQueue<int>;
    using Emergency = EmergencyDetail::Emergency;
}  // namespace
TEST_CASE("Queue slots release on dequeue but normal weight stays charged through processing", "[utilities][queue]")
{
    Queue queue(1, 4);
    REQUIRE(queue.Push(1, 4));
    std::latch entered(1);
    auto producer = std::async(std::launch::async,
                               [&]
                               {
                                   entered.count_down();
                                   return queue.Push(2, 1);
                               });
    entered.wait();
    CHECK(producer.wait_for(30ms) == std::future_status::timeout);
    auto first = queue.WaitUntil();
    CHECK(first.Status() == Queue::WaitStatus::Item);
    CHECK(first.Value() == 1);
    CHECK(producer.wait_for(30ms) == std::future_status::timeout);
    auto moved = std::move(first);
    first.Complete();
    CHECK(producer.wait_for(30ms) == std::future_status::timeout);
    moved.Complete();
    moved.Complete();
    const bool ready = producer.wait_for(5s) == std::future_status::ready;
    queue.Close();
    CHECK(ready);
    CHECK(producer.get());
    auto second = queue.WaitUntil();
    REQUIRE(second);
    CHECK(second.Value() == 2);
    CHECK(queue.WaitUntil().Status() == Queue::WaitStatus::Closed);
}
TEST_CASE("Queue has one extra oversized allowance independent of normal weight", "[utilities][queue]")
{
    Queue queue(3, 4);
    REQUIRE(queue.Push(1, 5));
    REQUIRE(queue.Push(2, 4));
    std::latch entered(1);
    auto producer = std::async(std::launch::async,
                               [&]
                               {
                                   entered.count_down();
                                   return queue.Push(3, 9);
                               });
    entered.wait();
    auto oversized = queue.WaitUntil();
    auto normal    = queue.WaitUntil();
    CHECK(producer.wait_for(30ms) == std::future_status::timeout);
    oversized.Complete();
    const bool ready = producer.wait_for(5s) == std::future_status::ready;
    queue.Close();
    CHECK(ready);
    CHECK(producer.get());
    auto next = queue.WaitUntil();
    REQUIRE(next);
    CHECK(next.Value() == 3);
    CHECK(normal.Value() == 2);
}
TEST_CASE("Queue close wakes admission and consumer waits without consuming commit bookkeeping", "[utilities][queue]")
{
    Queue queue(1, 4);
    unsigned commits = 0;
    REQUIRE(queue.Push(7, 0, [&](int& value) noexcept { value = ++commits; }));
    auto producer = std::async(std::launch::async, [&] { return queue.Push(8, 0, [&](int&) noexcept { ++commits; }); });
    CHECK(producer.wait_for(30ms) == std::future_status::timeout);
    queue.Close();
    CHECK_FALSE(producer.get());
    CHECK(commits == 1);
    CHECK_FALSE(queue.TryRegisterProducer());
    auto item = queue.WaitUntil(std::chrono::steady_clock::now() - 1s);
    REQUIRE(item);
    CHECK(item.Value() == 1);
    CHECK(queue.WaitUntil().Status() == Queue::WaitStatus::Closed);
    Queue empty(1, 1);
    CHECK(empty.WaitUntil(std::chrono::steady_clock::now()).Status() == Queue::WaitStatus::Timeout);
    auto consumer = std::async(std::launch::async, [&] { return empty.WaitUntil().Status(); });
    CHECK(consumer.wait_for(30ms) == std::future_status::timeout);
    empty.Close();
    CHECK(consumer.get() == Queue::WaitStatus::Closed);
}
TEST_CASE("Producer registrations survive close until moved ownership is released", "[utilities][queue]")
{
    Queue queue(1, 1);
    auto registration = queue.TryRegisterProducer();
    REQUIRE(registration);
    auto moved = std::move(registration);
    registration.reset();
    auto replacement = queue.TryRegisterProducer();
    *moved           = std::move(*replacement);
    replacement.reset();
    queue.Close();
    auto stopped = std::async(std::launch::async, [&] { queue.WaitForProducers(); });
    CHECK(stopped.wait_for(30ms) == std::future_status::timeout);
    moved.reset();
    CHECK(stopped.wait_for(5s) == std::future_status::ready);
    stopped.get();
    CHECK_FALSE(queue.Push(1));
    CHECK_THROWS_AS(Queue(0, 1), std::invalid_argument);
    CHECK_THROWS_AS(Queue(1, 0), std::invalid_argument);
}
TEST_CASE("Owned files preserve binary bytes, exclusive creation, append and checked close", "[utilities][file]")
{
    TempFolder folder;
    const auto path = folder.path / std::filesystem::path(u8"日志.bin");
    const std::string bytes("a\0b\r\n", 5);
    auto file = FileHandle::OpenAppend(path, FileHandle::Creation::CreateNew, FileHandle::Protection::PreventRemoval);
    CHECK_FALSE(FileHandle::CanRemove(path));
    CHECK(file.Write(std::as_bytes(std::span{bytes.data(), bytes.size()})));
    CHECK(file.Flush());
    CHECK(ReadFile(path) == bytes);
    CHECK_THROWS_AS(FileHandle::OpenAppend(path, FileHandle::Creation::CreateNew), std::system_error);
    auto moved = std::move(file);
    CHECK(file.Close());
    CHECK_FALSE(file.Write({}));
    CHECK_FALSE(file.Flush());
    CHECK(moved.Write({}));
    CHECK(moved.Close());
    CHECK(moved.Close());
    CHECK(FileHandle::CanRemove(path));
    file = FileHandle::OpenAppend(path);
    CHECK(FileHandle::CanRemove(path));
    CHECK(file.Write(std::as_bytes(std::span{bytes.data(), bytes.size()})));
    CHECK(file.Close());
    CHECK(ReadFile(path) == bytes + bytes);
    CHECK_THROWS_AS(FileHandle::OpenAppend(folder.path), std::system_error);
}
TEST_CASE("File move replacement closes and flushes the previous owned stream", "[utilities][file]")
{
    TempFolder folder;
    auto first             = FileHandle::OpenAppend(folder.path / "first");
    const std::string text = "previous";
    REQUIRE(first.Write(std::as_bytes(std::span{text.data(), text.size()})));
    auto second = FileHandle::OpenAppend(folder.path / "second");
    first       = std::move(second);
    CHECK(ReadFile(folder.path / "first") == text);
    CHECK(second.Close());
    CHECK(first.Close());
}
#ifndef _WIN32
TEST_CASE("Owned file flush and close report buffered write failure", "[utilities][file]")
{
    auto file               = FileHandle::OpenAppend("/dev/full");
    const std::string bytes = "buffered";
    CHECK(file.Write(std::as_bytes(std::span{bytes.data(), bytes.size()})));
    CHECK_FALSE(file.Close());
    CHECK(file.Close());
    auto flushed = FileHandle::OpenAppend("/dev/full");
    CHECK(flushed.Write(std::as_bytes(std::span{bytes.data(), bytes.size()})));
    CHECK_FALSE(flushed.Flush());
}
#endif
TEST_CASE("Scoped file lock ownership moves and blocks another process until release", "[utilities][file]")
{
    TempFolder folder;
    auto lock  = std::make_unique<ScopedFileLock>(folder.path / "coordination-lock");
    auto moved = std::make_unique<ScopedFileLock>(std::move(*lock));
    lock.reset();
    auto replacement = std::make_unique<ScopedFileLock>(folder.path / "other-lock");
    *replacement     = std::move(*moved);
    moved.reset();
    const std::string command = std::string("\"") + LLUTILS_TEST_PROCESS + "\" lock \"" + folder.path.string() + "\"";
    auto child                = std::async(std::launch::async,
                                           [&]
                                           {
#ifdef _WIN32
                                return std::system(("\"" + command + "\"").c_str());
#else
        return std::system(command.c_str());
#endif
                            });
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    while (!std::filesystem::exists(folder.path / "lock-started") && std::chrono::steady_clock::now() < deadline)
        std::this_thread::yield();
    CHECK(std::filesystem::exists(folder.path / "lock-started"));
    CHECK_FALSE(std::filesystem::exists(folder.path / "lock-acquired"));
    CHECK(child.wait_for(30ms) == std::future_status::timeout);
    replacement.reset();
    CHECK(child.get() == 0);
    CHECK(std::filesystem::exists(folder.path / "lock-acquired"));
    CHECK_THROWS_AS(ScopedFileLock(folder.path), std::system_error);
}
TEST_CASE("Repetition limiter has deterministic count, time and generation boundaries", "[utilities][limiter]")
{
    RepetitionLimiter count(3);
    CHECK(count.Admit(0, {}) == 0);
    CHECK_FALSE(count.Admit(0, {}));
    CHECK_FALSE(count.Admit(0, {}));
    CHECK(count.Admit(0, {}) == 2);
    CHECK_FALSE(count.Admit(0, {}));
    CHECK(count.Admit(1, {}) == 0);
    RepetitionLimiter timed(1, 10s);
    const auto now = std::chrono::steady_clock::time_point{};
    CHECK(timed.Admit(0, now) == 0);
    CHECK_FALSE(timed.Admit(0, now + 9s));
    CHECK(timed.Admit(0, now + 10s) == 1);
    CHECK_FALSE(timed.Admit(0, now + 19s));
    CHECK(timed.Admit(1, now + 19s) == 0);
    CHECK_THROWS_AS(RepetitionLimiter(0), std::invalid_argument);
    CHECK_THROWS_AS(RepetitionLimiter(1, -1s), std::invalid_argument);
}
TEST_CASE("Concurrent repetition calls share one count policy", "[utilities][limiter]")
{
    RepetitionLimiter limiter(3);
    std::atomic<unsigned> admitted = 0, skipped = 0;
    std::vector<std::thread> threads;
    for (unsigned t = 0; t < 3; ++t)
        threads.emplace_back(
            [&]
            {
                for (unsigned i = 0; i < 100; ++i)
                    if (auto result = limiter.Admit(7, {}))
                    {
                        ++admitted;
                        skipped += static_cast<unsigned>(*result);
                    }
            });
    for (auto& thread : threads)
        thread.join();
    CHECK(admitted == 100);
    CHECK(skipped == 198);
    CHECK(limiter.Admit(7, {}) == 2);
}

TEST_CASE("Replacing a work item releases its previous charge and transfers completion ownership", "[utilities][queue]")
{
    Queue queue(3, 3);
    REQUIRE(queue.Push(1, 2));
    REQUIRE(queue.Push(2, 1));
    auto first  = queue.WaitUntil();
    auto second = queue.WaitUntil();
    first       = std::move(second);
    second.Complete();
    CHECK(first.Value() == 2);
    auto producer    = std::async(std::launch::async, [&] { return queue.Push(3, 2); });
    const bool ready = producer.wait_for(5s) == std::future_status::ready;
    queue.Close();
    CHECK(ready);
    CHECK(producer.get());
    auto next = queue.WaitUntil();
    REQUIRE(next);
    CHECK(next.Value() == 3);
}

TEST_CASE("Dequeue constructs its final owned result with exactly one payload move", "[utilities][queue]")
{
    struct Payload
    {
        unsigned* moves;
        explicit Payload(unsigned& count) : moves(&count) {}
        Payload(Payload&& other) noexcept : moves(other.moves) { ++*moves; }
        Payload(const Payload&)            = delete;
        Payload& operator=(const Payload&) = delete;
    };
    unsigned moves = 0;
    BoundedWorkQueue<Payload> queue(1, 1);
    REQUIRE(queue.Push(Payload(moves), 1));
    const auto before = moves;
    auto work         = queue.WaitUntil();
    REQUIRE(work);
    CHECK(moves == before + 1);
    auto moved = std::move(work);
    CHECK_FALSE(work);
    CHECK(moved);
    queue.Close();
}

TEST_CASE("The diagnostic scope is per thread, counted and restored during unwinding", "[utilities][emergency]")
{
    using Scope = Emergency::Scope;
    // With no scope installed the exception layer notifies observers on this thread, which is the state
    // every ordinary error starts from.
    CHECK_FALSE(Emergency::Active());
    {
        const Scope scope;
        CHECK(Emergency::Active());
        {
            const Scope nested;
            CHECK(Emergency::Active());
        }
        // Nesting is counted, so leaving the inner scope must not clear the outer one.
        CHECK(Emergency::Active());
    }
    CHECK_FALSE(Emergency::Active());
    try
    {
        const Scope scope;
        // A standard exception is deliberate: unwinding the scope is the behaviour under test, so the
        // diagnostic machinery this predicate guards stays out of it.
        throw std::runtime_error("unwind");
    }
    catch (const std::runtime_error&)
    {
    }
    // If the scope did not restore nesting on the way out, every later report on this thread would be
    // suppressed.
    CHECK_FALSE(Emergency::Active());
    const Scope scope;
    unsigned seen = 0;
    std::thread worker([&] { seen += Emergency::Active() ? 1u : 0u; });
    worker.join();
    // The state is thread local, so a worker must still report while this thread is inside a scope.
    CHECK(seen == 0);
    CHECK(Emergency::Active());
}
