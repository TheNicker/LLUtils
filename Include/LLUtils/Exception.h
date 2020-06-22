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
#include <exception>
#include <string>
#include <array>
#include "Event.h"
#include "PlatformUtility.h"
#include "StringUtility.h"

namespace LLUtils
{
 
    class Exception : public std::exception
    {
    public:
        enum class ErrorCode
        {
              Unspecified
            , Unknown
            , CorruptedValue
            , LogicError
            , RuntimeError
            , DuplicateItem
            , BadParameters
            , NotImplemented
            , InvalidState
            , SystemError
			, NotFound
            , __Count__
        };

        struct EventArgs
        {
            ErrorCode errorCode;
            std::wstring description;
            std::wstring systemErrorMessage;
            std::wstring callstack;
            std::wstring functionName;
        };

    
        using OnExceptionEventType = Event<void(EventArgs)>;
        static inline OnExceptionEventType OnException;
        Exception(ErrorCode errorCode, std::string function, std::string description, bool systemError)
        {
            using namespace std;
            PlatformUtility::StackTrace stackTrace = PlatformUtility::GetCallStack(2);

            std::wstring systemErrorMessage;
            if (systemError == true)
                systemErrorMessage = PlatformUtility::GetLastErrorAsString() ;
            
            wstringstream ss;

			for (const auto& f : stackTrace)
			{
				ss << filesystem::path(f.moduleName).filename().wstring() << L"!" << f.name;
				if (f.sourceFileName.empty() == false)
					ss << L" at " << f.sourceFileName << dec << L" line: " << f.line << L" column: " << f.displacement;

				ss << L" at address 0x" << hex << f.address << endl;
			}
			

            std::wstring callStack = ss.str();
            EventArgs args =
            {
                  errorCode
                , LLUtils::StringUtility::ToWString(description)
                , systemErrorMessage
                , callStack
                , LLUtils::StringUtility::ToWString(function)
            };
            
            OnException.Raise(args);
        }

        static std::wstring ExceptionErrorCodeToString(ErrorCode errorCode)
        {
            static std::array<std::wstring, static_cast<size_t>( ErrorCode::__Count__)> errorcodeToString =
            {
                 L"Unspecified"
                ,L"Unknown"
                ,L"Corrupted value"
                ,L"Logic error"
                ,L"Runtime error"
                ,L"Duplicate item"
                ,L"Bad parameters"
                ,L"Missing implmentation"
                ,L"System error"
            };

            size_t errorCodeInt = static_cast<size_t>(errorCode);

            if (errorCodeInt < errorcodeToString.size())
                return errorcodeToString[errorCodeInt];
            else
                return L"Unspecified";
        }
    };


#define LL_EXCEPTION(ERROR_CODE,DESCRIPTION) ( throw LLUtils::Exception(ERROR_CODE, __FUNCTION__, DESCRIPTION, false) )

//predefine common exceptions
#define LL_EXCEPTION_SYSTEM_ERROR(DESCRIPTION) ( throw LLUtils::Exception(LLUtils::Exception::ErrorCode::SystemError, __FUNCTION__, DESCRIPTION, true) )
#define LL_EXCEPTION_UNEXPECTED_VALUE ( throw LLUtils::Exception(LLUtils::Exception::ErrorCode::RuntimeError, __FUNCTION__, "unexpected or coruppted value.", false) )
#define LL_EXCEPTION_NOT_IMPLEMENT(WHAT) ( throw LLUtils::Exception(LLUtils::Exception::ErrorCode::NotImplemented, __FUNCTION__, WHAT, false) )
    
}