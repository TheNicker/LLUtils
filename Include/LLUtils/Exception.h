/*
Copyright (c) 2019 Lior Lahav

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#include "Event.h"
#include "PlatformUtility.h"
#include "StringUtility.h"

#include <array>
#include <atomic>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <memory>
#include <source_location>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace LLUtils
{
    class Exception : public std::exception
    {
      public:

        enum class ErrorCode
        {
            Unspecified,
            Unknown,
            CorruptedValue,
            LogicError,
            RuntimeError,
            DuplicateItem,
            BadParameters,
            NotImplemented,
            InvalidState,
            SystemError,
            NotFound,
            Count
        };
        enum class Mode
        {
            Exception,
            Error
        };

        // Text is UTF-8. A cause owns the original exception, including foreign dynamic types.
        struct EventArgs
        {
            ErrorCode errorCode{};
            std::string description;
            std::string systemErrorMessage;
            PlatformUtility::StackTrace stackTrace;
            std::string functionName;
            Mode exceptionmode{};
            std::source_location source{};
            std::error_code systemError;
            std::exception_ptr cause;
        };

        // Event supplies synchronized registration, snapshots, and subscription ownership.
        // Exception observation adds only failure containment and recursion suppression.
        class OnExceptionEventType : private Event<void(const EventArgs&), true>
        {
            using Base = Event<void(const EventArgs&), true>;

          public:

            using Base::Func;
            using Base::Subscribe;
            using Base::Subscription;

            void Raise(const EventArgs& args) noexcept
            {
                if (sNotifying)
                    return;
                struct NotificationScope
                {
                    NotificationScope() { sNotifying = true; }
                    ~NotificationScope() { sNotifying = false; }
                } scope;
                try
                {
                    VisitListeners(
                        [&](const Func& callback) noexcept
                        {
                            try
                            {
                                callback(args);
                            }
                            catch (...)
                            { /* Diagnostics must not replace the original failure. */
                            }
                            return true;
                        });
                }
                catch (...)
                { /* Snapshot allocation is optional diagnostic work as well. */
                }
            }

          private:

            static inline thread_local bool sNotifying = false;
        };

        static OnExceptionEventType OnException;
        static inline std::atomic_bool sThrowErrorsInDebug{true};
        static void SetThrowErrorsInDebug(bool shouldThrow) noexcept
        {
            sThrowErrorsInDebug.store(shouldThrow, std::memory_order_relaxed);
        }

        Exception(ErrorCode errorCode, std::string function, std::string description, bool systemError,
                  Mode exceptionMode, int callStackLevel = 2,
                  std::source_location source = std::source_location::current())
            : Exception(errorCode, std::move(function), std::move(description),
                        systemError ? LastSystemError() : std::error_code{}, exceptionMode, callStackLevel, source, {})
        {
        }

        Exception(const Exception&) noexcept            = default;
        Exception& operator=(const Exception&) noexcept = default;
        // Moving preserves the shared snapshot in both objects. Accessors therefore never
        // need to initialize an allocating empty diagnostic, even under memory pressure.
        Exception(Exception&& other) noexcept : Exception(static_cast<const Exception&>(other)) {}
        Exception& operator=(Exception&& other) noexcept { return *this = static_cast<const Exception&>(other); }

        [[nodiscard]] const EventArgs& GetDetails() const noexcept { return *fDetails; }
        [[nodiscard]] const char* what() const noexcept override { return GetDetails().description.c_str(); }

        [[nodiscard]] static Exception FromSystemError(std::error_code code, std::string description,
                                                       std::source_location source = std::source_location::current())
        {
            return Exception(ErrorCode::SystemError, source.function_name(), std::move(description), code,
                             Mode::Exception, 2, source, {});
        }

        [[noreturn]] static void Rethrow(ErrorCode code, std::string description,
                                         std::source_location source = std::source_location::current())
        {
            auto cause = std::current_exception();
            if (!cause)
                throw Exception(ErrorCode::InvalidState, source.function_name(),
                                "Exception::Rethrow requires an active exception", {}, Mode::Exception, 2, source, {});
            throw Exception(code, source.function_name(), std::move(description), {}, Mode::Exception, 2, source,
                            std::move(cause));
        }

        [[nodiscard]] static constexpr std::string_view ExceptionErrorCodeToString(ErrorCode errorCode) noexcept
        {
            constexpr std::array names{std::string_view("Unspecified"),     std::string_view("Unknown"),
                                       std::string_view("Corrupted value"), std::string_view("Logic error"),
                                       std::string_view("Runtime error"),   std::string_view("Duplicate item"),
                                       std::string_view("Bad parameters"),  std::string_view("Not implemented"),
                                       std::string_view("Invalid state"),   std::string_view("System error"),
                                       std::string_view("Not found")};
            static_assert(names.size() == static_cast<std::size_t>(ErrorCode::Count));
            const auto index = static_cast<std::size_t>(errorCode);
            return index < names.size() ? names[index] : names.front();
        }

        static native_string_type FormatStackTrace(const PlatformUtility::StackTrace& stackTrace,
                                                   uint16_t maxDepth = std::numeric_limits<uint16_t>::max());

      private:

        static std::error_code LastSystemError() noexcept
        {
#if LLUTILS_PLATFORM == LLUTILS_PLATFORM_WIN32
            return {static_cast<int>(::GetLastError()), std::system_category()};
#else
            return {errno, std::generic_category()};
#endif
        }

        Exception(ErrorCode errorCode, std::string function, std::string description, std::error_code systemError,
                  Mode exceptionMode, int callStackLevel, std::source_location source, std::exception_ptr cause)
        {
            auto details = std::make_shared<EventArgs>(EventArgs{
                .errorCode     = errorCode,
                .description   = std::move(description),
                .functionName  = std::move(function),
                .exceptionmode = exceptionMode,
                .source        = source,
                .systemError   = systemError,
                .cause         = std::move(cause),
            });
            // Once the basic error exists, optional enrichment must not replace it.
            try
            {
                if (systemError)
                {
#if LLUTILS_PLATFORM == LLUTILS_PLATFORM_WIN32
                    if (systemError.category() == std::system_category())
                    {
                        wchar_t message[1024]{};
                        const auto count = FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                                          nullptr, static_cast<DWORD>(systemError.value()), 0, message,
                                                          static_cast<DWORD>(std::size(message)), nullptr);
                        details->systemErrorMessage = StringUtility::ToAString(std::wstring_view(message, count));
                    }
                    else
#endif
                        details->systemErrorMessage = systemError.message();
                }
            }
            catch (...)
            {
            }
            try
            {
                details->stackTrace = PlatformUtility::GetCallStack(callStackLevel);
            }
            catch (...)
            {
            }
            fDetails = std::move(details);
            OnException.Raise(*fDetails);
        }

        std::shared_ptr<const EventArgs> fDetails;
    };

    inline Exception::OnExceptionEventType Exception::OnException;
}  // namespace LLUtils

