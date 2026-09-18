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

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>
#include <memory>
#include "Platform.h"
#if LLUTILS_PLATFORM == LLUTILS_PLATFORM_WIN32
#include <Windows.h>
#include <ShlObj.h>
#include <DbgHelp.h>
#elif LLUTILS_PLATFORM == LLUTILS_PLATFORM_LINUX
#include <execinfo.h> /* backtrace, backtrace_symbols */
#include <dlfcn.h>    // for dladdr
#include <cxxabi.h>   // for __cxa_demangle
#include <sys/resource.h>
#include <algorithm>
#include <time.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#endif

#include "StringDefs.h"
#include "Utility.h"
#include "StringUtility.h"
#include "Buffer.h"
#include <LLUtils/Warnings.h>

#if LLUTILS_PLATFORM == LLUTILS_PLATFORM_WIN32

LLUTILS_DISABLE_WARNING_PUSH
LLUTILS_DISABLE_WARNING_RESEREVED_IDENTIFIER
EXTERN_C IMAGE_DOS_HEADER __ImageBase;
LLUTILS_DISABLE_WARNING_POP

#pragma push_macro("max")

#undef max
#endif

namespace LLUtils
{

#if LLUTILS_PLATFORM == LLUTILS_PLATFORM_LINUX
    namespace
    {
        std::string sh(std::string cmd)
        {
            std::array<char, 128> buffer;
            std::string result;
            std::shared_ptr<FILE> pipe(popen(cmd.c_str(), "r"), pclose);
            if (!pipe)
                throw std::runtime_error("popen() failed!");
            while (!feof(pipe.get()))
            {
                if (fgets(buffer.data(), 128, pipe.get()) != nullptr)
                {
                    result += buffer.data();
                }
            }
            result = LLUtils::StringUtility::rtrim(result, "\n");
            return result;
        }
    }  // namespace
#endif

    class PlatformUtility
    {
      public:

        struct StackTraceEntry
        {
            native_string_type moduleName;
            native_string_type name;
            native_string_type sourceFileName;
            uint64_t address = 0;
            uint32_t line = 0;
            uint32_t displacement = 0;
        };

        using StackTrace = std::vector<StackTraceEntry>;

        // Keep diagnostic capture bounded. Symbol lookup is optional; raw instruction
        // addresses remain useful when symbols are disabled, busy, or unavailable.
        static StackTrace GetCallStack(int framesToSkip = 0)
        {
            constexpr std::size_t MaxFrames = 64;
            std::array<void*, MaxFrames> addresses{};
            StackTrace stackTrace(0);
            const auto skip = static_cast<std::size_t>((std::max) (framesToSkip, 0));
            if (skip >= MaxFrames)
                return stackTrace;
#if LLUTILS_PLATFORM == LLUTILS_PLATFORM_WIN32
            const auto frames = CaptureStackBackTrace(0, static_cast<DWORD>(addresses.size()), addresses.data(),
                                                      nullptr);
#elif LLUTILS_PLATFORM == LLUTILS_PLATFORM_LINUX
            const auto frames = backtrace(addresses.data(), static_cast<int>(addresses.size()));
#else
            constexpr int frames = 0;
#endif
            const auto count = static_cast<std::size_t>((std::max) (static_cast<int>(frames), 0));
            for (auto index = (std::min) (skip, count); index < count; ++index)
                stackTrace.push_back({.address = reinterpret_cast<std::uintptr_t>(addresses[index])});

#if defined(LLUTILS_ENABLE_DEBUG_SYMBOLS) && LLUTILS_ENABLE_DEBUG_SYMBOLS == 1
            try
            {
    #if LLUTILS_PLATFORM == LLUTILS_PLATFORM_WIN32
                // DbgHelp is process-wide. Keep the existing cross-module lock name, but do
                // not wait for another failing thread, or use DbgHelp after a lock failure.
                struct SymbolSession
                {
                    HANDLE mutex     = CreateMutexW(nullptr, FALSE, L"OIV_Symbol_Api_Mutex_Lock");
                    HANDLE process   = GetCurrentProcess();
                    bool locked      = false;
                    bool initialized = false;
                    SymbolSession()
                    {
                        if (mutex != nullptr)
                        {
                            const auto result = WaitForSingleObject(mutex, 0);
                            locked            = result == WAIT_OBJECT_0 || result == WAIT_ABANDONED;
                            if (locked && result != WAIT_ABANDONED)
                                initialized = SymInitializeW(process, nullptr, TRUE) != FALSE;
                        }
                    }
                    ~SymbolSession()
                    {
                        if (initialized)
                            SymCleanup(process);
                        if (locked)
                            ReleaseMutex(mutex);
                        if (mutex != nullptr)
                            CloseHandle(mutex);
                    }
                } symbols;
                if (symbols.initialized)
                {
                    constexpr std::size_t MaxName = 256;
                    alignas(SYMBOL_INFOW) std::array<std::byte, sizeof(SYMBOL_INFOW) + MaxName * sizeof(wchar_t)>
                        storage{};
                    auto* symbol         = reinterpret_cast<SYMBOL_INFOW*>(storage.data());
                    symbol->SizeOfStruct = sizeof(SYMBOL_INFOW);
                    symbol->MaxNameLen   = MaxName;
                    for (auto& entry : stackTrace)
                    {
                        if (SymFromAddrW(symbols.process, entry.address, nullptr, symbol))
                            entry.name.assign(symbol->Name, symbol->NameLen);
                        IMAGEHLP_LINEW64 line{};
                        line.SizeOfStruct = sizeof(line);
                        DWORD displacement{};
                        if (SymGetLineFromAddrW64(symbols.process, entry.address, &displacement, &line))
                        {
                            entry.line         = line.LineNumber;
                            entry.displacement = displacement;
                            if (line.FileName != nullptr)
                                entry.sourceFileName = line.FileName;
                        }
                        IMAGEHLP_MODULEW64 module{};
                        module.SizeOfStruct = sizeof(module);
                        if (SymGetModuleInfoW64(symbols.process, entry.address, &module))
                            entry.moduleName = module.ImageName;
                    }
                }
    #elif LLUTILS_PLATFORM == LLUTILS_PLATFORM_LINUX
                for (auto& entry : stackTrace)
                {
                    Dl_info info{};
                    if (dladdr(reinterpret_cast<void*>(entry.address), &info))
                    {
                        if (info.dli_fname != nullptr)
                            entry.moduleName = info.dli_fname;
                        if (info.dli_sname != nullptr)
                        {
                            int status{};
                            const std::unique_ptr<char, decltype(&std::free)> name(
                                abi::__cxa_demangle(info.dli_sname, nullptr, nullptr, &status), &std::free);
                            entry.name = status == 0 && name ? name.get() : info.dli_sname;
                        }
                    }
                }
    #endif
            }
            catch (...)
            { /* Preserve captured addresses if optional enrichment fails. */
            }
#endif
            return stackTrace;
        }

