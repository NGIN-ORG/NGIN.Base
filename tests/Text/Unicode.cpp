#include <NGIN/Text/Unicode.hpp>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <vector>

TEST_CASE("Unicode validates UTF-8 and UTF-16 sequences", "[Text][Unicode]")
{
    using namespace NGIN::Text::Unicode;

    CHECK(IsValidUtf8("plain ascii"));
    CHECK(IsValidUtf8(std::string_view("\xC3\xA5\xC3\xA4\xC3\xB6", 6)));
    CHECK_FALSE(IsValidUtf8(std::string("\xC0\xAF", 2)));
    CHECK_FALSE(IsValidUtf8(std::string("\xED\xA0\x80", 3)));
    CHECK_FALSE(IsValidUtf8(std::string("\xF4\x90\x80\x80", 4)));
    CHECK_FALSE(IsValidUtf8(std::string("\xE2\x82", 2)));

    CHECK(IsValidUtf16(u"hello"));
    CHECK(IsValidUtf16(u"\U0001F600"));
    CHECK_FALSE(IsValidUtf16(std::u16string {static_cast<char16_t>(0xD800)}));
    CHECK_FALSE(IsValidUtf16(std::u16string {static_cast<char16_t>(0xDC00)}));

    CHECK(IsValidUtf32(U"hello"));
    CHECK_FALSE(IsValidUtf32(std::u32string {static_cast<char32_t>(0xD800)}));
    CHECK_FALSE(IsValidUtf32(std::u32string {static_cast<char32_t>(0x110000)}));
}

TEST_CASE("Unicode decode primitives report detailed failures", "[Text][Unicode]")
{
    using namespace NGIN::Text::Unicode;

    const auto utf8 = DecodeUtf8(std::string_view("\xF0\x9F\x98\x80", 4));
    REQUIRE(utf8.error == EncodingError::None);
    CHECK(utf8.unitsConsumed == 4U);
    CHECK(utf8.codePoint == U'😀');

    const auto truncated = DecodeUtf8(std::string_view("\xE2\x82", 2));
    CHECK(truncated.error == EncodingError::UnexpectedEnd);

    const auto overlong = DecodeUtf8(std::string_view("\xC0\xAF", 2));
    CHECK(overlong.error == EncodingError::OverlongSequence);

    const std::u16string surrogateUnits {static_cast<char16_t>(0xD834), static_cast<char16_t>(0xDD1E)};
    const auto           surrogate = DecodeUtf16(std::u16string_view {surrogateUnits.data(), surrogateUnits.size()});
    REQUIRE(surrogate.error == EncodingError::None);
    CHECK(surrogate.codePoint == U'𝄞');

    const std::u16string unpairedUnits {static_cast<char16_t>(0xD834), u'A'};
    const auto           unpaired = DecodeUtf16(std::u16string_view {unpairedUnits.data(), unpairedUnits.size()});
    CHECK(unpaired.error == EncodingError::UnpairedSurrogate);
}

TEST_CASE("Unicode conversion policies are explicit", "[Text][Unicode]")
{
    using namespace NGIN::Text::Unicode;

    const std::string invalidUtf8("\xF0\x28\x8C\x28", 4);

    const auto strict = ToUtf32(invalidUtf8, ErrorPolicy::Strict);
    REQUIRE_FALSE(strict.HasValue());
    CHECK(strict.Error().error == EncodingError::InvalidSequence);
    CHECK(strict.Error().inputOffset == 0U);

    const auto replace = ToUtf32(invalidUtf8, ErrorPolicy::Replace);
    REQUIRE(replace.HasValue());
    CHECK(replace.Value().Size() == 4U);
    CHECK(replace.Value()[0] == static_cast<char32_t>(0xFFFD));
    CHECK(replace.Value()[1] == U'(');
    CHECK(replace.Value()[2] == static_cast<char32_t>(0xFFFD));
    CHECK(replace.Value()[3] == U'(');

    const auto skip = ToUtf32(invalidUtf8, ErrorPolicy::Skip);
    REQUIRE(skip.HasValue());
    CHECK(skip.Value().Size() == 2U);
    CHECK(skip.Value()[0] == U'(');
    CHECK(skip.Value()[1] == U'(');
}

