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
#include <type_traits>
#include <cmath>
#include "Platform.h"

namespace LLUtils
{
    class Math
    {
    public:
        
        static constexpr double PI =  3.14159265358979323846;
        
		template <typename T, typename std::enable_if_t<std::is_integral_v<T>, int> = 0>
        static constexpr LLUTILS_FORCE_INLINE T Modulu(T val, T mod)
        {
            return (mod + (val % mod)) % mod;
        }
        template <typename T, typename std::enable_if_t<std::is_floating_point_v<T>, int> = 0>
        static constexpr LLUTILS_FORCE_INLINE T Modulu(T val, T mod)
        {
            return fmod(mod + fmod(val, mod), mod);
        }

        template <typename T, typename std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>
        static constexpr LLUTILS_FORCE_INLINE T Sign(T val)
        {
            return (static_cast<T>(0) < val) - (val < static_cast<T>(0));
        }

        template <typename T, typename std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>
        static constexpr LLUTILS_FORCE_INLINE T ToDegrees(T val)
        {
            return (val * static_cast<T>(180)) / static_cast<T>(PI);
        }

        template <typename T, typename std::enable_if_t<std::is_arithmetic_v<T>, int> = 0>
        static constexpr LLUTILS_FORCE_INLINE T ToRadians(T val)
        {
            return (val * static_cast<T>(PI)) / static_cast<T>(180);
        }
    };
}