#define LL_EXCEPTION(ERROR_CODE, DESCRIPTION)                                                                          \
    (throw ::LLUtils::Exception((ERROR_CODE), __FUNCTION__, (DESCRIPTION), false,                                      \
                                ::LLUtils::Exception::Mode::Exception))

// Capture the OS error before evaluating DESCRIPTION. Passing source_location into the
// lambda keeps the user's call site rather than the implementation lambda's function name.
#if LLUTILS_PLATFORM == LLUTILS_PLATFORM_WIN32
    #define LL_EXCEPTION_SYSTEM_ERROR(DESCRIPTION)                                                                     \
        (throw[&](std::source_location source) {                                                                       \
            const auto value = ::GetLastError();                                                                       \
            const std::error_code code(static_cast<int>(value), std::system_category());                               \
            return ::LLUtils::Exception::FromSystemError(code, (DESCRIPTION), source);                                 \
        }(std::source_location::current()))
#else
    #define LL_EXCEPTION_SYSTEM_ERROR(DESCRIPTION)                                                                     \
        (throw[&](std::source_location source) {                                                                       \
            const int value = errno;                                                                                   \
            const std::error_code code(value, std::generic_category());                                                \
            return ::LLUtils::Exception::FromSystemError(code, (DESCRIPTION), source);                                 \
        }(std::source_location::current()))
#endif
#define LL_EXCEPTION_UNEXPECTED_VALUE                                                                                  \
    LL_EXCEPTION(::LLUtils::Exception::ErrorCode::RuntimeError, "Unexpected or corrupted value")
#define LL_EXCEPTION_NOT_IMPLEMENT(WHAT) LL_EXCEPTION(::LLUtils::Exception::ErrorCode::NotImplemented, (WHAT))
// Construction/notification reports the error; optional diagnostics are best-effort.
// Core message allocation can still throw, so noexcept recovery boundaries must contain it.
#define LL_EXCEPTION_DONT_THROW(ERROR_CODE, DESCRIPTION)                                                               \
    (::LLUtils::Exception((ERROR_CODE), __FUNCTION__, (DESCRIPTION), false, ::LLUtils::Exception::Mode::Error))
#ifdef _DEBUG
    #define LL_ERROR(ERROR_CODE, DESCRIPTION)                                                                          \
        do                                                                                                             \
        {                                                                                                              \
            if (::LLUtils::Exception::sThrowErrorsInDebug.load(std::memory_order_relaxed))                             \
                LL_EXCEPTION((ERROR_CODE), (DESCRIPTION));                                                             \
            else                                                                                                       \
                LL_EXCEPTION_DONT_THROW((ERROR_CODE), (DESCRIPTION));                                                  \
        } while (false)
#else
    #define LL_ERROR LL_EXCEPTION_DONT_THROW
#endif

// Preserve the legacy static stack formatter for clients that include only Exception.h.
#include "ExceptionFormatter.h"
