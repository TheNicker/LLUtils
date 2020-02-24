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

#include <memory>
#include <stdexcept>
#include <cstring>
#include <LLUtils/Platform.h>

namespace LLUtils
{
    class STDAlloc
    {
    public:
        static std::byte* Allocate(size_t size)
        {
            return new std::byte[size];
        }

        static void Deallocate(std::byte* buffer)
        {
            delete[] buffer;
        }
    };


    class AlignedAlloc
    {
    public:
        static constexpr int Alignment = 16;

        static std::byte * Allocate(size_t size)
        {
        #if LLUTILS_COMPILER_MIN_VERSION(LLUTILS_COMPILER_MSVC, 1700)
            return reinterpret_cast<std::byte*>(_aligned_malloc(size, Alignment));
        #else
            return reinterpret_cast<std::byte*>(std::aligned_alloc(size, Alignment));
        #endif
        }

        static void Deallocate(std::byte* buffer)
        {
        #if LLUTILS_COMPILER_MIN_VERSION(LLUTILS_COMPILER_MSVC, 1700)
            _aligned_free(buffer);
        #else
            std::free(buffer);
        #endif
        }
    };


    template <typename Alloc>
    class BufferBase
    {
    public:
        using Allocator = Alloc;
        BufferBase(const BufferBase& rhs)
        {
            *this = Clone(rhs);
            
        }
        
        BufferBase& operator=(const BufferBase& rhs)
        {
            *this = rhs.Clone();
            return *this;
        }
        

        BufferBase() {}
        BufferBase(size_t size)
        {
            Allocate(size);
        }

        void operator=(BufferBase&& rhs) noexcept
        {
            Swap(std::move(rhs));
        }

        BufferBase(BufferBase&& rhs) noexcept
        {
            Swap(std::move(rhs));
        }

        bool operator==(nullptr_t null) const
        {
            return fData == null;
        }

        bool operator!=(nullptr_t null) const
        {
            return fData != null;
        }

        BufferBase Clone() const
        {
            BufferBase cloned(fSize);
            cloned.Write(fData, 0, fSize);
            return cloned;
        }

		[[deprecated("deprecated, use Buffer::data() instead")]]
        const std::byte* GetBuffer() const
        {
            return fData;
        }

		[[deprecated("deprecated, use Buffer::data() instead")]]
        std::byte* GetBuffer()
        {
            return fData;
        }

		std::byte* data()
		{
			return fData;
		}

		const std::byte* data() const
		{
			return fData;
		}


        void Free()
        {
            FreeImpl();
        }

        void Allocate(size_t size)
        {
            AllocateImp(size);
        }

        void Read(std::byte* dest, size_t offset, size_t size) const
        {
            if (offset + size <= fSize)
                memcpy(dest, fData + offset, size);
            else
                throw std::runtime_error("Memory read overflow");
        }

        void Write(const std::byte* BufferBase, size_t offset, size_t size)
        {
            if (offset + size <= fSize)
                memcpy(fData + offset, BufferBase, size);
            else
                throw std::runtime_error("Memory write overflow");
        }

        ~BufferBase()
        {
            Free();
        }

        size_t Size() const
        {
            return fSize;
        }

		size_t size() const
		{
			return fSize;
		}


        // buffer must have been allocated with the corresponding Allocator.
        void TransferOwnership(size_t size, std::byte*& data)
        {
            fSize = size;
            fData = data;
            data = nullptr;
        }

        // buffer must be freed  with the corresponding Allocator.
        void RemoveOwnership(size_t& size , std::byte*& data)
        {
            size = fSize;
            fSize = 0;
            data = fData;
            fData = nullptr;
        }

    private:
        // private methods
        void Swap(BufferBase&& rhs)
        {
            std::swap(fSize, rhs.fSize);
            rhs.fSize = 0;
            std::swap(fData, rhs.fData);
            Allocator::Deallocate(rhs.fData);
            rhs.fData = nullptr;
        }

        void AllocateImp(size_t size)
        {
            Free();
            fData = Allocator::Allocate(size);
            fSize = size;
        }



        void FreeImpl()
        {
            if (fData != nullptr)
            {
                Allocator::Deallocate(fData);
                fData = nullptr;
                fSize = 0;
            }
        }

        // private member fields
        std::byte* fData = nullptr;
        size_t fSize = 0;
    };

    using Buffer = BufferBase<AlignedAlloc>;
}