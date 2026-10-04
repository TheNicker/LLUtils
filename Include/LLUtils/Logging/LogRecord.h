#pragma once
#include "OperationContext.h"
#include "../BitFlags.h"
#include <chrono>
#include <cstdint>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>

namespace LLUtils
{
    // Values increase with severity; a minimum accepts levels >= it. Off disables output, not an event level.
    enum class LogLevel
    {
        Trace,
        Debug,
        Info,
        Warning,
        Error,
        Critical,
        Off
    };
    // Outcome of a logger operation; successful Log means admitted, while successful Flush means drained.
    enum class LogResult
    {
        Success,
        Inactive,
        InvalidConfiguration,
        OutputFailure,
        Reentrant
    };
    // Optional producer metadata. LogFieldFlags combines requirements and marks captured values.
    enum class LogFields : std::uint32_t
    {
        None      = 0 << 0,
        Timestamp = 1 << 0,
        Thread    = 1 << 1,
        Source    = 1 << 2,
        Operation = 1 << 3,
        All       = Timestamp | Thread | Source | Operation
    };
    LLUTILS_DEFINE_ENUM_CLASS_FLAG_OPERATIONS(LogFields)
    using LogFieldFlags = BitFlags<LogFields>;
    // One range check for the enum, shared by configuration validation and level rendering.
    constexpr bool IsValidLevel(LogLevel level) noexcept
    {
        return static_cast<unsigned>(level) <= static_cast<unsigned>(LogLevel::Off);
    }
    constexpr std::string_view LogLevelName(LogLevel level)
    {
        constexpr std::string_view names[] = {"Trace", "Debug", "Info", "Warning", "Error", "Critical", "Off"};
        return IsValidLevel(level) ? names[static_cast<unsigned>(level)] : std::string_view{"?"};
    }

    // Registered identity with shared immutable name storage; remains valid across logger sessions.
    class LogCategory
    {
      public:

        LogCategory() = default;
        std::string_view Name() const noexcept { return fName ? *fName : std::string_view{}; }
        std::uint64_t Identity() const noexcept { return fIdentity; }
        explicit operator bool() const noexcept { return fName != nullptr; }

      private:

        friend class Logger;
        LogCategory(std::shared_ptr<const std::string> name, std::uint64_t identity)
            : fName(std::move(name)), fIdentity(identity)
        {
        }
        // Shared ownership keeps borrowed Name() views valid while the category is alive.
        std::shared_ptr<const std::string> fName;
        // Process-wide registration key; zero identifies an unregistered category.
        std::uint64_t fIdentity = 0;
    };

    // Keep the owned message, category identity and captured event metadata together for live delivery and
    // history replay. Replay preserves original identities; the queue owns admission accounting and the
    // data command owns live formatter versions, so retaining history does not keep those resources alive.
    struct LogRecord
    {
        LogCategory category;             // Owns the registered category name used by filtering and rendering.
        LogLevel level = LogLevel::Info;  // Event severity, independent of each consumer threshold.
        std::string message{""};          // Owned UTF-8 text; its capacity is charged to queue/history byte limits.
        // Optional origin metadata is captured before message formatting and any admission wait. Display
        // precision is a rendering choice, not a guarantee of clock accuracy; captured identifies valid fields.
        std::chrono::system_clock::time_point timestamp{};
        std::uint64_t threadId = 0;  // Originating producer thread, not the writer or replay thread.
        // References static text in the emitting module; that module must remain loaded until records drain.
        std::source_location source{};
        // Correlate records from the same logical request across threads/async work. Captured from the current
        // OperationScope or supplied original exception context; zero means absent or not requested by consumers.
        OperationId operationId;
        // Assigned from one at successful queue admission under the queue mutex; resets each session. This
        // identifies admission order, not timestamp order or causality. Replay keeps it; generated markers
        // retain zero (rendered as "-"). Filtered/skipped calls and control commands consume no sequence.
        std::uint64_t sequence = 0;
        // Eligible calls skipped by this call site's explicit repetition limiter since its previous emission.
        // Aggregate across threads/operations and attach to the next emitted record and its message suffix.
        // Zero denotes ordinary logging or no skipped calls; replay preserves the original count.
        std::uint64_t skipped = 0;
        // Validity mask for optional timestamp/thread/source/operation metadata. Omitted default values must
        // not be mistaken for observed values; history cannot reconstruct fields that were never captured.
        LogFieldFlags captured = LogFields::None;
    };
    // Borrowed sink callback input; copy required data before Write returns.
    struct LogDelivery
    {
        const LogRecord& record;  // Original owned record, or a generated history boundary marker.
        std::string_view text;    // Rendered UTF-8 without sink terminator; empty when NeedsText() is false.
        bool replay = false;      // Delivery belongs to a diagnostic-history dump.
        bool marker = false;      // Generated history boundary rather than a producer event.
    };
    // Calls run on the writer, outside logger locks. Views expire when Write returns.
    // Sinks must not wait on producers or on application locks held by a producer.
    class LogSink
    {
      public:

        virtual ~LogSink() = default;
        virtual bool NeedsText() const noexcept { return true; }
        // Raw sinks request metadata here; text patterns contribute their own requirements.
        virtual LogFieldFlags RequiredFields() const noexcept { return LogFields::None; }
        virtual bool Write(const LogDelivery& delivery) = 0;
        virtual bool Flush()                            = 0;
    };
}  // namespace LLUtils
