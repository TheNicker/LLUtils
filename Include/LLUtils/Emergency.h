#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include "DepthScope.h"
#include "UnicodeCodec.h"
#ifdef _WIN32
    #include <windows.h>
#else
    #include <unistd.h>
#endif

namespace LLUtils::EmergencyDetail
{
    // Emergency reporting that cannot allocate, take a lock, or depend on any logger state. It must stay
    // usable from a faulting thread, from a sink callback and from a late static destructor, so this
    // header deliberately avoids <filesystem> and every other heavyweight dependency.
    //
    // This is last-resort diagnostic output, not logging. It is shared by the logging pipeline, the
    // writer, the file sinks and the exception layer, which is why it lives outside Logging/ and why
    // nothing here may name a logging type.
    class Emergency
    {
      private:

        // Declared ahead of Scope because a member initializer is not a function body, and the
        // enclosing class is not complete inside a nested class definition. The nesting is what keeps
        // the counter private: no name outside this class can reach it.
        static inline thread_local unsigned sDepth = 0;  // Nesting of diagnostic work on this thread.

      public:

        static constexpr std::size_t MaxMessageBytes         = 1024;
        static constexpr std::string_view RecursiveMessage   = "Recursive logging bypassed\n";
        static constexpr std::string_view ExceptionMessage   = "Exception inside logging\n";
        static constexpr std::string_view PreparationMessage = "Logging call preparation failed\n";
        // Shared diagnostic-origin scope for producers, the writer and reporting boundaries; restores
        // nesting during unwinding. A nested type rather than an alias because it is constructed from
        // outside this class, and that is also what keeps the counter private.
        class Scope
        {
          public:

            Scope() noexcept : fGuard(sDepth) {}
            Scope(const Scope&)            = delete;
            Scope& operator=(const Scope&) = delete;

          private:

            DepthScope<unsigned> fGuard;
        };
        // True while a diagnostic pipeline runs on this thread: a producer call, the writer, a sink
        // callback, or any other reporting boundary. The condition is deliberately not logging-specific,
        // because the exception layer is an independent caller that must not be made to name a logger.
        [[nodiscard]] static bool Active() noexcept { return sDepth != 0; }
        // Appends the notice to the private descriptor, then to standard error and the debugger. Reentrant
        // reporting is dropped: a failure while reporting a failure cannot be reported any further.
        static void Write(std::string_view text) noexcept
        {
            if (sWriting)
                return;
            sWriting = true;
            text     = text.substr(0, MaxMessageBytes);  // Every destination below sees the capped notice.
            WriteTo(sHandle.load(std::memory_order_acquire), text);
#ifdef _WIN32
            // A real console needs UTF-16 regardless of its configured code page. Redirected stderr
            // remains UTF-8 bytes. Fixed storage keeps both paths available during native faults.
            //
            // The bound holds by construction rather than by data: UTF-16 units never exceed UTF-8
            // bytes, so MaxMessageBytes bytes fit, and Encode emits a surrogate pair without bounds
            // checking, so the guard below admits a call only with two free slots. Each Encode then
            // writes at indices at most MaxMessageBytes - 1 and MaxMessageBytes, leaving written at
            // most MaxMessageBytes + 1 for the terminator inside the MaxMessageBytes + 2 array.
            wchar_t wide[MaxMessageBytes + 2]{};
            const auto count = ToWide(text, wide);
            DWORD mode{}, written{};
            const auto error = GetStdHandle(STD_ERROR_HANDLE);
            if (error && error != INVALID_HANDLE_VALUE)
            {
                if (count > 0 && GetConsoleMode(error, &mode))
                    WriteConsoleW(error, wide, static_cast<DWORD>(count), &written, nullptr);
                else
                    WriteFile(error, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
            }
            OutputDebugStringW(wide);
#else
            const auto ignored = ::write(STDERR_FILENO, text.data(), text.size());
            (void) ignored;
#endif
            sWriting = false;
        }
        // Zero means no descriptor is available. Callers check before creating a file, because creating one
        // has side effects the check exists to avoid. Adopt at most once, from application startup.
        [[nodiscard]] static bool HasDescriptor() noexcept
        {
            return sHandle.load(std::memory_order_relaxed) != 0;
        }
        static void AdoptDescriptor(std::uintptr_t descriptor) noexcept
        {
            sHandle.store(descriptor, std::memory_order_release);
        }

      private:

#ifdef _WIN32
        // Allocation-free UTF-8 to UTF-16 for the crash path, using the same codec as the sinks.
        // An invalid sequence becomes U+FFFD, matching the substitution MultiByteToWideChar
        // performed, so a malformed notice still reports instead of losing the notice entirely.
        // TranscodeToWide owns the bounds arithmetic that keeps the surrogate pair inside the
        // array and leaves room for the terminator.
        static std::size_t ToWide(std::string_view text, wchar_t* output) noexcept
        {
            return UnicodeDetail::TranscodeToWide(text, output, MaxMessageBytes + 2);
        }
#endif
        // Appends to one already truncated descriptor. Zero means no descriptor is available.
        static void WriteTo(std::uintptr_t encoded, std::string_view text) noexcept
        {
            if (!encoded)
                return;
#ifdef _WIN32
            DWORD written{};
            WriteFile(reinterpret_cast<HANDLE>(encoded), text.data(), static_cast<DWORD>(text.size()), &written,
                      nullptr);
#else
            const auto ignored = ::write(static_cast<int>(encoded) - 1, text.data(), text.size());
            (void) ignored;
#endif
        }
        // Zero means unopened; Windows stores the HANDLE, POSIX stores the descriptor plus one. Adopted once
        // from application startup and never closed, so a fault cannot race a close against a reporting
        // thread.
        static inline std::atomic<std::uintptr_t> sHandle{0};
        static inline thread_local bool sWriting = false;  // Stops recursive ordinary emergency notices.
    };
}  // namespace LLUtils::EmergencyDetail