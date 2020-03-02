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
#ifndef _BIT_FLAGS_H_
#define _BIT_FLAGS_H_
#include "EnumClassBitwise.h"
namespace LLUtils
{
    template<typename T>
    class BitFlags
    {
    public:
        constexpr inline BitFlags() = default;
        constexpr inline BitFlags(T value) { mValue = value; }
        constexpr inline BitFlags operator| (T rhs) const { return mValue | rhs; }
        constexpr inline BitFlags operator& (T rhs) const { return mValue & rhs; }
        constexpr inline BitFlags operator~ () const { return ~mValue; }
        constexpr inline operator T() const { return mValue; }
        constexpr inline BitFlags& operator|=(T rhs) { mValue |= rhs; return *this; }
        constexpr inline BitFlags& operator&=(T rhs) { mValue &= rhs; return *this; }
        constexpr inline bool test(T rhs) const { return (mValue & rhs) == rhs; }
        constexpr inline void set(T rhs) { mValue |= rhs; }
        constexpr inline void clear(T rhs) { mValue &= ~rhs; }
        constexpr inline T get() const { return mValue;}

    private:
        T mValue;
    };
}
#endif //#define _BIT_FLAGS_H_