        struct OSVersion
        {
            uint32_t major;
            uint32_t minor;
            uint32_t build;
        };

        static OSVersion GetOSVersion()
        {
#if LLUTILS_PLATFORM == LLUTILS_PLATFORM_WIN32

            using Win32VersionInfo = OSVERSIONINFOEX;
            auto GetWin32OSVersion = []() -> Win32VersionInfo
            {
                HMODULE hMod = ::GetModuleHandleW(L"ntdll.dll");

                using NTSTATUS = LONG;
                constexpr NTSTATUS STATUS_SUCCESS = 0x00000000;
                using RtlGetVersionPtr = INT_PTR(FAR WINAPI*)(Win32VersionInfo*);

                Win32VersionInfo vi{};

                if (hMod != nullptr)
                {
                    LLUTILS_DISABLE_WARNING_PUSH
                    LLUTILS_DISABLE_WARNING_CAST_FUNCTION_TYPE
                    RtlGetVersionPtr fxPtr = reinterpret_cast<RtlGetVersionPtr>(
                        ::GetProcAddress(hMod, "RtlGetVersion"));
                    LLUTILS_DISABLE_WARNING_POP

                    if (fxPtr != nullptr)
                    {
                        vi.dwOSVersionInfoSize = sizeof(vi);
                        if (STATUS_SUCCESS != fxPtr(&vi))
                        {
                            throw std::runtime_error("Could not get OS version info.");
                        }
                    }
                }
                return vi;
            };

            const Win32VersionInfo win32Version = GetWin32OSVersion();
            return {win32Version.dwMajorVersion, win32Version.dwMinorVersion, win32Version.dwBuildNumber};

#else
            throw std::runtime_error("GetOSVersion: Not implemented in the current platform.");
#endif
        }

        static native_string_type GetAppDataFolder()
        {
#if LLUTILS_PLATFORM == LLUTILS_PLATFORM_WIN32
            native_char_type szPath[MAX_PATH];

            if (SUCCEEDED(SHGetFolderPath(nullptr, CSIDL_APPDATA | CSIDL_FLAG_CREATE, nullptr, 0, szPath)))
            {
                native_string_type result = szPath;
                return result;
            }

            return native_string_type();
#else
            throw std::logic_error("GetAppDataFolder: Not implemented in the current platform.");
            // TODO: Make the exception call below work here
            // LL_EXCEPTION(LLUtils::Exception::ErrorCode::NotImplemented,);
#endif
        }

#if LLUTILS_PLATFORM == LLUTILS_PLATFORM_WIN32