TEST_CASE("Unicode round-trips UTF-8 UTF-16 and UTF-32", "[Text][Unicode]")
{
    using namespace NGIN::Text::Unicode;

    const std::string source("Hello \xC3\xA5\xF0\x9F\x98\x80", 12);

    const auto utf16 = ToUtf16(source, ErrorPolicy::Strict);
    REQUIRE(utf16.HasValue());

    const auto utf32 = ToUtf32(source, ErrorPolicy::Strict);
    REQUIRE(utf32.HasValue());

    const auto backToUtf8From16 = ToUtf8(std::u16string_view {utf16.Value().Data(), utf16.Value().Size()}, ErrorPolicy::Strict);
    REQUIRE(backToUtf8From16.HasValue());
    CHECK(backToUtf8From16.Value().View() == source);

    const auto backToUtf8From32 = ToUtf8(std::u32string_view {utf32.Value().Data(), utf32.Value().Size()}, ErrorPolicy::Strict);
    REQUIRE(backToUtf8From32.HasValue());
    CHECK(backToUtf8From32.Value().View() == source);
}

TEST_CASE("Unicode UTF-8 view iterates code points and tracks strict errors", "[Text][Unicode]")
{
    using namespace NGIN::Text::Unicode;

    Utf8View valid(std::string_view("A\xF0\x9F\x98\x80\xC3\xA5", 7));
    std::vector<CodePoint> codePoints;
    for (CodePoint cp: valid)
        codePoints.push_back(cp);

    REQUIRE(codePoints.size() == 3U);
    CHECK(codePoints[0] == U'A');
    CHECK(codePoints[1] == U'😀');
    CHECK(codePoints[2] == U'å');
    CHECK_FALSE(valid.HasError());

    Utf8View strictInvalid(std::string_view("\xE2\x82", 2), ErrorPolicy::Strict);
    CHECK(strictInvalid.HasError());
    CHECK(strictInvalid.GetError().error == EncodingError::UnexpectedEnd);

    const char        invalidWithSuffixBytes[] {static_cast<char>(0xE2), static_cast<char>(0x82), 'A'};
    const std::string invalidWithSuffix(invalidWithSuffixBytes, sizeof(invalidWithSuffixBytes));
    Utf8View          replaceInvalid(invalidWithSuffix, ErrorPolicy::Replace);
    std::vector<CodePoint> replaced;
    for (CodePoint cp: replaceInvalid)
        replaced.push_back(cp);
    REQUIRE(replaced.size() == 2U);
    CHECK(replaced[0] == static_cast<CodePoint>(0xFFFD));
    CHECK(replaced[1] == U'A');

    Utf8View skipInvalid(invalidWithSuffix, ErrorPolicy::Skip);
    std::vector<CodePoint> skipped;
    for (CodePoint cp: skipInvalid)
        skipped.push_back(cp);
    REQUIRE(skipped.size() == 1U);
    CHECK(skipped[0] == U'A');
}

TEST_CASE("Unicode helpers count code points and handle BOMs", "[Text][Unicode]")
{
    using namespace NGIN::Text::Unicode;

    const auto count = CountCodePoints(std::string_view("A\xF0\x9F\x98\x80\xC3\xA5", 7));
    REQUIRE(count.HasValue());
    CHECK(count.Value() == 3U);

    CHECK(IsAscii("ASCII"));
    CHECK_FALSE(IsAscii(std::string_view("\xC3\xA5", 2)));

    const std::array<NGIN::Byte, 3> utf8Bom {
            static_cast<NGIN::Byte>(0xEF),
            static_cast<NGIN::Byte>(0xBB),
            static_cast<NGIN::Byte>(0xBF)};
    const BomInfo bom = DetectBom(std::span<const NGIN::Byte> {utf8Bom});
    CHECK(bom.kind == BomKind::Utf8);
    CHECK(bom.bytes == 3U);
    CHECK(StripBom(std::string_view("\xEF\xBB\xBFtext", 7)) == "text");
}
