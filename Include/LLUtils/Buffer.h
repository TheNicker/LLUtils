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

#include <cstddef>
#include <memory>
#include <stdexcept>
#include <cstring>
#include <LLUtils/Platform.h>
#include <LLUtils/Utility.h>
#include <LLUtils/Warnings.h>

#if __has_include(<LLUtilsBufferCustomAllocator.h>)
    #define LLUTILS_BUFFER_CUSTOM_ALLOCATOR 1
    #include <LLUtilsBufferCustomAllocator.h>
#endif

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
            return reinterpret_cast<std::byte*>(_aligned_malloc(LLUtils::Utility::Align<size_t>(size, Alignment), Alignment));
        #else
            return reinterpret_cast<std::byte*>(std::aligned_alloc(Alignment, LLUtils::Utility::Align<size_t>(size, Alignment)));
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


#if defined (LLUTILS_BUFFER_CUSTOM_ALLOCATOR) &&  LLUTILS_BUFFER_CUSTOM_ALLOCATOR == 1
    using DefaultAllocator = CustomMemoryAllocator;
#else
    using DefaultAllocator = AlignedAlloc;
#endif
    
    
    template <typename Alloc>
    class BufferBase
    {
    public:
        using Allocator = Alloc;

        BufferBase() {}
        BufferBase(size_t size)
        {
            Allocate(size);
        }

        BufferBase(const std::byte* data, size_t size)
        {
            if (size > 0)
            {
                Allocate(size);
                Write(data, 0, size);
            }
        }


        BufferBase(const BufferBase& rhs, size_t size) : BufferBase(rhs.data(), size)
        {
            
        }

        BufferBase(const BufferBase& rhs)
        {
            *this = rhs.Clone();
        }
        
        BufferBase(BufferBase&& rhs) noexcept
        {
            Swap(std::move(rhs));
        }

        BufferBase& operator=(const BufferBase& rhs)
        {
            *this = rhs.Clone();
            return *this;
        }

        void operator=(BufferBase&& rhs) noexcept
        {
            Swap(std::move(rhs));
        }

        bool operator==(std::nullptr_t null) const
        {
            return fData == null;
        }

        bool operator!=(std::nullptr_t null) const
        {
            return fData != null;
        }

        BufferBase Clone() const
        {
            BufferBase cloned;
            if (fSize > 0)
            {
                cloned.Allocate(fSize);
                cloned.Write(fData, 0, fSize);
            }

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

        std::byte*& dataRef()
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
LLUTILS_DISABLE_WARNING_PUSH
LLUTILS_DISABLE_WARNING_UNSAFE_BUFFER_USAGE
            if (offset + size <= fSize)
                memcpy(fData + offset, BufferBase, size);
            else
                throw std::runtime_error("Memory write overflow");
LLUTILS_DISABLE_WARNING_POP
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
    using Buffer = BufferBase<DefaultAllocator>;
}