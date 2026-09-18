#pragma once
#include "Exception.h"

#include <algorithm>
#include <charconv>
#include <concepts>
#include <stdexcept>
#include <iterator>
#include <tuple>

namespace LLUtils
{
    namespace ExceptionFormatting
    {
        inline constexpr std::size_t MaxReportBytes = 16 * 1024;
        inline constexpr std::size_t MaxCauseDepth  = 16;
        inline constexpr std::size_t MaxStackDepth  = 64;

        // All fields are bounded before conversion/appending, including foreign what() pointers.
        // The reserved suffix makes truncation explicit without exceeding the total UTF-8 budget.
        class Report
        {
          public:

            void Append(std::string_view text)
            {
                if (fTruncated)
                    return;
                const auto remaining = MaxReportBytes - Truncation.size() - fText.size();
                auto count           = (std::min) (remaining, text.size());
                const bool truncated = count < text.size();
                if (truncated)
                {
                    while (count != 0 && (static_cast<unsigned char>(text[count]) & 0xc0) == 0x80)
                        --count;
                }
                const auto prefix = text.substr(0, count);
                try
                {
                    // Conversion validates UTF-8. Keep the original bytes after validation.
                    std::ignore = StringUtility::ToWString(prefix);
                    // Embedded NULs must not hide the rest of a dialog's diagnostic.
                    for (const char value : prefix)
                        fText.push_back(value == '\0' ? '?' : value);
                }
                catch (const std::invalid_argument&)
                {
                    constexpr std::string_view invalid = "[invalid text]";
                    fText.append(invalid.substr(0, remaining));
                }
                fTruncated = truncated;
            }

            void Append(const char* text)
            {
                if (text != nullptr && !fTruncated)
                {
                    const auto limit   = MaxReportBytes - Truncation.size() - fText.size() + 1;
                    std::size_t length = 0;
                    while (length < limit && text[length] != '\0')
                        ++length;
                    Append(std::string_view(text, length));
                }
            }

            void Native(const native_string_type& text)
            {
#if LLUTILS_PLATFORM == LLUTILS_PLATFORM_WIN32
                if (fTruncated)
                    return;
                auto count = (std::min) (text.size(), MaxReportBytes);
                if (count < text.size() && count != 0 && text[count - 1] >= 0xd800 && text[count - 1] <= 0xdbff)
                    --count;
                try
                {
                    Append(StringUtility::ToAString(std::wstring_view(text.data(), count)));
                }
                catch (const std::invalid_argument&)
                {
                    Append("[invalid path]");
                }
                fTruncated = fTruncated || count < text.size();
#else
                Append(std::string_view(text));
#endif
            }

            template <std::integral T>
            void Number(T value, int base = 10)
            {
                char buffer[32];
                const auto result = std::to_chars(std::begin(buffer), std::end(buffer), value, base);
                if (result.ec == std::errc{})
                    Append(std::string_view(buffer, static_cast<std::size_t>(result.ptr - buffer)));
            }

            void Stack(const PlatformUtility::StackTrace& stack, std::size_t maxDepth)
            {
                const auto count = (std::min) ({stack.size(), maxDepth, MaxStackDepth});
                for (std::size_t index = 0; index < count && !fTruncated; ++index)
                {
                    const auto& frame = stack[index];
                    Native(frame.moduleName);
                    Append("!");
                    Native(frame.name);
                    Append(" at 0x");
                    Number(frame.address, 16);
                    if (!frame.sourceFileName.empty())
                    {
                        Append(" at ");
                        Native(frame.sourceFileName);
                        Append(":");
                        Number(frame.line);
                    }
                    Append("\n");
                }
                if (count < stack.size() && maxDepth != 0)
                    Append("[stack truncated]\n");
            }

            void Details(const Exception::EventArgs& details)
            {
                Append(Exception::ExceptionErrorCodeToString(details.errorCode));
                Append(": ");
                Append(std::string_view(details.description));
                Append("\nSource: ");
                Append(details.source.line() != 0 ? details.source.function_name() : details.functionName.c_str());
                if (details.source.line() != 0)
                {
                    Append(" at ");
                    Append(details.source.file_name());
                    Append(":");
                    Number(details.source.line());
                }
                Append("\n");
                if (details.systemError || details.errorCode == Exception::ErrorCode::SystemError)
                {
                    SystemCode(details.systemError);
                    Append(std::string_view(details.systemErrorMessage));
                    Append("\n");
                }
                if (details.stackTrace.empty())
                    Append("Stack: unavailable\n");
                else
                {
                    Append("Stack:\n");
                    Stack(details.stackTrace, details.exceptionmode == Exception::Mode::Error ? 3 : MaxStackDepth);
                }
            }

            void Chain(std::exception_ptr exception, std::size_t depth = 0)
            {
                if (!exception && depth == 0)
                    Append("Termination without an active exception\n");
                while (exception && depth < MaxCauseDepth && !fTruncated)
                {
                    if (depth != 0)
                        Append("\nCaused by:\n");
                    std::exception_ptr cause;
                    try
                    {
                        std::rethrow_exception(exception);
                    }
                    catch (const Exception& error)
                    {
                        Details(error.GetDetails());
                        cause = error.GetDetails().cause;
                        if (!cause)
                            if (const auto* nested = dynamic_cast<const std::nested_exception*>(&error))
                                cause = nested->nested_ptr();
                    }
                    catch (const std::exception& error)
                    {
                        Append("Standard exception: ");
                        Append(error.what());
                        Append("\n");
                        if (const auto* system = dynamic_cast<const std::system_error*>(&error))
                        {
                            SystemCode(system->code());
                            Append("\n");
                        }
                        if (const auto* nested = dynamic_cast<const std::nested_exception*>(&error))
                            cause = nested->nested_ptr();
                    }
                    catch (const std::nested_exception& error)
                    {
                        Append("Unknown/non-standard exception\n");
                        cause = error.nested_ptr();
                    }
                    catch (...)
                    {
                        Append("Unknown/non-standard exception\n");
                    }
                    exception = std::move(cause);
                    ++depth;
                }
                if (exception && !fTruncated)
                    Append("[cause depth limit reached]\n");
            }

            std::string Take()
            {
                if (fTruncated)
                    fText.append(Truncation);
                return std::move(fText);
            }

          private:

            void SystemCode(const std::error_code& code)
            {
                Append("System error ");
                Append(code.category().name());
                Append(":");
                Number(code.value());
                Append(" ");
            }
            static constexpr std::string_view Truncation = "\n[report truncated]\n";
            // The literal constructor is potentially throwing, including MSVC debug
            // iterator-proxy allocation. A noexcept default constructor would terminate.
            std::string fText{""};
            bool fTruncated = false;
        };
    }  // namespace ExceptionFormatting

    // These functions may allocate. A noexcept reporter must send a static fallback directly
    // to its output sink if formatting fails, without trying to allocate another report.
    inline std::string FormatException(const Exception::EventArgs& details)
    {
        ExceptionFormatting::Report report;
        report.Details(details);
        report.Chain(details.cause, 1);
        return report.Take();
    }

    inline std::string FormatException(std::exception_ptr exception)
    {
        ExceptionFormatting::Report report;
        report.Chain(std::move(exception));
        return report.Take();
    }

    inline native_string_type Exception::FormatStackTrace(const PlatformUtility::StackTrace& stackTrace,
                                                          uint16_t maxDepth)
    {
        ExceptionFormatting::Report report;
        report.Stack(stackTrace, maxDepth);
        return StringUtility::ToNativeString(report.Take());
    }
}  // namespace LLUtils
