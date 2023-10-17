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
#include <filesystem>
#include <string>
#include <cstdint>
#include "Platform.h"
#include "StringDefs.h"

#if LLUTILS_PLATFORM == LLUTILS_PLATFORM_WIN32
    #include <Windows.h>
#elif LLUTILS_PLATFORM == LLUTILS_PLATFORM_LINUX
    #include <sys/mman.h>
	#include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
#endif

#if LLUTILS_PLATFORM == LLUTILS_PLATFORM_WIN32
    using NATIVE_HANDLE = HANDLE;
#elif LLUTILS_PLATFORM == LLUTILS_PLATFORM_LINUX
    using NATIVE_HANDLE = int;
#endif

namespace LLUtils
{
    class FileMapping
    {
    public:
        FileMapping(const native_string_type& filePath) : fFilePath(filePath)
        {
            Open();
        }

        ~FileMapping()
        {
            Close();
        }

        void Open()
        {
            Close();
            OpenImp();
        }

        void Close()
        {
#if LLUTILS_PLATFORM == LLUTILS_PLATFORM_WIN32
            if (fView != nullptr && UnmapViewOfFile(fView) == 0)
                throw std::runtime_error("Error unmapping file");

            if (fHandleMMF != nullptr && CloseHandle(fHandleMMF) == 0)
                throw std::runtime_error("Error unmapping file");

            if (fHandleFile != nullptr && CloseHandle(fHandleFile) == 0)
                throw std::runtime_error("Error unmapping file");
            fView = fHandleMMF = fHandleFile = nullptr;   
#elif LLUTILS_PLATFORM == LLUTILS_PLATFORM_LINUX
        if (fView != MAP_FAILED && fView != nullptr && munmap(fView, fSize) == -1)
            throw std::runtime_error("Error mapping file");
        if (fHandleFile == -1 && close(fHandleFile) == -1)
            throw std::runtime_error("Cannot close file");
            fView = nullptr;
            fHandleMMF = fHandleFile = 0;
#endif
        
        }

        void* GetBuffer() const
        {
            return fView;
        }
        uintmax_t GetSize() const
        {
            return fSize;
        }

    private: //methods

        void OpenImp()
        {
#if LLUTILS_PLATFORM == LLUTILS_PLATFORM_WIN32
            fHandleFile = CreateFile(fFilePath.c_str(), GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL, nullptr);

            if (fHandleFile != INVALID_HANDLE_VALUE)
            {
                fHandleMMF = CreateFileMapping(fHandleFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
                if (fHandleMMF != nullptr)
                    fView = MapViewOfFile(fHandleMMF, FILE_MAP_READ, 0, 0, 0);

                fSize = std::filesystem::file_size(fFilePath);
            }


#elif LLUTILS_PLATFORM == LLUTILS_PLATFORM_LINUX
        fHandleFile = open(fFilePath.c_str(), O_RDONLY);
        if (fHandleFile == -1)
            throw std::runtime_error("Cannot open file");

        struct stat64 sb;
        if (fstat64(fHandleFile, &sb) == -1)
            throw std::runtime_error("Cannot get file information");

        fSize = static_cast<uintmax_t>(sb.st_size);
        fView = mmap(NULL, fSize, PROT_READ,
                       MAP_PRIVATE, fHandleFile, 0);
        if (fView == MAP_FAILED)
            throw std::runtime_error("Cannot map file");

#endif
        }

    private: // member fields
        const native_string_type fFilePath;
        uintmax_t fSize{};
        NATIVE_HANDLE fHandleMMF{};
        NATIVE_HANDLE fHandleFile{};
        void *fView = nullptr;
    };
}