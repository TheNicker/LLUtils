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

#include <array>
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

        static StackTrace GetCallStack([[maybe_unused]] int framesToSkip = 0)
        {
            StackTrace stackTrace;

#if LLUTILS_PLATFORM == LLUTILS_PLATFORM_WIN32 && defined(LLUTILS_ENABLE_DEBUG_SYMBOLS) &&                             \
    LLUTILS_ENABLE_DEBUG_SYMBOLS == 1
            constexpr USHORT MaxStackTraceSize = std::numeric_limits<USHORT>::max();
            static thread_local std::array<void*, MaxStackTraceSize> stack;
            const HANDLE process = GetCurrentProcess();

            unsigned short frames = CaptureStackBackTrace(static_cast<DWORD>(framesToSkip), MaxStackTraceSize,
                                                          stack.data(), nullptr);

            static const std::string MutexPrefix = "OIV_Symbol_Api_Mutex";

            struct ScopedMutex
            {
                ScopedMutex(bool allowThrow) : mutexLock(CreateMutexA(nullptr, FALSE, (MutexPrefix + "_Lock").c_str()))
                {
                    if (mutexLock != nullptr)
                        WaitForSingleObject(mutexLock, INFINITE);
                    else if (allowThrow == true)
                        throw std::logic_error("Error, can not create mutex");
                }

                ~ScopedMutex()
                {
                    if (mutexLock != nullptr)
                    {
                        ReleaseMutex(mutexLock);
                        CloseHandle(mutexLock);
                    }
                }

              private:

                const HANDLE mutexLock = nullptr;
            };

            ScopedMutex lock(false);

            constexpr size_t maxNameLength = 255;
            constexpr size_t sizeOfStruct = sizeof(SYMBOL_INFOW);
            auto symbolReservedMemory = std::make_unique<std::byte[]>(sizeOfStruct +
                                                                      (maxNameLength - 1) * sizeof(TCHAR));
            SYMBOL_INFOW* symbol = reinterpret_cast<SYMBOL_INFOW*>(symbolReservedMemory.get());

            if (SymInitializeW(process, nullptr, TRUE) == TRUE)
            {
                symbol->MaxNameLen = maxNameLength;
                symbol->SizeOfStruct = sizeOfStruct;

                stackTrace.resize(frames);
                for (size_t i = 0; i < frames; i++)
                {
                    StackTraceEntry& entry = stackTrace[i];

                    const DWORD64 memoryAddress = reinterpret_cast<DWORD64>(stack[i]);

                    entry.address = memoryAddress;

                    if (SymFromAddrW(process, memoryAddress, nullptr, symbol) == TRUE)
                    {
                        entry.name = symbol->Name;
                        entry.address = symbol->Address;
                    }

                    IMAGEHLP_LINEW64 line;
                    line.SizeOfStruct = sizeof(IMAGEHLP_LINEW64);
                    DWORD disp;
                    if (SymGetLineFromAddrW64(process, memoryAddress, &disp, &line) == TRUE)
                    {
                        entry.line = line.LineNumber;
                        entry.displacement = disp;
                        entry.sourceFileName = line.FileName;
                    }

                    IMAGEHLP_MODULEW64 module64;
                    module64.SizeOfStruct = sizeof(IMAGEHLP_MODULEW64);

                    if (SymGetModuleInfoW64(process, memoryAddress, &module64) == TRUE)
                    {
                        entry.moduleName = module64.ImageName;
                    }
                }
                if (SymCleanup(process) == FALSE)
                {
                    // something bad has happend.
                }
            }
#elif LLUTILS_PLATFORM == LLUTILS_PLATFORM_LINUX
            rlimit limit;
            getrlimit(RLIMIT_STACK, &limit);
            const int maxFrames = limit.rlim_cur;
            auto callstackPtrs = std::make_unique<void*[]>(maxFrames);
            int nFrames = backtrace(callstackPtrs.get(), maxFrames);
            char** symbols = backtrace_symbols(callstackPtrs.get(), nFrames);
            stackTrace.resize(nFrames - framesToSkip);
            auto exec_path = GetExePath();

            for (int i = 0; i < nFrames - framesToSkip; i++)
            {
                int currentFrame = i + framesToSkip;
                Dl_info info;
                StackTraceEntry& entry = stackTrace[i];
                entry.address = (uint64_t) callstackPtrs[currentFrame];
                if (dladdr(callstackPtrs[currentFrame], &info))
                {
                    if (info.dli_saddr != nullptr)
                    {
                        entry.moduleName = info.dli_fname;
                        void* symAddr = (void*) ((char*) info.dli_saddr - (char*) info.dli_fbase);
                        std::stringstream ss;
                        ss << symAddr;

                        auto r = sh("addr2line -C  -e " + exec_path + " " + ss.str());

                        if (r.find('?') == std::string::npos)
                        {
                            auto idx = r.find_first_of(':');
                            std::string filename;
                            std::string line;

                            if (idx != std::string::npos)
                            {
                                entry.sourceFileName = r.substr(0, idx);
                                entry.line = std::stol(r.substr(idx + 1, r.length() - idx - 1));
                            }
                        }

                        char* demangled = NULL;
                        int status{};
                        size_t size{};
                        demangled = abi::__cxa_demangle(info.dli_sname, nullptr, &size, &status);
                        std::string functionName;
                        if (status == 0)
                        {
                            auto buffer = std::make_unique<char[]>(size);
                            entry.name = abi::__cxa_demangle(info.dli_sname, buffer.get(), &size, &status);
                            free(demangled);
                        }
                        else
                        {
                            if (info.dli_sname != nullptr)
                                entry.name = info.dli_sname;
                        }
                    }
                }
            }
            free(symbols);
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

                return StringUtility::ToDefaultString(native_string_type(ownPth));
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
            return StringUtility::ToDefaultString(filesystem::path(GetDllPath()).parent_path().wstring());
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

        static default_string_type GetExeFolder()
        {
            using namespace std;
            return StringUtility::ToDefaultString(filesystem::path(GetExePath()).parent_path().wstring());
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

            CHAR_TYPE* messageBuffer = nullptr;
            size_t size = 0;
            if (typeid(CHAR_TYPE) == typeid(wchar_t))
            {
                size = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                          FORMAT_MESSAGE_IGNORE_INSERTS,
                                      nullptr, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                      reinterpret_cast<wchar_t*>(&messageBuffer), 0, nullptr);
            }
            else
            {
                size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                          FORMAT_MESSAGE_IGNORE_INSERTS,
                                      nullptr, errorMessageID, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                      reinterpret_cast<char*>(&messageBuffer), 0, nullptr);
            }

            ustring message(messageBuffer, size);

            // Free the buffer.
            LocalFree(messageBuffer);

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