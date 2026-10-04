#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <LLUtils/Logging/LogFileSink.h>
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

#ifndef _WIN32
    #include <sys/syscall.h>
    #include <unistd.h>

namespace
{
    // Pause only the creator thread between native open and its protection lock. Other sinks keep
    // performing real retention so the test exercises publication against an actual concurrent remover.
    thread_local std::promise<void>* sharedLockEntered      = nullptr;
    thread_local std::shared_future<void>* sharedLockResume = nullptr;
}  // namespace

extern "C" int flock(int descriptor, int operation) noexcept
{
    if ((operation & LOCK_SH) != 0 && sharedLockEntered != nullptr)
    {
        auto* entered = std::exchange(sharedLockEntered, nullptr);
        auto* resume  = std::exchange(sharedLockResume, nullptr);
        entered->set_value();
        resume->wait();
    }
    return static_cast<int>(::syscall(SYS_flock, descriptor, operation));
}

TEST_CASE("Retention cannot unlink a segment before its creation lock is acquired", "[logging][file]")
{
    TempFolder folder;
    std::promise<void> entered, resume;
    auto paused  = entered.get_future();
    auto resumed = resume.get_future().share();
    auto creator = std::async(std::launch::async,
                              [&]
                              {
                                  sharedLockEntered = &entered;
                                  sharedLockResume  = &resumed;
                                  return std::make_unique<FileLogSink>(
                                      LogFileOptions{.path = folder.path / "viewer", .closedFiles = 0});
                              });
    // Unblock before destroying the async future, including when an assertion fails.
    struct Release
    {
        std::promise<void>& promise;
        bool released = false;
        void Resume()
        {
            if (!released)
            {
                released = true;
                promise.set_value();
            }
        }
        ~Release() { Resume(); }
    } release{resume};
    REQUIRE(paused.wait_for(5s) == std::future_status::ready);
    {
        FileLogSink competitor(LogFileOptions{.path = folder.path / "viewer", .closedFiles = 0});
    }
    release.Resume();
    auto file = creator.get();
    const LogRecord record{.message = "active segment survived"};
    REQUIRE(file->Write({record, record.message}));
    REQUIRE(file->Flush());
    std::vector<std::filesystem::path> segments;
    for (const auto& entry : std::filesystem::directory_iterator(folder.path))
    {
        CHECK(entry.path().extension() != ".opening");
        if (entry.path().extension() == ".log")
            segments.push_back(entry.path());
    }
    REQUIRE(segments.size() == 1);
    CHECK(ReadFile(segments.front()) == "active segment survived\r\n");
    CHECK_FALSE(FileHandle::CanRemove(segments.front()));
}
#endif

TEST_CASE("File output rotates whole UTF-8 records and retains closed segments", "[logging][file]")
{
    TempFolder folder;
    Runtime run;
    auto file = std::make_shared<FileLogSink>(
        LogFileOptions{.path = folder.path / "viewer", .rotationBytes = 18, .closedFiles = 1});
    run.options.sinks.push_back({file});
    run.Start();
    for (unsigned i = 0; i < 5; ++i)
        LL_LOG(run.category, LogLevel::Info, "{}: euro \xe2\x82\xac\nline", i);
    REQUIRE(Logger::Flush() == LogResult::Success);
    REQUIRE(Logger::Shutdown() == LogResult::Success);
    file.reset();
    run.options.sinks.clear();
    std::vector<std::string> segments;
    for (const auto& entry : std::filesystem::directory_iterator(folder.path))
        if (entry.path().extension() == ".log")
            segments.emplace_back(ReadFile(entry.path()));
    REQUIRE(segments.size() == 2);
    for (const auto& segment : segments)
    {
        CHECK_THAT(segment, ContainsSubstring("euro \xe2\x82\xac\r\nline\r\n"));
        CHECK(segment.size() > 18);
    }
}

TEST_CASE("Non-ASCII native file names preserve UTF-8 output", "[logging][file]")
{
    TempFolder folder;
    Runtime run;
    const auto filename = std::filesystem::path(u8"viewer-\xe2\x82\xac-\xf0\x9f\x8c\x8d");
    auto file           = std::make_shared<FileLogSink>(LogFileOptions{.path = folder.path / filename});
    run.options.sinks.push_back({file});
    run.Start();
    LL_LOG(run.category, LogLevel::Info, "unicode");
    REQUIRE(Logger::Shutdown() == LogResult::Success);
    bool found = false;
    for (const auto& entry : std::filesystem::directory_iterator(folder.path))
        if (entry.path().extension() == ".log")
        {
            CHECK(entry.path().filename().native().starts_with(filename.native()));
            found = true;
        }
    CHECK(found);
}

TEST_CASE("Retention cleanup failure preserves writes and retries at the next rotation", "[logging][file]")
{
    TempFolder folder;
    Runtime run;
    const auto lockPath = folder.path / "viewer.retention-lock";
    std::filesystem::create_directory(lockPath);  // Prevent acquisition without depending on administrator privileges.
    auto file = std::make_shared<FileLogSink>(
        LogFileOptions{.path = folder.path / "viewer", .rotationBytes = 8, .closedFiles = 1});
    run.options.sinks.push_back({file});
    run.Start();
    for (unsigned i = 0; i < 4; ++i)
        LL_LOG(run.category, LogLevel::Info, "record {}", i);
    REQUIRE(Logger::Flush() == LogResult::Success);
    auto count = [&]
    {
        unsigned result = 0;
        for (const auto& entry : std::filesystem::directory_iterator(folder.path))
            if (entry.path().extension() == ".log")
                ++result;
        return result;
    };
    CHECK(count() == 4);
    std::filesystem::remove(lockPath);
    LL_LOG(run.category, LogLevel::Info, "retry cleanup");
    REQUIRE(Logger::Flush() == LogResult::Success);
    CHECK(count() == 2);
}

TEST_CASE("Retention cleanup preserves emergency files and unrelated files", "[logging][file]")
{
    TempFolder folder;
    Runtime run;
    const auto emergency = folder.path / "viewer.emergency.log";
    const auto legacy    = folder.path / "viewer.log";
    {
        std::ofstream stream(legacy);
        stream << "legacy retained";
    }
    {
        std::ofstream stream(emergency);
        stream << "emergency retained";
    }
    auto file = std::make_shared<FileLogSink>(
        LogFileOptions{.path = folder.path / "viewer", .rotationBytes = 8, .closedFiles = 0});
    run.options.sinks.push_back({file});
    run.Start();
    for (unsigned i = 0; i < 4; ++i)
        LL_LOG(run.category, LogLevel::Info, "rotation {}", i);
    REQUIRE(Logger::Flush() == LogResult::Success);
    const auto content = ReadFile(emergency);
    CHECK(content == "emergency retained");
    CHECK(std::filesystem::exists(legacy));
}
