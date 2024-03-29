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
#include <string>
#include <vector>
#include <sstream>
#include "Platform.h"

namespace LLUtils
{

#if LLUTILS_PLATFORM == LLUTILS_PLATFORM_WIN32
    #if LLUTILS_CHARSET == LLUTILS_CHARSET_UNICODE
        using native_char_type = wchar_t; // UTF 16 in Windows
        #define LLUTILS_TEXT(T) L##T
        #else
            using native_char_type = char; // Ascii in Windows
        #define LLUTILS_TEXT(T) T
    #endif
#elif LLUTILS_PLATFORM == LLUTILS_PLATFORM_LINUX
        using native_char_type = char; // UTF8 in linux
        #define LLUTILS_TEXT(T) T
#endif

    using native_string_type = std::basic_string<native_char_type>;
    using native_stringstream = std::basic_stringstream<native_char_type>;

    using default_char_type = native_char_type;
    using default_string_type = std::basic_string<default_char_type>;
    using default_stringstream_type = std::basic_stringstream<default_char_type>;

    template <class string_type = default_string_type>
    using ListString = std::vector<string_type>;
    using ListStringIterator = ListString<>::iterator;

    using ListNString = ListString<native_string_type>;
    using ListNStringIterator = ListNString::iterator;
    using ListAString = ListString<std::string>;
    using ListAStringIterator = ListAString::iterator;
    using ListWString = ListString<std::wstring>;
    using ListWStringIterator = ListWString::iterator;

}  
