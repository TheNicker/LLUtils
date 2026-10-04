#pragma once
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <system_error>
#include <utility>
#ifdef _WIN32
    #include <windows.h>
    #include <io.h>
    #include <fcntl.h>
#else
    #include <fcntl.h>
    #include <sys/file.h>
    #include <sys/stat.h>
    #include <unistd.h>
#endif

namespace LLUtils
{
    // Owned native file descriptor, and the single place platform handle creation, advisory locking and
    // std::system_error mapping live. Destruction closes the descriptor and drops any lock it holds.
    // All descriptors are non-inheritable. This type adds no buffering; FileHandle layers a CRT stream on top.
    class NativeFile
    {
      public:
        // Each access value also fixes the Windows sharing mode that makes concurrent use safe; POSIX has no
        // sharing concept and ignores it, relying on the advisory locks below instead.
        enum class Access
        {
            Read,      // Read-only; also the POSIX counterpart of a delete-availability probe.
            Write,     // Write-only, opened for append; shares read, write and delete.
            Append,    // Append-only writes that can never truncate or overwrite; shares read and write.
            ReadWrite, // Used by coordination locks; shares read and write so the lock file is not replaced.
            Delete     // Windows-only probe for a delete operation; shares read, write and delete.
        };
        enum class Disposition
        {
            OpenExisting,
            OpenOrCreate,
            CreateNew
        };
        // Protected files deny removal (and other writers on Windows). POSIX protection is advisory:
        // cooperating removers must probe availability first. Ordinary files impose no such protection.
        enum class Protection
        {
            None,
            PreventRemoval
        };
        // The descriptor is stored encoded so that zero always means unopened: Windows keeps the HANDLE
        // value, POSIX keeps the descriptor plus one because descriptor zero is a valid stream.
        NativeFile() noexcept = default;
        NativeFile(NativeFile&& other) noexcept : fDescriptor(std::exchange(other.fDescriptor, Unopened))
        {
            MoveLockFrom(other);
        }
        NativeFile& operator=(NativeFile&& other) noexcept
        {
            if (this != &other)
            {
                Close();
                fDescriptor = std::exchange(other.fDescriptor, Unopened);
                MoveLockFrom(other);
            }
            return *this;
        }
        NativeFile(const NativeFile&)            = delete;
        NativeFile& operator=(const NativeFile&) = delete;
        ~NativeFile() { Close(); }
        // Opens a descriptor with the given access, disposition and protection. Throws std::system_error on
        // failure, including a failed PreventRemoval lock, so callers never observe a partially protected file.
        static NativeFile Open(const std::filesystem::path& path, Access access, Disposition disposition,
                               Protection protection = Protection::None)
        {
#ifdef _WIN32
            const DWORD desired = access == Access::Read        ? GENERIC_READ
                                  : access == Access::Write     ? GENERIC_WRITE
                                  : access == Access::Append    ? FILE_APPEND_DATA
                                  : access == Access::ReadWrite ? GENERIC_READ | GENERIC_WRITE
                                                                : DELETE;  // Access::Delete
            // Windows encodes protection in the sharing mode, so there is no lock to acquire here. A protected
            // file stays readable by others but is neither writable nor removable while it remains open.
            const DWORD share = protection == Protection::PreventRemoval
                                    ? FILE_SHARE_READ
                                    : (access == Access::Append || access == Access::ReadWrite)
                                          ? FILE_SHARE_READ | FILE_SHARE_WRITE
                                          : FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
            const DWORD dispositionFlags = disposition == Disposition::CreateNew  ? CREATE_NEW
                                          : disposition == Disposition::OpenExisting ? OPEN_EXISTING
                                                                                     : OPEN_ALWAYS;
            const auto handle = CreateFileW(path.c_str(), desired, share, nullptr, dispositionFlags,
                                            FILE_ATTRIBUTE_NORMAL, nullptr);
            if (handle == INVALID_HANDLE_VALUE)
                throw std::system_error(GetLastError(), std::system_category());
            // Private descriptors must not travel across a CreateProcess family call. This keeps emergency
            // files, coordination locks and other runtime state from being inherited by child processes.
            if (SetHandleInformation(handle, HANDLE_FLAG_INHERIT, 0) == 0)
            {
                const auto error = GetLastError();
                CloseHandle(handle);
                throw std::system_error(error, std::system_category());
            }
            NativeFile result;
            result.fDescriptor = reinterpret_cast<std::uintptr_t>(handle);
            return result;
#else
            // A delete-availability probe only needs to see the file, so it opens read-only like Access::Read.
            const int flags = access == Access::ReadWrite ? O_RDWR
                                : access == Access::Write || access == Access::Append
                                    ? O_WRONLY | O_APPEND
                                    : O_RDONLY;
            const int fd    = ::open(path.c_str(),
                                    flags | O_CLOEXEC | (disposition == Disposition::CreateNew ? O_EXCL : 0) |
                                        (disposition == Disposition::OpenExisting ? 0 : O_CREAT),
                                    S_IRUSR | S_IWUSR);
            if (fd < 0)
                throw std::system_error(errno, std::generic_category());
            // POSIX protection is advisory: a cooperating remover must probe before unlinking.
            if (protection == Protection::PreventRemoval && flock(fd, LOCK_SH | LOCK_NB) != 0)
            {
                const auto error = errno;
                ::close(fd);
                throw std::system_error(error, std::generic_category());
            }
            NativeFile result;
            result.fDescriptor = static_cast<std::uintptr_t>(fd) + 1;
            return result;
#endif
        }
        // Reports whether an exclusive lock is obtainable without waiting. POSIX holds it until Close();
        // Windows releases it again immediately, because protection there is expressed through sharing.
        bool TryLockExclusive() noexcept
        {
#ifdef _WIN32
            OVERLAPPED overlap{};  // Synchronous probe at offset zero; never pending after the call returns.
            if (LockFileEx(Handle(), LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, ExclusiveBytes, 0,
                           &overlap) == 0)
                return false;
            // The probe must not reserve the region for the caller's remaining lifetime.
            UnlockFileEx(Handle(), 0, ExclusiveBytes, 0, &overlap);
            return true;
#else
            return flock(Descriptor(), LOCK_EX | LOCK_NB) == 0;
#endif
        }
        // Blocks until the exclusive lock is held; Close() releases it.
        void LockExclusive()
        {
#ifdef _WIN32
            if (LockFileEx(Handle(), LOCKFILE_EXCLUSIVE_LOCK, 0, ExclusiveBytes, 0, &fOverlap) == 0)
                throw std::system_error(GetLastError(), std::system_category());
            fLocked = true;
#else
            if (flock(Descriptor(), LOCK_EX) != 0)
                throw std::system_error(errno, std::generic_category());
#endif
        }
        // Hands the descriptor to an owned CRT stream opened for binary append and gives up ownership.
        // Throws after releasing the descriptor if the CRT refuses it, leaving this object unopened.
        [[nodiscard]] std::FILE* ReleaseAsStream()
        {
            std::FILE* stream = nullptr;
            const auto encoded = std::exchange(fDescriptor, Unopened);
#ifdef _WIN32
            const int fd = _open_osfhandle(static_cast<std::intptr_t>(encoded), _O_BINARY | _O_APPEND | _O_NOINHERIT);
            if (fd < 0)
            {
                const auto error = errno;
                CloseHandle(reinterpret_cast<HANDLE>(encoded));
                throw std::system_error(error, std::generic_category());
            }
            stream = _fdopen(fd, "ab");
            if (!stream)
            {
                const auto error = errno;
                _close(fd);
                throw std::system_error(error, std::generic_category());
            }
#else
            stream = fdopen(static_cast<int>(encoded) - 1, "ab");
            if (!stream)
            {
                const auto error = errno;
                ::close(static_cast<int>(encoded) - 1);
                throw std::system_error(error, std::generic_category());
            }
#endif
            return stream;
        }
        // Relinquishes the descriptor without closing it, for callers whose lifetime outlives any stack frame.
        [[nodiscard]] std::uintptr_t Release() noexcept { return std::exchange(fDescriptor, Unopened); }
        void Close() noexcept
        {
            const auto encoded = std::exchange(fDescriptor, Unopened);
            if (encoded == Unopened)
                return;
#ifdef _WIN32
            if (fLocked)
            {
                UnlockFileEx(reinterpret_cast<HANDLE>(encoded), 0, ExclusiveBytes, 0, &fOverlap);
                fLocked = false;
            }
            CloseHandle(reinterpret_cast<HANDLE>(encoded));
#else
            ::close(static_cast<int>(encoded) - 1);  // Closing also drops any advisory lock.
#endif
        }
        explicit operator bool() const noexcept { return fDescriptor != Unopened; }

      private:

        static constexpr std::uintptr_t Unopened = 0;
#ifdef _WIN32
        static constexpr DWORD ExclusiveBytes = 1;  // All participants lock the first byte.
        HANDLE Handle() const noexcept { return reinterpret_cast<HANDLE>(fDescriptor); }
        // The exclusive region travels with the handle, so a move must carry both or the unlock is skipped.
        void MoveLockFrom(NativeFile& other) noexcept
        {
            fOverlap = other.fOverlap;
            fLocked  = std::exchange(other.fLocked, false);
        }
        OVERLAPPED fOverlap{};  // Synchronous lock at offset zero, never pending during a move.
        bool fLocked = false;  // Exclusive lock held; a failed LockExclusive never sets it.
#else
        int Descriptor() const noexcept { return static_cast<int>(fDescriptor) - 1; }
        void MoveLockFrom(NativeFile&) noexcept {}  // Closing the descriptor drops any advisory lock.
#endif
        std::uintptr_t fDescriptor = Unopened;  // Platform handle or descriptor plus one.
    };
}  // namespace LLUtils