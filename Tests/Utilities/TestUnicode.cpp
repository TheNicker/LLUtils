#include <catch2/catch_test_macros.hpp>
#include <LLUtils/StringUtility.h>
#include <LLUtils/UnicodeCodec.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace
{
    using namespace LLUtils;
    using namespace LLUtils::UnicodeDetail;

    constexpr std::size_t SupplementaryUnits = sizeof(wchar_t) == 2 ? 2 : 1;

    // One scalar with its canonical UTF-8 form and the native wide unit count it must produce.
    struct Scalar
    {
        char32_t value;
        std::string_view utf8;
        std::size_t units;
    };

    // Boundaries around every UTF-8 length change, plus both sides of the surrogate threshold. The U+0000
    // entry spells out its length because a string_view built from a bare literal stops at the NUL and would
    // be empty, which is not the one-code-unit input this case is about.
    constexpr Scalar Scalars[] = {
        {0x000000, std::string_view("\x00", 1), 1},
        {0x000041, "A", 1},
        {0x00007f, "\x7f", 1},
        {0x000080, "\xc2\x80", 1},
        {0x0007ff, "\xdf\xbf", 1},
        {0x000800, "\xe0\xa0\x80", 1},
        {0x00ffff, "\xef\xbf\xbf", 1},
        {0x010000, "\xf0\x90\x80\x80", SupplementaryUnits},
        {0x01f600, "\xf0\x9f\x98\x80", SupplementaryUnits},
        {0x10ffff, "\xf4\x8f\xbf\xbf", SupplementaryUnits},
    };

    // Transcodes with Encode directly rather than a reimplementation, so the shared codec is what the
    // assertions actually exercise. Native wide units never exceed UTF-8 bytes, which the separate
    // invariant test below asserts, so sizing by the input length is safe here.
    std::wstring Transcode(std::string_view utf8)
    {
        std::wstring result(utf8.size() + 1, L'\0');
        std::size_t position = 0, written = 0;
        while (position < utf8.size())
        {
            const auto value = Decode(utf8, position);
            REQUIRE(value != InvalidCodePoint);
            Encode(value, result.data(), written);
        }
        result.resize(written);
        return result;
    }

    // Wide to UTF-8 is bounded by the widest single wchar_t expansion rather than the input length.
    std::string TranscodeBack(std::wstring_view wide)
    {
        constexpr std::size_t expansion = sizeof(wchar_t) == 2 ? 3 : 4;
        std::string result(wide.size() * expansion + 1, '\0');
        std::size_t position = 0, written = 0;
        while (position < wide.size())
        {
            const auto value = Decode(wide, position);
            REQUIRE(value != InvalidCodePoint);
            Encode(value, result.data(), written);
        }
        result.resize(written);
        return result;
    }

    // Runs TranscodeToWide into a canary-padded buffer and verifies it stayed inside the capacity.
    std::size_t TranscodeGuarded(std::string_view utf8, std::size_t capacity)
    {
        constexpr wchar_t canary = 0x5a5a;
        std::vector<wchar_t> buffer(capacity + 4, canary);
        const auto units = TranscodeToWide(utf8, buffer.data(), capacity);
        if (capacity == 0)
        {
            REQUIRE(units == 0);
            for (const auto value : buffer)
                REQUIRE(value == canary);
            return units;
        }
        for (std::size_t index = capacity; index < buffer.size(); ++index)
            REQUIRE(buffer[index] == canary);
        REQUIRE(buffer[units] == L'\0');
        REQUIRE(units < capacity);
        return units;
    }
}  // namespace

TEST_CASE("Shared codec decodes every UTF-8 length boundary to its expected scalar", "[utilities][unicode]")
{
    for (const auto& scalar : Scalars)
    {
        CAPTURE(scalar.value);
        std::size_t position = 0;
        CHECK(Decode(scalar.utf8, position) == scalar.value);
        CHECK(position == scalar.utf8.size());
        CHECK(Transcode(scalar.utf8).size() == scalar.units);
    }
}

TEST_CASE("Shared codec and StringUtility agree on valid UTF-8", "[utilities][unicode]")
{
    // This is the guarantee the extraction exists for: the sink and the logging crash path now share
    // one decoder, so they cannot drift apart on well-formed input.
    for (const auto& scalar : Scalars)
    {
        CAPTURE(scalar.value);
        CHECK(Transcode(scalar.utf8) == StringUtility::ToWString(scalar.utf8));
    }
    const std::string_view mixed = "\xce\xbb \xe2\x82\xac \xf0\x9f\x98\x80 tail";
    CHECK(Transcode(mixed) == StringUtility::ToWString(mixed));
    CHECK(TranscodeBack(Transcode(mixed)) == mixed);
}

