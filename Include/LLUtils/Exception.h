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
#include <atomic>
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
            , Count
        };
        enum class Mode
        {
             Exception
            ,Error
        };

        struct EventArgs
        {
            ErrorCode errorCode{};
            native_string_type description;
            native_string_type systemErrorMessage;
            PlatformUtility::StackTrace stackTrace;
            native_string_type functionName;
            Mode exceptionmode{};
        };

    
        // Event supplies synchronized registration, snapshots, and subscription ownership.
        // Exception observation adds only failure containment and recursion suppression.
        class OnExceptionEventType : private Event<void(const EventArgs&), true>
        {
            using Base = Event<void(const EventArgs&), true>;

          public:

            using Base::Func;
            using Base::Subscribe;
            using Base::Subscription;

            void Raise(const EventArgs& args) noexcept
            {
                if (sNotifying)
                    return;
                struct NotificationScope
                {
                    NotificationScope() { sNotifying = true; }
                    ~NotificationScope() { sNotifying = false; }
                } scope;
                try
                {
                    VisitListeners(
                        [&](const Func& callback) noexcept
                        {
                            try
                            {
                                callback(args);
                            }
                            catch (...)
                            { /* Diagnostics must not replace the original failure. */
                            }
                            return true;
                        });
                }
                catch (...)
                { /* Snapshot allocation is optional diagnostic work as well. */
                }
            }

          private:

            static inline thread_local bool sNotifying = false;
        };

        static OnExceptionEventType OnException;
        static inline std::atomic_bool sThrowErrorsInDebug{true};
        static void SetThrowErrorsInDebug(bool shouldThrow) noexcept
        {
            sThrowErrorsInDebug.store(shouldThrow, std::memory_order_relaxed);
        }

        static native_string_type FormatStackTrace(const PlatformUtility::StackTrace& stackTrace, uint16_t maxDepth = std::numeric_limits<uint16_t>::max())
        {
            using namespace std;
            using char_type = native_string_type::value_type;
            basic_stringstream<char_type> ss;
            uint16_t depth = 0;
			for (const auto& f : stackTrace)
			{
                if (depth++ > maxDepth)
                    break;

				ss << filesystem::path(f.moduleName).filename().string<char_type>() << LLUTILS_TEXT("!") << f.name;
				if (f.sourceFileName.empty() == false)
					ss << LLUTILS_TEXT(" at ") << f.sourceFileName << dec << LLUTILS_TEXT(" line: ") << f.line 
#if LLUTILS_PLATFORM == LLUTILS_PLATFORM_WIN32 
                    << LLUTILS_TEXT(" column: ") << f.displacement
#endif
                    ;

				ss << LLUTILS_TEXT(" at address 0x") << hex << f.address << endl;
			}
			
            return ss.str();

        }

        Exception(ErrorCode errorCode, std::string function, std::string description, bool systemError, Mode exceptionMode,int callStackLevel = 2)
        {
            native_string_type systemErrorMessage;
            if (systemError == true)
                systemErrorMessage = PlatformUtility::GetLastErrorAsString<native_string_type::value_type>() ;
            
            EventArgs args =
            {
                  errorCode
                , LLUtils::StringUtility::ToNativeString(description)
                , systemErrorMessage
                , PlatformUtility::GetCallStack(callStackLevel)
                , LLUtils::StringUtility::ToNativeString(function)
                , exceptionMode
            };
            
            OnException.Raise(args);
        }

        static native_string_type ExceptionErrorCodeToString(ErrorCode errorCode)
        {
            static std::array<native_string_type, static_cast<size_t>( ErrorCode::Count)> errorcodeToString =
            {
                {
                     LLUTILS_TEXT("Unspecified")
                    ,LLUTILS_TEXT("Unknown")
                    ,LLUTILS_TEXT("Corrupted value")
                    ,LLUTILS_TEXT("Logic error")
                    ,LLUTILS_TEXT("Runtime error")
                    ,LLUTILS_TEXT("Duplicate item")
                    ,LLUTILS_TEXT("Bad parameters")
                    ,LLUTILS_TEXT("Missing implmentation")
                    ,LLUTILS_TEXT("System error")
                }
            };

            size_t errorCodeInt = static_cast<size_t>(errorCode);

            if (errorCodeInt < errorcodeToString.size())
                return errorcodeToString[errorCodeInt];
            else
                return LLUTILS_TEXT("Unspecified");
        }
    };

    inline Exception::OnExceptionEventType Exception::OnException;

#define LL_EXCEPTION(ERROR_CODE,DESCRIPTION) ( throw LLUtils::Exception(ERROR_CODE, __FUNCTION__, DESCRIPTION, false, LLUtils::Exception::Mode::Exception) )

    //predefine common exceptions
#define LL_EXCEPTION_SYSTEM_ERROR(DESCRIPTION) ( throw LLUtils::Exception(LLUtils::Exception::ErrorCode::SystemError, __FUNCTION__, DESCRIPTION, true, LLUtils::Exception::Mode::Exception) )
#define LL_EXCEPTION_UNEXPECTED_VALUE ( throw LLUtils::Exception(LLUtils::Exception::ErrorCode::RuntimeError, __FUNCTION__, "unexpected or coruppted value.", false, LLUtils::Exception::Mode::Exception) )
#define LL_EXCEPTION_NOT_IMPLEMENT(WHAT) ( throw LLUtils::Exception(LLUtils::Exception::ErrorCode::NotImplemented, __FUNCTION__, WHAT, false, LLUtils::Exception::Mode::Exception) )

// Create an exception object without throwing.
#define LL_EXCEPTION_DONT_THROW(ERROR_CODE,DESCRIPTION) ( LLUtils::Exception(ERROR_CODE, __FUNCTION__, DESCRIPTION, false, LLUtils::Exception::Mode::Error) )

#ifdef _DEBUG
    #define LL_ERROR(ERROR_CODE, DESCRIPTION)                                                                          \
        do                                                                                                             \
        {                                                                                                              \
            if (::LLUtils::Exception::sThrowErrorsInDebug.load(std::memory_order_relaxed))                             \
                LL_EXCEPTION((ERROR_CODE), (DESCRIPTION));                                                             \
            else                                                                                                       \
                LL_EXCEPTION_DONT_THROW((ERROR_CODE), (DESCRIPTION));                                                  \
        } while (false)
#else
    #define LL_ERROR LL_EXCEPTION_DONT_THROW
#endif


}