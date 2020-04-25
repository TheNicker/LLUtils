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
#ifndef _ENUM_CLASS_BITWISE_H_
#define _ENUM_CLASS_BITWISE_H_
#include <type_traits>

#if 1
#define LLUTILS_DEFINE_ENUM_CLASS_FLAG_OPERATIONS_EMPTY_TOKEN

#define LLUTILS_DEFINE_ENUM_CLASS_FLAG_OPERATIONS_IMPL(Enum,PREFIX) \
\
    PREFIX constexpr inline Enum operator~ (Enum val)\
    {\
        val = static_cast<Enum>(~static_cast<std::underlying_type_t<Enum>>(val));\
        return val;\
    }\
		\
    PREFIX constexpr inline Enum operator& (Enum lhs, Enum rhs)\
    {\
        return static_cast<Enum>(static_cast<std::underlying_type_t<Enum>>(lhs) & static_cast<std::underlying_type_t<Enum>>(rhs));\
    }\
	\
    PREFIX constexpr inline Enum operator<< (Enum lhs, std::underlying_type_t<Enum> rhs)\
    {\
        return static_cast<Enum>(static_cast<std::underlying_type_t<Enum>>(lhs) << rhs);\
    }\
	\
    PREFIX constexpr inline Enum operator>> (Enum lhs, std::underlying_type_t<Enum> rhs)\
    {\
        return static_cast<Enum>(static_cast<std::underlying_type_t<Enum>>(lhs) >> rhs);\
    }\
	\
    PREFIX constexpr inline Enum operator&= (Enum& lhs, Enum rhs)\
    {\
        lhs = static_cast<Enum>(static_cast<std::underlying_type_t<Enum>>(lhs) & static_cast<std::underlying_type_t<Enum>>(rhs));\
        return lhs;\
    }\
	\
    PREFIX constexpr inline Enum operator| (Enum lhs, Enum rhs)\
    {\
        return static_cast<Enum>(static_cast<std::underlying_type_t<Enum>>(lhs) | static_cast<std::underlying_type_t<Enum>>(rhs));\
    }\
	\
    PREFIX constexpr inline Enum& operator|= (Enum& lhs, Enum rhs)\
    {\
        lhs = static_cast<Enum>(static_cast<std::underlying_type_t<Enum>>(lhs) | static_cast<std::underlying_type_t<Enum>>(rhs));\
        return lhs;\
    }\
    PREFIX constexpr inline Enum& operator<<= (Enum& lhs, std::underlying_type_t<Enum> rhs)\
    {\
        lhs = static_cast<Enum>(static_cast<std::underlying_type_t<Enum>>(lhs) << rhs);\
        return lhs;\
    }\
    PREFIX constexpr inline Enum& operator>>= (Enum& lhs, std::underlying_type_t<Enum> rhs)\
    {\
        lhs = static_cast<Enum>(static_cast<std::underlying_type_t<Enum>>(lhs) >> rhs);\
        return lhs;\
    }\

#define LLUTILS_DEFINE_ENUM_CLASS_FLAG_OPERATIONS(ENUM) LLUTILS_DEFINE_ENUM_CLASS_FLAG_OPERATIONS_IMPL(ENUM,LLUTILS_DEFINE_ENUM_CLASS_FLAG_OPERATIONS_EMPTY_TOKEN )
#define LLUTILS_DEFINE_ENUM_CLASS_FLAG_OPERATIONS_IN_CLASS(ENUM) LLUTILS_DEFINE_ENUM_CLASS_FLAG_OPERATIONS_IMPL(ENUM,friend )


#else
//Global enum bit flags 
//unary ~operator
template <typename Enum, typename std::enable_if_t<std::is_enum<Enum>::value, int> = 0>
constexpr inline Enum & operator~ (Enum & val)
{
	val = static_cast<Enum>(~static_cast<std::underlying_type_t<Enum>>(val));
	return val;
}

// & operator
template <typename Enum, typename std::enable_if_t<std::is_enum<Enum>::value, int> = 0>
constexpr inline Enum operator& (Enum lhs, Enum rhs)
{
	return static_cast<Enum>(static_cast<std::underlying_type_t<Enum>>(lhs) & static_cast<std::underlying_type_t<Enum>>(rhs));
}

// &= operator
template <typename Enum, typename std::enable_if_t<std::is_enum<Enum>::value, int> = 0>
constexpr inline Enum operator&= (Enum & lhs, Enum rhs)
{
	lhs = static_cast<Enum>(static_cast<std::underlying_type_t<Enum>>(lhs) & static_cast<std::underlying_type_t<Enum>>(rhs));
	return lhs;
}

//| operator

template <typename Enum, typename std::enable_if_t<std::is_enum<Enum>::value, int> = 0 >
constexpr inline Enum operator| (Enum lhs, Enum rhs)
{
	return static_cast<Enum>(static_cast<std::underlying_type_t<Enum>>(lhs) | static_cast<std::underlying_type_t<Enum>>(rhs));
}
//|= operator

template <typename Enum, typename std::enable_if_t<std::is_enum<Enum>::value, int> = 0 >
constexpr inline Enum & operator|= (Enum & lhs, Enum rhs)
{
	lhs = static_cast<Enum>(static_cast<std::underlying_type_t<Enum>>(lhs) | static_cast<std::underlying_type_t<Enum>>(rhs));
	return lhs;
}
#endif

#endif // _ENUM_CLASS_BITWISE_H_