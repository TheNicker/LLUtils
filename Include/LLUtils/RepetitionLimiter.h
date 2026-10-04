#pragma once
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <utility>

namespace LLUtils
{
    // Thread-safe repetition gate for a single call site. It answers one question per invocation: may this
    // call be emitted, and if so how many eligible calls were dropped since the previous emission?
    //
    // Two policies exist and are mutually exclusive. A zero interval selects count gating, which admits one
    // call per callsPerEmission eligible invocations. A nonzero interval selects time gating, which admits the
    // first call of each generation and then one per interval. Time is supplied by the caller against a
    // monotonic clock, so this class performs no clock reads of its own.
    //
    // A generation is an opaque identity supplied by the caller. Presenting a different generation resets the
    // gate, so a new session or scope starts from a clean count instead of inheriting the previous backlog.
    // Generation zero is valid, so a separate flag distinguishes "never used" from "used with generation zero".
    //
    // Admitted calls carry the number of eligible calls skipped since the previous admission, aggregated
    // across threads and saturating at UINT64_MAX rather than wrapping. That count lets a caller report what
    // it lost instead of losing it silently; zero means this emission follows directly after the previous one.
    class RepetitionLimiter
    {
      public:

        // One admitted emission per callsPerEmission eligible invocations. Both arguments must be nonzero,
        // because an interval of zero selects count gating and a count of zero could never admit a call.
        RepetitionLimiter(std::uint64_t callsPerEmission,
                          std::chrono::steady_clock::duration minimumInterval = {})
            : fCallsPerEmission(callsPerEmission), fMinimumInterval(minimumInterval)
        {
            if (!callsPerEmission || minimumInterval < std::chrono::steady_clock::duration{})
                throw std::invalid_argument("Invalid repetition policy");
        }

        // Decides whether the current call is admitted:
        //   std::nullopt  the call is skipped and must not be emitted
        //   a value       the call is admitted, and the value is how many eligible calls were skipped since
        //                 the previous admission, which is zero for a back-to-back emission
        // Callers pass monotonic time; count-gated callers may pass a default-constructed time point.
        std::optional<std::uint64_t> Admit(std::uint64_t generation,
                                           std::chrono::steady_clock::time_point now)
        {
            const std::lock_guard lock(fMutex);

            // A new generation starts with no skipped backlog and no cooldown, so its first call is admitted.
            const bool isFirstInGeneration = !fHasGeneration || fGeneration != generation;
            if (isFirstInGeneration)
            {
                fHasGeneration           = true;
                fGeneration              = generation;
                fCallsUntilNextAdmission = 0;
                fSkippedCalls            = 0;
                fNextEligibleTime        = {};
            }

            // Time gating admits the first call of a generation regardless of the cooldown and then relies
            // on the deadline alone. Count gating admits whenever the countdown has run out.
            const bool admit = fMinimumInterval != std::chrono::steady_clock::duration{} ? isFirstInGeneration || now >= fNextEligibleTime
                                                                                       : fCallsUntilNextAdmission == 0;
            if (!admit)
            {
                // Saturate instead of wrapping: a wrapped counter would understate the backlog.
                if (fSkippedCalls != UINT64_MAX)
                    ++fSkippedCalls;
                // Only the count policy keeps a countdown; the time policy is deadline-driven.
                if (fMinimumInterval == std::chrono::steady_clock::duration{})
                    --fCallsUntilNextAdmission;
                return {};
            }

            // Hand the accumulated backlog to this emission and start a fresh one.
            const auto skippedCalls   = std::exchange(fSkippedCalls, 0);
            fCallsUntilNextAdmission = fCallsPerEmission - 1;
            fNextEligibleTime        = now + fMinimumInterval;
            return skippedCalls;
        }

      private:

        const std::uint64_t fCallsPerEmission;  // Count policy: eligible invocations allowed per emission.
        const std::chrono::steady_clock::duration fMinimumInterval;  // Nonzero selects time gating and replaces counting.
        std::mutex fMutex;                                  // Protects all mutable state below across callers.
        std::uint64_t fGeneration = 0;                      // Most recent generation presented by a caller.
        std::uint64_t fCallsUntilNextAdmission = 0;         // Count policy countdown to the next admission.
        std::uint64_t fSkippedCalls = 0;                    // Eligible calls dropped since the previous admission.
        bool fHasGeneration = false;                        // Separates "never used" from generation zero.
        std::chrono::steady_clock::time_point fNextEligibleTime{};  // Earliest time-policy admission.
    };
}  // namespace LLUtils