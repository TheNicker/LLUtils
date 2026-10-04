#pragma once
#include <atomic>
#include <compare>
#include <cstdint>

namespace LLUtils
{
    // An ID is ordinary request data. Scopes belong only to synchronous execution segments.
    struct OperationId
    {
        // Zero means no operation; Create assigns process-wide IDs independently of logger sessions.
        std::uint64_t value                        = 0;
        auto operator<=>(const OperationId&) const = default;
        static OperationId Create() noexcept { return {sNext.fetch_add(1, std::memory_order_relaxed)}; }

      private:

        // Allocation only needs uniqueness, not synchronization of the request data itself.
        static inline constinit std::atomic<std::uint64_t> sNext{1};
    };

    // Installs a thread-local operation for one synchronous segment; never span coroutine suspension.
    class OperationScope
    {
      public:

        explicit OperationScope(OperationId id) noexcept : fPrevious(sCurrent) { sCurrent = id; }
        ~OperationScope() { sCurrent = fPrevious; }
        OperationScope(const OperationScope&)            = delete;
        OperationScope& operator=(const OperationScope&) = delete;
        static OperationId Current() noexcept { return sCurrent; }

      private:

        OperationId fPrevious;  // Restored on destruction, allowing nested synchronous scopes.
        static inline thread_local OperationId sCurrent{};  // Each worker explicitly installs its request ID.
    };
}  // namespace LLUtils
