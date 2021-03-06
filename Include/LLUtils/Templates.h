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
namespace LLUtils
{
    class NoCopyable
    {
    public:
        NoCopyable() = default;
        NoCopyable(const NoCopyable&) = delete;
        NoCopyable& operator=(const NoCopyable&) = delete;
    };


template <typename T> 
T constexpr GetMaxBitsMask()
{
    // ((1 << (width of T - 1)) * 2 ) - 1
    return  (((static_cast<T>(1) << (sizeof(T) * static_cast<T>(8) - static_cast<T>(1))) - static_cast<T>(1)) << static_cast<T>(1)) + static_cast<T>(1);
}

template<size_t LENGTH, class T>
constexpr inline size_t array_length([[maybe_unused]] T(&arr)[LENGTH]) { return LENGTH; }

template<size_t LENGTH, class T>
constexpr inline size_t array_size(T(&arr)[LENGTH]) { return array_length(arr) * sizeof(T); }


}