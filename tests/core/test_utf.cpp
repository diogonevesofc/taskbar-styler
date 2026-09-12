// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <initializer_list>
#include <string>

#include <styler/utf.h>

using styler::Utf8ToWide;
using styler::WideToUtf8;

namespace {

// Builds a std::string from raw byte values, sidestepping any ambiguity
// about how the compiler's source/execution character set would treat a
// string literal containing high-bit bytes.
std::string Bytes(std::initializer_list<unsigned char> bytes) {
    return std::string(bytes.begin(), bytes.end());
}

}  // namespace

TEST_CASE("round-trips an astral code point both directions") {
    // U+1F600 GRINNING FACE: 4-byte UTF-8, surrogate pair in UTF-16.
    auto utf8 = Bytes({0xF0, 0x9F, 0x98, 0x80});
    auto wide = Utf8ToWide(utf8);
    CHECK(wide == std::wstring(L"\xD83D\xDE00"));
    CHECK(WideToUtf8(wide) == utf8);
}

TEST_CASE("round-trips the U+10FFFF boundary") {
    // The highest valid Unicode scalar value.
    auto utf8 = Bytes({0xF4, 0x8F, 0xBF, 0xBF});
    auto wide = Utf8ToWide(utf8);
    CHECK(wide == std::wstring(L"\xDBFF\xDFFF"));
    CHECK(WideToUtf8(wide) == utf8);
}

// NOTE: these use \x hex escapes for U+FFFD, not \u. MSVC's \u
// universal-character-name in a literal is funneled through the execution
// character set's narrow<->wide conversion, and without an explicit
// /utf-8 flag (not set in this project's CMake) that pipeline mojibakes
// non-ASCII code points — verified independently: L'�' compiled to
// U+00EF here, not U+FFFD. \x escapes store the numeric value directly with
// no such conversion, so they are used throughout this file instead.

TEST_CASE("replaces out-of-range and overlong sequences with U+FFFD") {
    // Lead byte 0xF5+ decodes past U+10FFFF.
    CHECK(Utf8ToWide(Bytes({0xF5, 0x80, 0x80, 0x80})) ==
          std::wstring(1, L'\xFFFD'));
    // One past the maximum valid scalar value, U+110000.
    CHECK(Utf8ToWide(Bytes({0xF4, 0x90, 0x80, 0x80})) ==
          std::wstring(1, L'\xFFFD'));
    // Overlong 2-byte encoding of NUL — must not smuggle an embedded NUL
    // into the decoded string.
    CHECK(Utf8ToWide(Bytes({0xC0, 0x80})) == std::wstring(1, L'\xFFFD'));
    // Overlong 2-byte encoding of '/' — must not smuggle a path separator
    // past a caller that filtered on the decoded string.
    CHECK(Utf8ToWide(Bytes({0xC0, 0xAF})) == std::wstring(1, L'\xFFFD'));
}

TEST_CASE("replaces a UTF-8-encoded surrogate code point with U+FFFD") {
    // 3-byte encoding of U+D800, a lone high surrogate. UTF-8 must never
    // encode a surrogate code point directly. This is the third of the
    // guard's three rejection classes (overlong, surrogate, out-of-range);
    // the other two are covered above.
    CHECK(Utf8ToWide(Bytes({0xED, 0xA0, 0x80})) == std::wstring(1, L'\xFFFD'));
}

TEST_CASE("a sequence truncated at end of input keeps the preceding text") {
    // 'a' then a 3-byte lead byte with only one of its two continuation
    // bytes present.
    auto result = Utf8ToWide(Bytes({'a', 0xE2, 0x82}));
    CHECK(result == std::wstring(L"a\xFFFD\xFFFD"));
}

// Despite the old name ("a bad continuation byte does not cascade into
// later text"), this case never reaches the per-byte continuation check at
// all: 0xE9 is a 3-byte lead byte demanding 2 more bytes, but only one byte
// ('m') remains before the end of input, so the truncation guard
// (`i + extra >= size`) fires first. What this actually proves is that a
// lead byte too close to the end of input is treated as truncated rather
// than reading past the end or misreading trailing text as its
// continuation bytes.
TEST_CASE("a lead byte too close to the end of input is truncated, not misread") {
    // "Ningu" + a lone Latin-1 'é' byte (0xE9) fed as if it were UTF-8,
    // followed by 'm'. The trailing 'm' must survive intact.
    auto result = Utf8ToWide(Bytes({'N', 'i', 'n', 'g', 'u', 0xE9, 'm'}));
    CHECK(result == std::wstring(L"Ningu\xFFFDm"));
}

// The real "bad continuation byte mid-sequence" case, with plenty of bytes
// remaining before the end of input: 0xE2 is a 3-byte lead byte, but '('
// (0x28) is not a continuation byte, so decoding fails after just one byte
// (k=1). The code sets `extra = k - 1` (here 0) instead of skipping the two
// bytes it expected, so the byte that broke the sequence is RESCANNED as
// the start of the next character rather than being swallowed.
TEST_CASE("a bad continuation byte mid-sequence rescans from the offending byte") {
    auto result = Utf8ToWide(Bytes({0xE2, 0x28, 0xA1}));
    // 0xE2 -> U+FFFD (bad continuation). '(' is rescanned as plain ASCII.
    // 0xA1 is then read fresh as a lead byte, but it is itself invalid as
    // one (a lone continuation-shaped byte) -> U+FFFD.
    CHECK(result == std::wstring(L"\xFFFD(\xFFFD"));
}

TEST_CASE("an unpaired high surrogate does not crash WideToUtf8") {
    std::wstring lone_high(1, static_cast<wchar_t>(0xD800));
    auto result = WideToUtf8(lone_high);
    CHECK(result == Bytes({0xED, 0xA0, 0x80}));
}
