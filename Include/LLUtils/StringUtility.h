/*
Copyright (c) 2021 Lior Lahav

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

#if !defined(__cpp_char8_t) || __cpp_char8_t < 201811L
    #error "LLUtils StringUtility requires C++23 with native char8_t support."
#endif

#include "StringDefs.h"
#include <algorithm>
#include <climits>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <type_traits>
#include <utility>

#if !defined(__cpp_lib_string_resize_and_overwrite) || __cpp_lib_string_resize_and_overwrite < 202110L
    #error "LLUtils StringUtility requires C++23 string resize_and_overwrite support."
#endif

namespace LLUtils
{
    // Narrow strings are UTF-8; wchar_t is UTF-16 at 16 bits and UTF-32 at 32 bits.
    // No operation consults the locale. See docs/StringEncoding.md for the full contract.
    class StringUtility
    {
        static_assert(CHAR_BIT == 8 && (sizeof(wchar_t) == 2 || sizeof(wchar_t) == 4),
                      "StringUtility requires 8-bit bytes and 16- or 32-bit wchar_t.");

        template <class Char>
        static constexpr bool IsCharacter = std::is_same_v<Char, char> || std::is_same_v<Char, char8_t> ||
                                            std::is_same_v<Char, wchar_t>;

        template <class String>
        static constexpr bool IsString = std::is_same_v<String, std::string> || std::is_same_v<String, std::u8string> ||
                                         std::is_same_v<String, std::wstring>;

        template <class Char>
            requires IsCharacter<Char>
        static std::basic_string_view<Char> View(const Char* text)
        {
            if (text == nullptr)
                throw std::invalid_argument("Null string input");
            return text;
        }

        template <class Char>
            requires IsCharacter<Char>
        static std::basic_string_view<Char> View(const std::basic_string<Char>& text)
        {
            return text;
        }

        template <class Char>
            requires IsCharacter<Char>
        static std::basic_string_view<Char> View(std::basic_string_view<Char> text)
        {
            return text;
        }

        static constexpr char32_t InvalidCodePoint = 0x110000;

        // Transcoding decodes each scalar into char32_t and immediately encodes it in the
        // destination format, so no intermediate UTF-32 buffer is needed. Identity conversions
        // and char/char8_t copies preserve code units directly and bypass this codec.
        //
        // A portable, header-only codec gives Windows and Linux the same locale-independent
        // behavior without another dependency or backend selection. It converts text in one pass
        // with at most one allocation, avoiding temporary strings and preliminary zero-fill.
        // Benchmarks show this is faster than the original CRT mbsrtowcs/wcsrtombs implementation.
        //
        // Windows MultiByteToWideChar and WideCharToMultiByte are also locale-independent with
        // CP_UTF8 and strict flags. They can be faster for larger inputs, while tiny inputs can
        // favor this codec. The portable implementation keeps the current design simple; another
        // backend can be added if application profiling justifies it. Comparisons should use
        // equivalent validation and allocation policies.
        //
        // Other Unicode representations can reuse scalar conversion. Legacy encodings would
        // also need mapping tables, sometimes decoder state, and a policy for characters they
        // cannot represent. Decode does not throw, allowing conversion to write directly into
        // the uninitialized storage supplied by resize_and_overwrite.
        template <class Char>
        static constexpr char32_t Decode(std::basic_string_view<Char> text, std::size_t& position) noexcept
        {
            char32_t value{};
            if constexpr (std::is_same_v<Char, wchar_t>)
            {
                value = static_cast<char32_t>(text[position++]);
                if constexpr (sizeof(wchar_t) == 2)
                {
                    if (value >= 0xd800 && value <= 0xdbff)
                    {
                        if (position == text.size())
                            return InvalidCodePoint;
                        const char32_t low = static_cast<char32_t>(text[position++]);
                        if (low < 0xdc00 || low > 0xdfff)
                            return InvalidCodePoint;
                        value = 0x10000 + ((value - 0xd800) << 10) + (low - 0xdc00);
                    }
                }
            }
            else
            {
                value = static_cast<unsigned char>(text[position++]);
                if (value >= 0x80)
                {
                    unsigned continuationCount{};
                    char32_t minimum{};
                    if (value >= 0xc2 && value <= 0xdf)
                    {
                        continuationCount = 1;
                        minimum           = 0x80;
                        value &= 0x1f;
                    }
                    else if (value >= 0xe0 && value <= 0xef)
                    {
                        continuationCount = 2;
                        minimum           = 0x800;
                        value &= 0x0f;
                    }
                    else if (value >= 0xf0 && value <= 0xf4)
                    {
                        continuationCount = 3;
                        minimum           = 0x10000;
                        value &= 0x07;
                    }
                    else
                        return InvalidCodePoint;

                    if (continuationCount > text.size() - position)
                        return InvalidCodePoint;
                    for (unsigned index = 0; index < continuationCount; ++index)
                    {
                        const auto next = static_cast<unsigned char>(text[position++]);
                        if ((next & 0xc0) != 0x80)
                            return InvalidCodePoint;
                        value = (value << 6) | (next & 0x3f);
                    }
                    if (value < minimum)
                        return InvalidCodePoint;
                }
            }
            if (value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
                return InvalidCodePoint;
            return value;
        }

        template <class Char>
        static constexpr void Encode(char32_t value, Char* output, std::size_t& position) noexcept
        {
            if constexpr (std::is_same_v<Char, wchar_t>)
            {
                if constexpr (sizeof(wchar_t) == 2)
                {
                    if (value >= 0x10000)
                    {
                        value -= 0x10000;
                        output[position++] = static_cast<Char>(0xd800 + (value >> 10));
                        value              = 0xdc00 + (value & 0x3ff);
                    }
                }
                output[position++] = static_cast<Char>(value);
            }
            else
            {
                // The scalar is already validated, so each UTF-8 form can use fixed stores
                // without a sizing helper or a loop over its bytes.
                Char* const destination = output + position;
                if (value < 0x80)
                {
                    destination[0] = static_cast<Char>(value);
                    ++position;
                }
                else if (value < 0x800)
                {
                    destination[0] = static_cast<Char>(0xc0 | (value >> 6));
                    destination[1] = static_cast<Char>(0x80 | (value & 0x3f));
                    position += 2;
                }
                else if (value < 0x10000)
                {
                    destination[0] = static_cast<Char>(0xe0 | (value >> 12));
                    destination[1] = static_cast<Char>(0x80 | ((value >> 6) & 0x3f));
                    destination[2] = static_cast<Char>(0x80 | (value & 0x3f));
                    position += 3;
                }
                else
                {
                    destination[0] = static_cast<Char>(0xf0 | (value >> 18));
                    destination[1] = static_cast<Char>(0x80 | ((value >> 12) & 0x3f));
                    destination[2] = static_cast<Char>(0x80 | ((value >> 6) & 0x3f));
                    destination[3] = static_cast<Char>(0x80 | (value & 0x3f));
                    position += 4;
                }
            }
        }

      public:

        template <typename char_type, typename string_type = std::basic_string<char_type>>
        static string_type& rtrim(string_type& s, const char_type* t)
        {
            s.erase(s.find_last_not_of(t) + 1);
            return s;
        }

        template <typename char_type, typename string_type = std::basic_string<char_type>>
        static string_type& ltrim(string_type& s, const char_type* t)
        {
            s.erase(0, s.find_first_not_of(t));
            return s;
        }

        template <typename char_type, typename string_type = std::basic_string<char_type>>
        static string_type& trim(string_type& s, const char_type* t)
        {
            return ltrim(rtrim(s, t), t);
        }

        template <typename string_type, typename char_type>
        static ListString<string_type> split(const string_type& s, char_type delim)
        {
            static_assert(std::is_same_v<typename string_type::value_type, char_type>, "Char type mismatch");
            ListString<string_type> result;
            std::size_t start = 0;
            while (start < s.size())
            {
                const auto delimiter = s.find(delim, start);
                const auto end       = delimiter == string_type::npos ? s.size() : delimiter;
                if (end != start)
                {
                    auto trimmedEnd = end;
                    while (trimmedEnd > start && s[trimmedEnd - 1] == char_type{})
                        --trimmedEnd;
                    // A token containing only NULs still contributes an empty token to the result.
                    result.emplace_back(s.data() + start, trimmedEnd - start);
                }
                start = delimiter == string_type::npos ? s.size() : delimiter + 1;
            }
            return result;
        }

        // Strings and views preserve embedded NULs, while pointers end at the first NUL.
        // Conversion between byte and wide strings rejects malformed Unicode. Identity and
        // char/char8_t copies preserve code units without validation. Null pointers are rejected.
        template <class DST, class SRC>
            requires(IsString<DST> && requires(const SRC& source) { View(source); })
        static DST ConvertString(SRC&& sourceString)
        {
            if constexpr (std::is_same_v<std::remove_cvref_t<SRC>, DST> && !std::is_lvalue_reference_v<SRC> &&
                          !std::is_const_v<std::remove_reference_t<SRC>>)
                return std::forward<SRC>(sourceString);
            else
            {
                const auto source = View(sourceString);
                using SourceChar  = typename decltype(source)::value_type;
                using DestChar    = typename DST::value_type;
                if constexpr (std::is_same_v<SourceChar, DestChar>)
                    return source.empty() ? DST{} : DST(source.data(), source.size());
                else
                {
                    DST result;
                    // With identity conversions handled above, char and char8_t can copy the
                    // same UTF-8 bytes. Converting to or from wchar_t requires transcoding
                    // because it uses UTF-16 on Windows and UTF-32 on Linux.
                    if constexpr (!std::is_same_v<SourceChar, wchar_t> && !std::is_same_v<DestChar, wchar_t>)
                    {
                        result.resize_and_overwrite(source.size(),
                                                    [&](DestChar* output, std::size_t) noexcept
                                                    {
                                                        if (!source.empty())
                                                            std::memcpy(output, source.data(), source.size());
                                                        return source.size();
                                                    });
                    }
                    else
                    {
                        // Reserving spare capacity avoids a sizing pass. resize_and_overwrite
                        // also avoids zero-filling storage that the codec will immediately fill.
                        constexpr std::size_t expansion = std::is_same_v<SourceChar, wchar_t>
                                                              ? (sizeof(wchar_t) == 2 ? 3 : 4)
                                                              : 1;
                        if (source.size() > result.max_size() / expansion)
                            throw std::length_error("Converted string allocation bound is too large");
                        bool valid = true;
                        result.resize_and_overwrite(
                            source.size() * expansion,
                            [&](DestChar* output, std::size_t) noexcept
                            {
                                std::size_t written = 0;
                                for (std::size_t position = 0; position < source.size();)
                                {
                                    // Wide ASCII maps directly to UTF-8. The type condition removes
                                    // this shortcut at compile time when the input is a byte string.
                                    if (std::is_same_v<SourceChar, wchar_t> &&
                                        static_cast<std::make_unsigned_t<SourceChar>>(source[position]) < 0x80)
                                    {
                                        output[written++] = static_cast<DestChar>(source[position]);
                                        ++position;
                                    }
                                    else
                                    {
                                        const char32_t value = Decode(source, position);
                                        if (value == InvalidCodePoint)
                                        {
                                            valid = false;
                                            break;
                                        }
                                        Encode(value, output, written);
                                    }
                                }
                                return written;
                            });
                        // The overwrite callback must not throw, so invalid input is reported here.
                        if (!valid)
                            throw std::invalid_argument("Invalid Unicode input");
                    }
                    return result;
                }
            }
        }

        // Inputs convertible to const char* remain borrowed. Other supported inputs produce
        // an owning UTF-8 string.
        template <class Source>
        static auto ToAString(Source&& str)
        {
            if constexpr (std::is_convertible_v<Source, const char*>)
                return static_cast<const char*>(std::forward<Source>(str));
            else
                return ConvertString<std::string>(std::forward<Source>(str));
        }

        // These wrappers return owning strings. They copy lvalues and const inputs but can
        // reuse storage from compatible non-const owning rvalues. Pointers and views are
        // forwarded directly, avoiding an intermediate input string.
        template <class Source>
        static std::wstring ToWString(Source&& str)
        {
            return ConvertString<std::wstring>(std::forward<Source>(str));
        }

        template <class Source>
        static native_string_type ToNativeString(Source&& str)
        {
            return ConvertString<native_string_type>(std::forward<Source>(str));
        }

        template <class Source>
        static default_string_type ToDefaultString(Source&& str)
        {
            return ConvertString<default_string_type>(std::forward<Source>(str));
        }

        struct CopyResult
        {
            std::size_t written;  // Destination code units, excluding NUL.
            bool truncated;
        };

        // The caller supplies valid Unicode without embedded NULs, and the view length is trusted.
        // Capacity includes the appended terminator. Only the truncation boundary is checked,
        // avoiding decoding, terminator scans, allocation, and padding. Overlapping buffers are supported.
        template <class Char>
            requires IsCharacter<Char>
        [[nodiscard]] static CopyResult StrCpy(Char* destination, std::basic_string_view<Char> source,
                                               std::size_t capacity)
        {
            if (capacity == 0)
                return {0, true};
            if (destination == nullptr)
                throw std::invalid_argument("Null string destination");
            std::size_t written  = (std::min) (source.size(), capacity - 1);
            const bool truncated = written < source.size();
            if (truncated)
            {
                if constexpr (std::is_same_v<Char, wchar_t>)
                {
                    if constexpr (sizeof(wchar_t) == 2)
                    {
                        if (written != 0 && source[written - 1] >= 0xd800 && source[written - 1] <= 0xdbff)
                            --written;
                    }
                }
                else
                {
                    // For valid UTF-8, at most three steps back reach the start of the cut code point.
                    // Truncation ensures source[written] is within the input.
                    for (unsigned backtracked = 0; written != 0 && backtracked < 3 &&
                                                   (static_cast<unsigned char>(source[written]) & 0xc0) == 0x80;
                         ++backtracked)
                        --written;
                }
            }
            if (written != 0)
                std::memmove(destination, source.data(), written * sizeof(Char));
            destination[written] = Char{};
            return {written, truncated};
        }

        template <class Char, std::size_t N>
            requires IsCharacter<Char>
        [[nodiscard]] static CopyResult StrCpy(Char (&destination)[N], std::basic_string_view<Char> source)
        {
            return StrCpy(destination, source, N);
        }

        // Casing changes only ASCII letters and preserves all non-ASCII code units, including UTF-8 bytes.
        // The local result allows NRVO. Taking the input by value saved copies of temporaries,
        // but benchmarks showed slower calls with short lvalues on both Windows and Linux.
        template <class string_type>
        static string_type ToLower(const string_type& str)
        {
            string_type result = str;
            std::transform(result.begin(), result.end(), result.begin(),
                           [](auto c) { return c >= 'A' && c <= 'Z' ? static_cast<decltype(c)>(c + ('a' - 'A')) : c; });
            return result;
        }

        template <class string_type>
        static string_type ToUpper(const string_type& str)
        {
            string_type result = str;
            std::transform(result.begin(), result.end(), result.begin(),
                           [](auto c) { return c >= 'a' && c <= 'z' ? static_cast<decltype(c)>(c - ('a' - 'A')) : c; });
            return result;
        }
    };
}  // namespace LLUtils
