#pragma once
#include <type_traits>

namespace LLUtils
{
    // Depth counter guard over caller-supplied storage. The storage owns the count rather than this
    // type, so one instantiation serves any number of independent counters. A counter held by the type
    // itself would alias unrelated uses, because every depth counter in the library has the same type
    // and a using alias cannot give an instantiation its own identity.
    //
    // Non-copyable, because a copy would decrement a count it never incremented.
    template <class Counter>
    class DepthScope
    {
      public:

        static_assert(std::is_integral_v<Counter>, "DepthScope requires an integral counter");

        explicit DepthScope(Counter& storage) noexcept : fStorage(&storage) { ++*fStorage; }
        ~DepthScope() noexcept { --*fStorage; }
        DepthScope(const DepthScope&)            = delete;
        DepthScope& operator=(const DepthScope&) = delete;

      private:

        Counter* fStorage;
    };
}  // namespace LLUtils