        template <size_t divVersion = 1>
        static LLUtils::Buffer CreateDIB(uint32_t width, uint32_t height, uint16_t bpp, uint32_t rowPitch,
                                         const std::byte* buffer)
        {
            if constexpr (divVersion == 5)
                return CreateDIB<BITMAPV5HEADER>(width, height, bpp, rowPitch, buffer);
            else
                return CreateDIB<BITMAPINFOHEADER>(width, height, bpp, rowPitch, buffer);
        }

        template <typename DibHeaderType>
        static LLUtils::Buffer CreateDIB(uint32_t width, uint32_t height, uint16_t bpp, uint32_t rowPitch,
                                         const std::byte* buffer)
        {
            // Align target dib scanline to 32 bit
            const DWORD dwBytesPerLine = LLUtils::Utility::Align(static_cast<DWORD>(bpp * width),
                                                                 static_cast<DWORD>((sizeof(DWORD) * CHAR_BIT))) /
                                         CHAR_BIT;
            const DWORD paletteSize = 0;  // not supproted.
            const size_t imageSize = static_cast<size_t>(dwBytesPerLine * height);
            const DWORD dibBufferSize = sizeof(DibHeaderType) + paletteSize + imageSize;
            LLUtils::Buffer dibBuffer(dibBufferSize);

            DibHeaderType& bi = *reinterpret_cast<DibHeaderType*>(dibBuffer.data());
            bi = {};

            BITMAPINFOHEADER& v1 = reinterpret_cast<BITMAPINFOHEADER&>(bi);
            v1.biSize = sizeof(DibHeaderType);
            v1.biWidth = static_cast<LONG>(width);
            v1.biHeight = static_cast<LONG>(height);
            v1.biPlanes = 1;      // must be 1
            v1.biBitCount = bpp;  // from parameter
            v1.biCompression = BI_RGB;

            size_t targetOffset = sizeof(DibHeaderType);

            // if source row pitch is identical to destination row pitch, copy in one pass
            if (dwBytesPerLine == rowPitch)
            {
                dibBuffer.Write(buffer, targetOffset, imageSize);
            }
            else
            {
                const size_t bytesTowritePerLIne = std::min<size_t>(rowPitch, dwBytesPerLine);
                size_t sourceOffset = 0;

                for (uint32_t y = 0; y < height; y++)
                {
                    dibBuffer.Write(reinterpret_cast<const std::byte*>(reinterpret_cast<const uint8_t*>(buffer) +
                                                                       sourceOffset),
                                    targetOffset, bytesTowritePerLIne);

                    targetOffset += dwBytesPerLine;
                    sourceOffset += rowPitch;
                }
            }
            return dibBuffer;
        }

        static default_string_type GetModulePath(HMODULE hModule)
        {
            native_char_type ownPth[MAX_PATH];
            if (hModule != nullptr && GetModuleFileName(hModule, ownPth, (sizeof(ownPth) / sizeof(ownPth[0]))) > 0)

                return default_string_type(ownPth);
            else
                return default_string_type();
        }

        static default_string_type GetDllPath()
        {
            return GetModulePath(reinterpret_cast<HINSTANCE>(&__ImageBase));
        }

        static default_string_type GetDllFolder()
        {
            using namespace std;
            return StringUtility::ToDefaultString(filesystem::path(GetDllPath()).parent_path().native());
        }
#endif
        static default_string_type GetExePath()
        {
#if LLUTILS_PLATFORM == LLUTILS_PLATFORM_WIN32
            return GetModulePath(GetModuleHandle(nullptr));

#elif LLUTILS_PLATFORM == LLUTILS_PLATFORM_LINUX
            native_char_type result[PATH_MAX]{};
            ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
            return native_string_type(result, (count > 0) ? count : 0);
#endif
        }

        // Keep filesystem operations in the native representation. A wide round trip
        // adds work on Linux and can reject native filename bytes under the C locale.
        static default_string_type GetExeFolder()
        {
            using namespace std;
            return StringUtility::ToDefaultString(filesystem::path(GetExePath()).parent_path().native());
        }

