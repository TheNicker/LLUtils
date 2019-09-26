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
#include <windows.h>
namespace LLUtils
{
    class FileMapping
    {
        
    public:

        FileMapping(std::wstring filePath) : fFilePath(filePath)
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
            if (mView != nullptr && UnmapViewOfFile(mView) == 0)
                throw std::exception("Error unmapping file");

            if (mHandleMMF != nullptr && CloseHandle(mHandleMMF) == 0)
                throw std::exception("Error unmapping file");

            if (mHandleFile != nullptr && CloseHandle(mHandleFile) == 0)
                throw std::exception("Error unmapping file");

            mView = mHandleMMF = mHandleFile = nullptr;
        }

        void* GetBuffer() const
        {
            return mView;
        }
        std::size_t GetSize() const
        {
            return mSize;
        }

    private: //methods

        void OpenImp()
        {
            mHandleFile = CreateFileW(fFilePath.c_str(), GENERIC_READ,
                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL, nullptr);

            if (mHandleFile != INVALID_HANDLE_VALUE)
            {
                mHandleMMF = CreateFileMapping(mHandleFile, nullptr, PAGE_READONLY, 0, 0, nullptr);
                if (mHandleMMF != nullptr)
                    mView = MapViewOfFile(mHandleMMF, FILE_MAP_READ, 0, 0, 0);

                mSize = std::filesystem::file_size(fFilePath);
            }
        }

    private: // member fields
        const std::wstring fFilePath;
        std::size_t mSize = 0;
        HANDLE mHandleMMF = nullptr;
        HANDLE mHandleFile = nullptr;
        void *mView = nullptr;
    };
}