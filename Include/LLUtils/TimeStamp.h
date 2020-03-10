/*
Copyright (c) 2020 Lior Lahav

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
#ifndef _LLUTILS_TIME_H_
#define _LLUTILS_TIME_H_

#include "Platform.h"
#define LLUTILS_TIMESTAMP_TECHNIQUE_ANSI_C 1
#define LLUTILS_TIMESTAMP_TECHNIQUE_WINDOWS_GETTIME 2
#define LLUTILS_TIMESTAMP_TECHNIQUE_GETTIMEOFDAY 3

#if LLUTILS_PLATFORM == LLUTILS_PLATFORM_WIN32
    #define LLUTILS_TIMESTAMP_TECHNIQUE LLUTILS_TIMESTAMP_TECHNIQUE_WINDOWS_GETTIME
#elif LLUTILS_PLATFORM == LLUTILS_PLATFORM_APPLE || LLUTILS_PLATFORM == LLUTILS_PLATFORM_APPLE_IOS || LLUTILS_PLATFORM == LLUTILS_PLATFORM_LINUX
    #define LLUTILS_TIMESTAMP_TECHNIQUE LLUTILS_TIMESTAMP_TECHNIQUE_GETTIMEOFDAY
#else
    #define LLUTILS_TIMESTAMP_TECHNIQUE LLUTILS_TIMESTAMP_TECHNIQUE_ANSI_C
#endif





#include <ctime>

#if LLUTILS_TIMESTAMP_TECHNIQUE == LLUTILS_TIMESTAMP_TECHNIQUE_WINDOWS_GETTIME
    #include <Windows.h>
#endif

#include <string>
#include <array>
#include <cassert>

#include "Templates.h"
#include "Exception.h"

namespace LLUtils
{
    
    class TimeStamp
    {

    private:
#if LLUTILS_TIMESTAMP_TECHNIQUE == LLUTILS_TIMESTAMP_TECHNIQUE_WINDOWS_GETTIME
        static native_string_type GetTimeStampWindows()
        {
            SYSTEMTIME time;
            constexpr size_t BufferSize = 24;
            std::array<native_char_type, BufferSize> buffer;
            GetLocalTime(&time);
            constexpr native_char_type dateFormat[] = LLUTILS_TEXT("yyyy-MM-dd");
            int ret = GetDateFormat(LOCALE_SYSTEM_DEFAULT, 0, &time, dateFormat, buffer.data(), sizeof(dateFormat));
            native_char_type* currentPos = buffer.data() + array_length(dateFormat) - 1;
            *currentPos++ = ' ';

            constexpr native_char_type timeFormat[] = LLUTILS_TEXT("HH:mm:ss");
            ret = GetTimeFormat(LOCALE_SYSTEM_DEFAULT, TIME_FORCE24HOURFORMAT, &time, timeFormat, currentPos, sizeof(timeFormat));
            currentPos += array_length(timeFormat) - 1;

            *currentPos++ = '.';
            const native_char_type* millisecPos = currentPos;

            WORD millisec = time.wMilliseconds;

            if (millisec < 100)
                *currentPos++ = '0';
            if (millisec < 10)
                *currentPos++ = '0';

            size_t length = (3 - (currentPos - millisecPos));
            size_t size = length * sizeof(native_char_type);
            
                memcpy_s(currentPos
                    ,size
#if LLUTILS_CHARSET == LLUTILS_CHARSET_UNICODE
                ,std::to_wstring(millisec).c_str()
#else 
                ,std::to_string(millisec).c_str()
#endif
            
            ,size);

            currentPos += length;
            *currentPos++ = '\0';

            assert("Mismatch buffer size" && currentPos - buffer.data() == BufferSize);

            return native_string_type(buffer.data());

        }
#endif

#if LLUTILS_TIMESTAMP_TECHNIQUE == LLUTILS_TIMESTAMP_TECHNIQUE_GETTIMEOFDAY
        static native_string_type GetTimeStampTimeOfDay()
        {
            LL_EXCEPTION_NOT_IMPLEMENT("time of day is not implemented");
        }
#endif

        static native_string_type GetTimeStampAnsiC()
        {

            std::array<native_char_type, 20> buffer;
            time_t timer;
            tm tm_info;
            timer = time(nullptr);
            localtime_s(&tm_info, &timer);

#if LLUTILS_CHARSET == LLUTILS_CHARSET_UNICODE
            wcsftime(buffer.data(), buffer.size(), TEXT("%Y-%m-%d %H:%M:%S"), &tm_info);
#else
            strftime(buffer.data(), buffer.size(), TEXT("%Y-%m-%d %H:%M:%S"), &tm_info);
#endif
            
            
            return native_string_type(buffer.data());
        }

    public:
        static native_string_type Now()
        {
#if LLUTILS_TIMESTAMP_TECHNIQUE == LLUTILS_TIMESTAMP_TECHNIQUE_WINDOWS_GETTIME
                return GetTimeStampWindows();
#elif LLUTILS_TIMESTAMP_TECHNIQUE == LLUTILS_TIMESTAMP_TECHNIQUE_GETTIMEOFDAY
                return GetTimeStampTimeOfDay();
#elif LLUTILS_TIMESTAMP_TECHNIQUE == LLUTILS_TIMESTAMP_TECHNIQUE_ANSI_C
                return GetTimeStampAnsiC();
#else
    #error Unknown timestamp technique
#endif

            
        }

    };
}
#endif //#define _LLUTILS_TIME_H_
