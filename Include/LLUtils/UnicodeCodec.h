#pragma once
#include <cstddef>
#include <string_view>
#include <type_traits>

namespace LLUtils::UnicodeDetail
{
    constexpr char32_t InvalidCodePoint      = 0x110000;
    constexpr char32_t ReplacementCharacter  = 0xfffd;

    // Portable, allocation-free scalar transcoding, shared by StringUtility and the logging
    // emergency path. Decode reads one scalar and Encode writes it in the destination format, so no
    // intermediate UTF-32 buffer and no temporary string is required. Both are constexpr and never
    // throw, which is what lets the crash path use them where the allocating, throwing
    // StringUtility::ConvertString cannot be used.
    //
    // A header-only codec gives Windows and Linux the same locale-independent behavior without
    // another dependency or backend selection. Benchmarks show this is faster than the original CRT
    // mbsrtowcs/wcsrtombs implementation. Windows MultiByteToWideChar and WideCharToMultiByte are
    // also locale-independent with CP_UTF8 and strict flags, and can be faster for larger inputs,
    // while tiny inputs can favor this codec. The portable implementation keeps the design simple;
    // another backend can be added if application profiling justifies it. Comparisons should use
    // equivalent validation and allocation policies.
    //
    // Other Unicode representations can reuse scalar conversion. Legacy encodings would also need
    // mapping tables, sometimes decoder state, and a policy for characters they cannot represent.
    // Decode reports failure by returning InvalidCodePoint instead of throwing, so a caller may
    // write directly into uninitialized storage.
    // Reads the scalar starting at position and advances position past it. The caller must ensure
    // position is below text.size(); Decode reads unconditionally and does not check that bound.
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

    // Writes one already validated scalar. A value at or above U+10000 emits a surrogate pair on
    // 16-bit wchar_t, so the caller must guarantee two free slots before calling. This function
    // performs no bounds checking of its own.
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

    // Transcodes UTF-8 into caller-provided wide storage, substituting ReplacementCharacter for any
    // invalid sequence so untrusted text can never suppress output. Writes at most capacity - 1 units
    // and terminates at that index, so the caller may use the result as a NUL-terminated wide string.
    // Returns the unit count, which is never greater than capacity - 1.
    //
    // Encode emits a surrogate pair for a scalar at or above U+10000 without bounds checking, so the
    // loop admits a call only while two units remain spare: entering with written <= capacity - 3 lets
    // Encode write at most indices capacity - 3 and capacity - 2, leaving written <= capacity - 1. That
    // bound holds for any input, independently of the fact that UTF-16 units never exceed UTF-8 bytes.
    inline std::size_t TranscodeToWide(std::string_view utf8, wchar_t* output, std::size_t capacity) noexcept
    {
        if (capacity == 0)
            return 0;
        std::size_t position = 0, written = 0;
        while (position < utf8.size() && written + 3 <= capacity)
        {
            auto value = Decode(utf8, position);
            if (value == InvalidCodePoint)
                value = ReplacementCharacter;
            Encode(value, output, written);
        }
        output[written] = L'\0';
        return written;
    }
}  // namespace LLUtils::UnicodeDetail