        static void nanosleep(uint64_t ns)
        {
#if LLUTILS_PLATFORM == LLUTILS_PLATFORM_WIN32
            class NanoSleep
            {
              public:

                void Wait(uint64_t ns)
                {
                    mLargeIntener.QuadPart = -static_cast<LONGLONG>(ns / 100);  // std::max<int64_t>(100, ns) / 100;
                    if (!SetWaitableTimer(mTimer, &mLargeIntener, 0, nullptr, nullptr, FALSE))
                        throw std::logic_error("Error, could not set timer");

                    std::ignore = WaitForSingleObject(mTimer, INFINITE);
                }
                ~NanoSleep()
                {
                    CloseHandle(mTimer);
                }

              private:

                HANDLE mTimer = CreateWaitableTimer(nullptr, TRUE, nullptr);
                LARGE_INTEGER mLargeIntener{};
            };

            static thread_local NanoSleep timer;
            timer.Wait(ns);
#elif LLUTILS_PLATFORM == LLUTILS_PLATFORM_LINUX
            timespec req{};
            timespec rem{};

            if (ns >= 1'000'000'000)
            {
                req.tv_nsec = ns % 1'000'000'000;
                req.tv_sec = (ns - req.tv_nsec) / 1'000'000'000;
            }
            else
            {
                req.tv_nsec = ns;
            }

            ::nanosleep(&req, &rem);
#endif
        }

        // Returns the last Win32 error, in string format. Returns an empty string if there is no error.
        template <class CHAR_TYPE = wchar_t, typename ustring = std::basic_string<CHAR_TYPE>>
        static ustring GetLastErrorAsString()
        {
#if LLUTILS_PLATFORM == LLUTILS_PLATFORM_WIN32
            // Get the error message, if any.
            DWORD errorMessageID = ::GetLastError();
            if (errorMessageID == 0)
                return ustring();  // No error message has been recorded

            wchar_t* messageBuffer = nullptr;
            const DWORD size       = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                                        FORMAT_MESSAGE_IGNORE_INSERTS,
                                                    nullptr, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                                    reinterpret_cast<wchar_t*>(&messageBuffer), 0, nullptr);
            const auto freeMessage = [](wchar_t* buffer) { LocalFree(buffer); };
            const std::unique_ptr<wchar_t, decltype(freeMessage)> ownedMessage(messageBuffer, freeMessage);
            const ustring message = size == 0
                                        ? ustring{}
                                        : StringUtility::ConvertString<ustring>(std::wstring_view(messageBuffer, size));

            return message;
#else
            return ustring();
#endif
        }

        struct CoresInfo
        {
            uint16_t physicalCores = 0;
            uint16_t logicalCores = 0;
        };

#if LLUTILS_PLATFORM == LLUTILS_PLATFORM_WIN32

      private:

        static DWORD CountSetBits(ULONG_PTR bitMask)
        {
            DWORD LSHIFT = sizeof(ULONG_PTR) * 8 - 1;
            DWORD bitSetCount = 0;
            ULONG_PTR bitTest = static_cast<ULONG_PTR>(1) << LSHIFT;
            for (DWORD i = 0; i <= LSHIFT; ++i)
            {
                bitSetCount += ((bitMask & bitTest) ? 1 : 0);
                bitTest /= 2;
            }
            return bitSetCount;
        }

      public:

        static CoresInfo GetCPUCoresInfo()
        {
            CoresInfo result;
            DWORD cores = 0, logical = 0, len = 0;
            if (FALSE == GetLogicalProcessorInformationEx(RelationAll, nullptr, &len))
            {
                if (GetLastError() == ERROR_INSUFFICIENT_BUFFER)
                {
                    size_t pos = 0;
                    auto buffer = std::make_unique<char[]>(len);
                    if (GetLogicalProcessorInformationEx(
                            RelationAll, reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(buffer.get()),
                            &len))
                    {
                        while (pos < len)
                        {
                            PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX pi =
                                reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(&buffer[pos]);
                            if (pi->Relationship == RelationProcessorCore)
                            {
                                cores++;
                                LLUTILS_DISABLE_WARNING_PUSH
                                LLUTILS_DISABLE_WARNING_UNSAFE_BUFFER_USAGE
                                for (size_t g = 0; g < pi->Processor.GroupCount; ++g)
                                    logical += CountSetBits(pi->Processor.GroupMask[g].Mask);
                                LLUTILS_DISABLE_WARNING_POP
                            }
                            pos += pi->Size;
                        }

                        result.physicalCores = static_cast<uint16_t>(cores);
                        result.logicalCores = static_cast<uint16_t>(logical);
                    }
                }
            }
            return result;
        }

#elif LLUTILS_PLATFORM == LLUTILS_PLATFORM_LINUX
        static CoresInfo GetCPUCoresInfo()
        {
            auto numberOfCores = sh("lscpu | grep -oP 'Core\\(s\\) per socket:\\s*\\K.+'");
            return {static_cast<uint16_t>(std::stoi(numberOfCores)), static_cast<uint16_t>(get_nprocs())};
        }
#endif
    };
}  // namespace LLUtils

#if LLUTILS_PLATFORM == LLUTILS_PLATFORM_WIN32
#pragma pop_macro("max")
#endif