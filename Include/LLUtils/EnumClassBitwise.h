/*
Copyright (c) 2019-2026 Lior Lahav

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

#include <concepts>
#include <type_traits>
#include <utility>


namespace LLUtils::detail
{
    template<typename Enum>
    concept BitwiseEnum =
        std::is_enum_v<Enum> &&
        requires(Enum value)
        {
            { enable_enum_class_bitwise(value) } -> std::same_as<std::true_type>;
        };
}

// Use LLUTILS_DEFINE_ENUM_CLASS_FLAG_OPERATIONS to opt-in an enum class to bitwise operators. 
 #define LLUTILS_DEFINE_ENUM_CLASS_FLAG_OPERATIONS(Enum) \
     inline consteval std::true_type enable_enum_class_bitwise(Enum) noexcept { return {}; }

// Use LLUTILS_DEFINE_ENUM_CLASS_FLAG_OPERATIONS_IN_CLASS to opt-in an enum class defined inside a class to bitwise operators.
// This second macro is for convinience and readability, as the first macro can be used for enums defined inside a class as well, 
// but it requires specifying the class name as part of the enum name and defining the operators outside of the class, which can be less readable.

#define LLUTILS_DEFINE_ENUM_CLASS_FLAG_OPERATIONS_IN_CLASS(Enum) \
    friend inline consteval std::true_type enable_enum_class_bitwise(Enum) noexcept { return {}; }

template<LLUtils::detail::BitwiseEnum Enum>
[[nodiscard]] constexpr inline Enum operator~(Enum value) noexcept
{
    return static_cast<Enum>(~std::to_underlying(value));
}

template<LLUtils::detail::BitwiseEnum Enum>
[[nodiscard]] constexpr inline Enum operator&(Enum lhs, Enum rhs) noexcept
{
    return static_cast<Enum>(std::to_underlying(lhs) & std::to_underlying(rhs));
}

template<LLUtils::detail::BitwiseEnum Enum>
[[nodiscard]] constexpr inline Enum operator<<(Enum lhs, std::underlying_type_t<Enum> rhs) noexcept
{
    return static_cast<Enum>(std::to_underlying(lhs) << rhs);
}

template<LLUtils::detail::BitwiseEnum Enum>
[[nodiscard]] constexpr inline Enum operator>>(Enum lhs, std::underlying_type_t<Enum> rhs) noexcept
{
    return static_cast<Enum>(std::to_underlying(lhs) >> rhs);
}

template<LLUtils::detail::BitwiseEnum Enum>
[[nodiscard]] constexpr inline Enum operator^(Enum lhs, Enum rhs) noexcept
{
    return static_cast<Enum>(std::to_underlying(lhs) ^ std::to_underlying(rhs));
}

template<LLUtils::detail::BitwiseEnum Enum>
constexpr inline Enum& operator^=(Enum& lhs, Enum rhs) noexcept
{
    lhs = lhs ^ rhs;
    return lhs;
}

template<LLUtils::detail::BitwiseEnum Enum>
constexpr inline Enum& operator&=(Enum& lhs, Enum rhs) noexcept
{
    lhs = lhs & rhs;
    return lhs;
}

template<LLUtils::detail::BitwiseEnum Enum>
[[nodiscard]] constexpr inline Enum operator|(Enum lhs, Enum rhs) noexcept
{
    return static_cast<Enum>(std::to_underlying(lhs) | std::to_underlying(rhs));
}

template<LLUtils::detail::BitwiseEnum Enum>
constexpr inline Enum& operator|=(Enum& lhs, Enum rhs) noexcept
{
    lhs = lhs | rhs;
    return lhs;
}

template<LLUtils::detail::BitwiseEnum Enum>
constexpr inline Enum& operator<<=(Enum& lhs, std::underlying_type_t<Enum> rhs) noexcept
{
    lhs = lhs << rhs;
    return lhs;
}

template<LLUtils::detail::BitwiseEnum Enum>
constexpr inline Enum& operator>>=(Enum& lhs, std::underlying_type_t<Enum> rhs) noexcept
{
    lhs = lhs >> rhs;
    return lhs;
}
#endif // _ENUM_CLASS_BITWISE_H_