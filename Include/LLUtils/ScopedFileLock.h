#pragma once
#include "NativeFile.h"

namespace LLUtils
{
    // Blocking exclusive cross-process coordination on a non-inheritable lock file.
    // Every participant must use the same path; destruction releases ownership without throwing.
    class ScopedFileLock
    {
      public:

        explicit ScopedFileLock(const std::filesystem::path& path)
            : fFile(NativeFile::Open(path, NativeFile::Access::ReadWrite, NativeFile::Disposition::OpenOrCreate))
        {
            // A failed acquisition unwinds fFile, which closes the descriptor without ever having locked it.
            fFile.LockExclusive();
        }
        // The descriptor is move-only and releases its lock when destroyed, so no explicit cleanup is needed.
        ScopedFileLock(ScopedFileLock&&) noexcept            = default;
        ScopedFileLock& operator=(ScopedFileLock&&) noexcept = default;
        ScopedFileLock(const ScopedFileLock&)            = delete;
        ScopedFileLock& operator=(const ScopedFileLock&) = delete;
        ~ScopedFileLock()                                 = default;

      private:

        NativeFile fFile;  // Locked exclusively for the lifetime of this object.
    };
}  // namespace LLUtils