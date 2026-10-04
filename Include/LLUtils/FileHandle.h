#pragma once
#include "NativeFile.h"
#include <memory>
#include <span>

namespace LLUtils
{
    // Owned buffered binary append file, complementing File's whole-file helpers.
    // No internal synchronization. All native descriptors are non-inheritable.
    class FileHandle
    {
      public:

        // Reuse the native descriptor vocabulary rather than restating it.
        using Creation   = NativeFile::Disposition;
        using Protection = NativeFile::Protection;
        static FileHandle OpenAppend(const std::filesystem::path& path, Creation creation = Creation::OpenOrCreate,
                                     Protection protection = Protection::None)
        {
            auto native = NativeFile::Open(path, NativeFile::Access::Write, creation, protection);
            FileHandle result;
            result.fFile.reset(native.ReleaseAsStream());
            return result;
        }
        bool Write(std::span<const std::byte> bytes) noexcept
        {
            return fFile && (bytes.empty() || std::fwrite(bytes.data(), 1, bytes.size(), fFile.get()) == bytes.size());
        }
        // Flush drains CRT buffering; it does not guarantee durable storage after a power failure.
        bool Flush() noexcept { return fFile && std::fflush(fFile.get()) == 0; }
        // Relinquishes ownership even on failure. Explicitly close before replacement if errors matter;
        // destruction and move assignment perform best-effort cleanup through the owning deleter.
        bool Close() noexcept
        {
            auto* stream = fFile.release();
            return !stream || std::fclose(stream) == 0;
        }
        // Availability probe only, not an atomic delete reservation. Caller coordinates retention separately.
        static bool CanRemove(const std::filesystem::path& path)
        {
#ifdef _WIN32
            // Windows denies DELETE access while another handle withholds delete sharing.
            try
            {
                const NativeFile probe = NativeFile::Open(path, NativeFile::Access::Delete,
                                                          NativeFile::Disposition::OpenExisting);
                return true;
            }
            catch (const std::system_error& error)
            {
                if (error.code().value() == ERROR_SHARING_VIOLATION || error.code().value() == ERROR_FILE_NOT_FOUND)
                    return false;
                throw;
            }
#else
            // POSIX protection is a shared advisory lock, so an unclaimed exclusive lock means the file is closed.
            auto probe =
                NativeFile::Open(path, NativeFile::Access::Read, NativeFile::Disposition::OpenExisting);
            return probe.TryLockExclusive();
#endif
        }

      private:

        std::unique_ptr<std::FILE, decltype(&std::fclose)> fFile{nullptr,
                                                                 &std::fclose};  // Owned CRT stream/native descriptor.
    };
}  // namespace LLUtils