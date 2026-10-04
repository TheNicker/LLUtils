#pragma once
#include "LogRecord.h"
#include "../StringUtility.h"
#include <cstdio>
#include <string>
#include <string_view>
#ifdef _WIN32
    #include <windows.h>
#endif

namespace LLUtils
{
    namespace LoggingDetail
    {
        // Developer output is line oriented but not a durable format, so both sinks below emit one LF per
        // delivery and leave embedded newlines as sent. The file sink differs deliberately: it normalizes to
        // canonical CRLF so a multiline payload cannot introduce a bare LF into a stored record.
        inline std::string AsLine(std::string_view text)
        {
            std::string line(text);
            line += '\n';
            return line;
        }
    }  // namespace LoggingDetail

    // Writer-serialized stderr output; Windows consoles receive Unicode, redirected streams receive UTF-8.
    class ConsoleLogSink final : public LogSink
    {
      public:

        bool Write(const LogDelivery& delivery) override
        {
            const auto line = LoggingDetail::AsLine(delivery.text);
#ifdef _WIN32
            const auto handle = GetStdHandle(STD_ERROR_HANDLE);
            DWORD mode{}, written{};
            if (GetConsoleMode(handle, &mode))
            {
                const auto wide = StringUtility::ToWString(line);
                return WriteConsoleW(handle, wide.data(), static_cast<DWORD>(wide.size()), &written, nullptr) &&
                       written == wide.size();
            }
#endif
            return std::fwrite(line.data(), 1, line.size(), stderr) == line.size();
        }
        bool Flush() override { return std::fflush(stderr) == 0; }
    };

    // Windows debugger output; intentionally succeeds without output on other platforms.
    class DebugLogSink final : public LogSink
    {
      public:

        bool Write(const LogDelivery& delivery) override
        {
#ifdef _WIN32
            OutputDebugStringW(StringUtility::ToWString(LoggingDetail::AsLine(delivery.text)).c_str());
#else
            (void) delivery;
#endif
            return true;
        }
        bool Flush() override { return true; }
    };
}  // namespace LLUtils