TEST_CASE("Shared codec rejects every malformed UTF-8 form", "[utilities][unicode]")
{
    CHECK_FALSE(StringUtility::IsValidUtf8("\xc0\x80"));          // Overlong U+0000.
    CHECK_FALSE(StringUtility::IsValidUtf8("\xe0\x80\x80"));      // Overlong U+0000.
    CHECK_FALSE(StringUtility::IsValidUtf8("\xed\xa0\x80"));      // Encoded high surrogate.
    CHECK_FALSE(StringUtility::IsValidUtf8("\xf5\x80\x80\x80"));  // Above U+10FFFF.
    CHECK_FALSE(StringUtility::IsValidUtf8("\x80"));              // Lone continuation byte.
    CHECK_FALSE(StringUtility::IsValidUtf8("\xc2"));              // Truncated two-byte tail.
    CHECK_FALSE(StringUtility::IsValidUtf8("\xe2\x82"));          // Truncated three-byte tail.
    CHECK_FALSE(StringUtility::IsValidUtf8("\xf0\x9f\x98"));      // Truncated four-byte tail.
    CHECK_FALSE(StringUtility::IsValidUtf8("\xe2\x28\xa1"));      // Missing continuation byte.
    CHECK(StringUtility::IsValidUtf8("plain ascii \xce\xbb \xf0\x9f\x98\x80"));

    // IsValidUtf8 covers narrow input only, so the wide rejection is checked through Decode.
    std::size_t position = 0;
    CHECK(Decode(std::wstring_view(L"\xd83d", 1), position) == InvalidCodePoint);
    position = 0;
    CHECK(Decode(std::wstring_view(L"\xdc00", 1), position) == InvalidCodePoint);
    position = 0;
    if constexpr (sizeof(wchar_t) == 2)
    {
        CHECK(Decode(std::wstring_view(L"\xd83d\xde00", 2), position) == 0x01f600);
        CHECK(position == 2);
    }
    else
    {
        CHECK(Decode(std::wstring_view(L"\xd83d\xde00", 2), position) == InvalidCodePoint);
        CHECK(position == 1);
        position = 0;
        CHECK(Decode(std::wstring_view(L"\U0001f600", 1), position) == 0x01f600);
        CHECK(position == 1);
        position                = 0;
        const wchar_t invalid[] = {static_cast<wchar_t>(0x110000)};
        CHECK(Decode(std::wstring_view(invalid, 1), position) == InvalidCodePoint);
    }
}

TEST_CASE("TranscodeToWide substitutes U+FFFD where StringUtility rejects", "[utilities][unicode]")
{
    // The emergency path must never lose a notice to malformed input, so TranscodeToWide replaces each
    // bad scalar rather than reporting failure the way StringUtility::ToWString does.
    const auto invalid = "\xce\xbb\xffz";
    std::wstring wide(16, L'\0');
    const auto units = TranscodeToWide(invalid, wide.data(), wide.size());
    REQUIRE(units == 3);
    CHECK(wide[0] == static_cast<wchar_t>(0x03bb));
    CHECK(wide[1] == static_cast<wchar_t>(ReplacementCharacter));
    CHECK(wide[2] == static_cast<wchar_t>('z'));
    CHECK(wide[units] == L'\0');
    CHECK_THROWS_AS(StringUtility::ToWString(invalid), std::invalid_argument);
}

TEST_CASE("Native wide units never exceed UTF-8 bytes", "[utilities][unicode]")
{
    // The emergency path sizes a fixed buffer from this invariant, so assert it rather than assume it.
    const auto check = [](std::string_view utf8)
    {
        CAPTURE(utf8.size());
        REQUIRE(Transcode(utf8).size() <= utf8.size());
    };
    check(std::string(1024, 'x'));  // Every scalar one unit wide.
    std::string allWide;
    for (int index = 0; index < 256; ++index)
        allWide += "\xf0\x9f\x98\x80";
    check(allWide);  // Supplementary scalars occupy one UTF-32 unit or two UTF-16 units.
    check("mixed \xce\xbb \xe2\x82\xac \xf0\x9f\x98\x80 ascii");
    check(std::string(1020, 'x') + "\xf0\x9f\x98\x80");  // Worst case at the emergency cap.
}

TEST_CASE("TranscodeToWide stays inside its capacity and reserves a terminator", "[utilities][unicode]")
{
    // Canaries on both sides catch any write past the declared capacity, including the surrogate pair
    // that Encode emits without bounds checking.
    const auto atCap             = std::string(1020, 'x') + "\xf0\x9f\x98\x80";
    constexpr auto expectedUnits = 1020 + SupplementaryUnits;
    CHECK(TranscodeGuarded(atCap, 1026) == expectedUnits);  // Full budget, supplementary scalar at the end.
    CHECK(TranscodeGuarded(atCap, 1025) == expectedUnits);  // Still fits: units are below bytes here.
    CHECK(TranscodeGuarded(atCap, 4) == 2);                 // Room for one scalar plus a spare slot.
    CHECK(TranscodeGuarded(atCap, 3) == 1);                 // Exactly one scalar, no surrogate pair.
    CHECK(TranscodeGuarded(atCap, 2) == 0);                 // Too small for the worst case; still terminates.
    CHECK(TranscodeGuarded(atCap, 1) == 0);
    CHECK(TranscodeGuarded(atCap, 0) == 0);  // Writes nothing at all.
    CHECK(TranscodeGuarded("", 8) == 0);
}
