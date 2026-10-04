#pragma once
#include "LogOptions.h"
#include "../Emergency.h"
#include "../FileHandle.h"
#include "../FileSystemHelper.h"
#include "../ScopedFileLock.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <ranges>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
#ifdef _WIN32
    #include <windows.h>
#else
    #include <unistd.h>
#endif

namespace LLUtils
{
    // Buffered private session segments; writer callbacks own rotation, flushing and retention cleanup.
    class FileLogSink final : public LogSink
    {
      public:

        explicit FileLogSink(LogFileOptions options) : fOptions(std::move(options))
        {
            if (fOptions.path.empty() || fOptions.rotationBytes == 0)
                throw std::invalid_argument("Invalid log file settings");
            fOptions.path = std::filesystem::absolute(fOptions.path);
            FileSystemHelper::EnsureDirectory(fOptions.path.native());
#ifdef _WIN32
            const auto pid = GetCurrentProcessId();
#else
            const auto pid = getpid();
#endif
            fPrefix  = fOptions.path.filename().native() + std::filesystem::path(".").native();
            fSession = fPrefix + std::filesystem::path(
                                     std::to_string(pid) + "." +
                                     std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".")
                                     .native();
            OpenSegment();
            Cleanup();
        }
        bool Write(const LogDelivery& delivery) override
        {
            // Each complete record is UTF-8 with CRLF, including multiline payloads.
            std::string text;
            text.reserve(delivery.text.size() + RecordTerminator.size());
            for (std::size_t i = 0; i < delivery.text.size(); ++i)
            {
                if (delivery.text[i] == '\r')
                {
                    if (i + 1 < delivery.text.size() && delivery.text[i + 1] == '\n')
                        ++i;
                    text += RecordTerminator;
                }
                else if (delivery.text[i] == '\n')
                    text += RecordTerminator;
                else
                    text += delivery.text[i];
            }
            text += RecordTerminator;
            if (fBytes != 0 && (fBytes >= fOptions.rotationBytes || text.size() > fOptions.rotationBytes - fBytes))
            {
                if (!fFile.Close())
                    return false;
                OpenSegment();
                Cleanup();
            }
            const bool written = fFile.Write(std::as_bytes(std::span{text.data(), text.size()}));
            if (written)
                fBytes += text.size();
            return written;
        }
        bool Flush() override { return fFile.Flush(); }

      private:

        static constexpr std::string_view RecordTerminator = "\r\n";
        static constexpr unsigned SegmentIdentityPartCount = 3;
#ifdef _WIN32
        static constexpr std::wstring_view SegmentExtension    = L".log";
        static constexpr std::wstring_view RetentionLockSuffix = L".retention-lock";
#else
        static constexpr std::string_view SegmentExtension    = ".log";
        static constexpr std::string_view RetentionLockSuffix = ".retention-lock";
#endif

        void OpenSegment()
        {
            const auto name = fSession + std::filesystem::path(std::to_string(++fSegment)).native() +
                              std::filesystem::path(SegmentExtension).native();
            const auto path = fOptions.path.parent_path() / name;
#ifdef _WIN32
            fFile = FileHandle::OpenAppend(path, FileHandle::Creation::CreateNew,
                                           FileHandle::Protection::PreventRemoval);
#else
            // POSIX open publishes a name before flock protects it. Retention ignores this temporary
            // suffix, and atomic rename publishes the final segment only after its shared lock is held.
            const auto openingPath = path.native() + ".opening";
            auto file              = FileHandle::OpenAppend(openingPath, FileHandle::Creation::CreateNew,
                                                            FileHandle::Protection::PreventRemoval);
            try
            {
                std::filesystem::rename(openingPath, path);
            }
            catch (...)
            {
                std::error_code ignored;
                std::filesystem::remove(openingPath, ignored);
                throw;
            }
            fFile = std::move(file);
#endif
            fBytes = 0;
        }
        bool IsSegment(const std::filesystem::path& path) const
        {
            const auto name = path.filename().native();
            if (name.size() <= fPrefix.size() + SegmentExtension.size() || !name.starts_with(fPrefix) ||
                !name.ends_with(SegmentExtension))
                return false;
            const std::basic_string_view identity(name.data() + fPrefix.size(),
                                                  name.size() - fPrefix.size() - SegmentExtension.size());
            unsigned parts = 0;
            for (const auto token : identity | std::views::split(static_cast<std::filesystem::path::value_type>('.')))
            {
                if (std::ranges::empty(token) ||
                    !std::ranges::all_of(token, [](auto ch) { return ch >= '0' && ch <= '9'; }))
                    return false;
                ++parts;
            }
            return parts == SegmentIdentityPartCount;
        }
        void Cleanup() noexcept
        {
            // Cleanup failure never disables output. Retention is a target: persistent failure can grow disk use.
            // All processes use this lock while enumerating/deleting closed segments; active segments are protected.
            try
            {
                const auto path = fOptions.path.native() + std::filesystem::path(RetentionLockSuffix).native();
                const ScopedFileLock lock(path);
                std::vector<std::filesystem::directory_entry> closed;
                for (const auto& entry : std::filesystem::directory_iterator(fOptions.path.parent_path()))
                    if (entry.is_regular_file() && IsSegment(entry.path()) && FileHandle::CanRemove(entry.path()))
                        closed.push_back(entry);
                std::ranges::sort(closed, {}, [](const auto& entry) { return entry.last_write_time(); });
                for (std::size_t i = 0; i < closed.size() && closed.size() - i > fOptions.closedFiles; ++i)
                    std::filesystem::remove(closed[i].path());
            }
            catch (...)
            {
                EmergencyDetail::Emergency::Write("Logging retention cleanup failed; output continues\n");
            }
        }
        LogFileOptions fOptions;                      // Validated settings with an absolute base path.
        std::filesystem::path::string_type fPrefix;   // Base filename plus separator, used for retention matching.
        std::filesystem::path::string_type fSession;  // Prefix plus process/session identity for new segments.
        FileHandle fFile;                             // Current private segment, explicitly closed on rotation.
        std::size_t fBytes = 0;                       // Current segment bytes including record terminators.
        unsigned fSegment  = 0;                       // Next segment ordinal within this session.
    };
}  // namespace LLUtils
