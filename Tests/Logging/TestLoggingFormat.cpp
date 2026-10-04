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

TEST_CASE("Chrono layouts preserve captured UTC time and requested precision", "[logging][format]")
{
    using namespace std::chrono;
    LogRecord record;
    record.timestamp = time_point_cast<system_clock::duration>(sys_days(2026y / October / 3) + 6h + 7min + 8s +
                                                               123456us);
    constexpr std::string_view expected[] = {"06:07:08", "06:07:08.123", "06:07:08.123456", "06:07:08.123456000"};
    std::string rendered;
    for (unsigned precision = 0; precision < std::size(expected); ++precision)
    {
        LoggingDetail::Pattern pattern(
            {.pattern = "{date} {time}", .timestampPrecision = static_cast<LogTimestampPrecision>(precision)});
        pattern.RenderInto(record, rendered);
        CHECK(rendered == "2026-10-03 " + std::string(expected[precision]));
    }
    LoggingDetail::Pattern repeated(
        {.pattern = "{time:%S|%S %z}", .timestampPrecision = LogTimestampPrecision::Microseconds});
    repeated.RenderInto(record, rendered);
    CHECK(rendered == "08.123456|08.123456 +0000");
}

TEST_CASE("Local chrono layouts honor the cached timezone across winter and summer", "[logging][format]")
{
    using namespace std::chrono;
    LoggingDetail::Pattern pattern(
        {.pattern = "{date:%F}T{time:%T %z}", .utc = false, .timestampPrecision = LogTimestampPrecision::Seconds});
    for (const auto day : {sys_days(2026y / January / 3), sys_days(2026y / July / 3)})
    {
        LogRecord record;
        record.timestamp = day + 12h;
        std::string rendered;
        pattern.RenderInto(record, rendered);
        const zoned_time local(current_zone(), floor<seconds>(record.timestamp));
        CHECK(rendered == std::format("{:%FT%T %z}", local));
    }
}

TEST_CASE("Default options and custom date layouts preserve their rendering contracts", "[logging][format]")
{
    LoggerOptions options;
    CHECK(options.globalMinimumLevel == LogLevel::Info);
    CHECK(options.queueRecords == 8192);
    CHECK(options.messageBytes == 16 * 1024 * 1024);
    CHECK(options.flushInterval == 1s);
    CHECK(options.flushSeverity == LogLevel::Error);
    CHECK(options.format.pattern == "[{date}][{time}][{loglevel}]{message}");
    CHECK(options.format.timestampPrecision == LogTimestampPrecision::Milliseconds);
    CHECK(options.format.utc);
    CHECK(options.sinks.empty());
    CHECK(options.emergencyPath.empty());
    const LogSinkOptions sink;
    CHECK(sink.sinkMinimumLevel == LogLevel::Trace);
    CHECK_FALSE(sink.sink);
    CHECK_FALSE(sink.format);
    const LogFileOptions file;
    CHECK(file.path.empty());
    CHECK(file.rotationBytes == 10 * 1024 * 1024);
    CHECK(file.closedFiles == 5);
    CHECK(LogDefaults::DateSpec == "%Y-%m-%d");
    CHECK(LogDefaults::TimeSpec == "%H:%M:%S");
    CHECK(LogDefaults::HistoryTriggerSeverity == LogLevel::Error);
    CHECK_FALSE(options.history.enabled);
    CHECK(options.history.records == 128);
    CHECK(options.history.bytes == 1024 * 1024);
    CHECK(options.history.historyMinimumLevel == LogLevel::Debug);
    LogRecord record;
    using namespace std::chrono;
    record.timestamp = sys_days(2026y / October / 3) + 6h + 7min + 8s + 123ms;
    LoggingDetail::Pattern custom({.pattern = "{date:%d/%m/%Y} {time:%I:%M:%S %p}"});
    std::string rendered;
    custom.RenderInto(record, rendered);
    CHECK(rendered == "03/10/2026 06:07:08.123 AM");
    LoggingDetail::Pattern implicit({.pattern = "{date} {time}|{loglevel}|{loglevel:8}|{loglevel:>8}"});
    implicit.RenderInto(record, rendered);
    CHECK(rendered == "2026-10-03 06:07:08.123|Info|Info    |    Info");
}

TEST_CASE("Metadata width and decimal capacity boundaries preserve complete values", "[logging][format]")
{
    LogRecord record;
    record.sequence          = UINT64_MAX;
    record.threadId          = UINT64_MAX;
    record.operationId.value = UINT64_MAX;
    LoggingDetail::Pattern format({.pattern = "{sequence}|{threadid}|{operationid}|{loglevel:>1024}"});
    std::string rendered;
    format.RenderInto(record, rendered);
    const std::string maximum = "18446744073709551615";
    CHECK(rendered == maximum + "|" + maximum + "|" + maximum + "|" + std::string(1020, ' ') + "Info");
    CHECK_THROWS_AS(LoggingDetail::Pattern(LogTextFormat{.pattern = "{message:1025}"}), std::invalid_argument);
}
