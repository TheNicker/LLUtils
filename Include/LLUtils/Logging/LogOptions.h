#pragma once
#include "LogRecord.h"
#include <filesystem>
#include <optional>
#include <vector>

namespace LLUtils
{
    namespace LoggingDetail
    {
        // Compile-time macro policy for one call site; runtime counters stay private to Logger.
        struct Repetition
        {
            std::uint64_t count;  // Count policy emits the first call and then every count-th eligible call.
            std::chrono::steady_clock::duration period;  // Nonzero selects time gating instead of count gating.
        };
    }  // namespace LoggingDetail

    // Controls rendered timestamp precision only; capture, clock accuracy and flushing are unchanged.
    enum class LogTimestampPrecision
    {
        Seconds,
        Milliseconds,
        Microseconds,
        Nanoseconds
    };

    // Library policy defaults, kept separate from option types. Application destinations and overrides
    // belong to application setup; empty values, state sentinels and fixed safety/file contracts stay local.
    struct LogDefaults
    {
        // Text layout, chrono specifications and implicit metadata padding.
        static constexpr std::string_view Pattern                 = "[{date}][{time}][{loglevel}]{message}";
        static constexpr std::string_view DateSpec                = "%Y-%m-%d";
        static constexpr std::string_view TimeSpec                = "%H:%M:%S";
        static constexpr bool Utc                                 = true;
        static constexpr LogTimestampPrecision TimestampPrecision = LogTimestampPrecision::Milliseconds;
        static constexpr unsigned FieldWidth                      = 0;
        static constexpr char FieldAlign                          = '<';
        static constexpr char FieldFill                           = ' ';

        // Live admission and pending-output buffering policy.
        static constexpr LogLevel GlobalMinimumLevel                       = LogLevel::Info;
        static constexpr LogLevel SinkMinimumLevel                         = LogLevel::Trace;
        static constexpr std::size_t QueueRecords                          = 8192;
        static constexpr std::size_t MessageBytes                          = 16 * 1024 * 1024;
        static constexpr std::chrono::steady_clock::duration FlushInterval = std::chrono::seconds(1);
        static constexpr LogLevel FlushSeverity                            = LogLevel::Error;

        // Independent diagnostic retention and automatic replay threshold.
        static constexpr bool HistoryEnabled             = false;
        static constexpr std::size_t HistoryRecords      = 128;
        static constexpr std::size_t HistoryBytes        = 1024 * 1024;
        static constexpr LogLevel HistoryMinimumLevel    = LogLevel::Debug;
        static constexpr LogLevel HistoryTriggerSeverity = LogLevel::Error;

        // Generated file-segment policy, excluding active/emergency/legacy files from retention.
        static constexpr std::size_t RotationBytes = 10 * 1024 * 1024;
        static constexpr std::size_t ClosedFiles   = 5;
    };

    // Validated rendering settings; they do not control capture time or timestamp accuracy.
    struct LogTextFormat
    {
        std::string pattern{LogDefaults::Pattern};  // Placeholder layout compiled when configuration is published.
        // False renders with the local timezone captured by the compiled pattern.
        bool utc = LogDefaults::Utc;
        // Fractional-second digits for rendered timestamps only.
        LogTimestampPrecision timestampPrecision    = LogDefaults::TimestampPrecision;
        bool operator==(const LogTextFormat&) const = default;
    };

    // One owned destination with an independent severity threshold and optional text layout.
    struct LogSinkOptions
    {
        std::shared_ptr<LogSink> sink;  // Non-null; callbacks are serialized on the session writer.
        // Additional output filter: live events must also pass the effective global/category minimum.
        // Trace adds no restriction; Off excludes this sink, including manual history dumps.
        // Automatic replay tests the trigger event; manual replay bypasses severity thresholds except Off.
        // With global Info and console Warning, this sink receives Warning and above.
        LogLevel sinkMinimumLevel = LogDefaults::SinkMinimumLevel;
        std::optional<LogTextFormat> format;  // Empty follows the runtime default layout, including updates.
    };
    // Writer-owned recent records for diagnostic replay; retention is independent of live eligibility.
    struct LogHistoryOptions
    {
        bool enabled = LogDefaults::HistoryEnabled;  // Enables retention and explicit/error-triggered history dumps.
        std::size_t records =
            LogDefaults::HistoryRecords;                // Maximum retained records; oldest records are evicted first.
        std::size_t bytes = LogDefaults::HistoryBytes;  // Sum of message capacities; records exceeding it are omitted.
        // Independent retention filter, even when global/sink filters hide a live event.
        // An explicit category Off also disables capture; global Off does not.
        LogLevel historyMinimumLevel = LogDefaults::HistoryMinimumLevel;
    };
    // Initialization settings for one session; runtime threshold/layout updates use immutable snapshots.
    struct LoggerOptions
    {
        // Logger-wide default live filter; a category override replaces it rather than adding another floor.
        // Each live sink also applies sinkMinimumLevel. Global Info + file Trace gives Info and above.
        // Off suppresses default live output; category overrides can still enable it, and history is independent.
        LogLevel globalMinimumLevel = LogDefaults::GlobalMinimumLevel;
        // Ring slots for data and control commands; producers wait for a free slot.
        std::size_t queueRecords = LogDefaults::QueueRecords;
        // Normal queued/in-flight message capacities; one extra oversized record remains complete.
        std::size_t messageBytes = LogDefaults::MessageBytes;
        // Positive target buffering interval, armed by the first pending write; idle sinks have no timer.
        std::chrono::steady_clock::duration flushInterval = LogDefaults::FlushInterval;
        // Successful live delivery at or above this severity flushes sinks immediately; Off disables it.
        LogLevel flushSeverity = LogDefaults::FlushSeverity;
        LogTextFormat format;               // Default layout for destinations without an explicit override.
        LogHistoryOptions history;          // Separate bounded diagnostic retention.
        std::vector<LogSinkOptions> sinks;  // Fixed destination list for this session; thresholds/layouts can change.
        // First nonempty configured path is process-owned and survives shutdown; empty leaves it unopened.
        std::filesystem::path emergencyPath;
    };

    // Generated session segments with complete-record rotation and best-effort closed-file retention.
    struct LogFileOptions
    {
        std::filesystem::path path;  // Base directory/name prefix, not the generated segment filename.
        // Positive segment byte threshold including terminators; a single larger record is never split.
        std::size_t rotationBytes = LogDefaults::RotationBytes;
        // Closed generated segments to keep across sessions; active/emergency/legacy files are excluded.
        // Cleanup failures can exceed this target and are retried at subsequent rotations.
        std::size_t closedFiles = LogDefaults::ClosedFiles;
    };

}  // namespace LLUtils
