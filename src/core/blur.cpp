// SPDX-License-Identifier: GPL-3.0-or-later
#include <styler/blur.h>

#include <algorithm>
#include <cstdlib>
#include <vector>

#include <styler/selector.h>  // ParseError

#include "detail/text.h"

namespace styler {
namespace {

// Narrows each wchar_t to its low byte: attribute names and XML syntax in
// this domain are ASCII, and the message round-trips through Utf8ToWide at
// the catch site (matcher.cpp) for callers that need a diagnostic wstring.
// A plain iterator-range append() does the same truncation implicitly and
// trips /W4 C4244; this makes it explicit instead.
void AppendNarrow(std::string& out, std::wstring_view s) {
    for (wchar_t c : s) {
        out += static_cast<char>(c);
    }
}

[[noreturn]] void Fail(std::wstring_view what, std::wstring_view detail) {
    std::string msg = "WindhawkBlur: ";
    AppendNarrow(msg, what);
    msg += " '";
    AppendNarrow(msg, detail);
    msg += "'";
    throw ParseError(msg);
}

float ParseFloat(std::wstring_view text) {
    std::wstring owned(text);
    wchar_t* end = nullptr;
    // wcstod, not stof: it reports where it stopped, so trailing junk
    // ("18px", "1,0") is rejected instead of silently truncated. The C
    // locale is the process default here and the corpus has no comma
    // decimals, so no locale juggling is needed.
    double v = std::wcstod(owned.c_str(), &end);
    if (end == owned.c_str() || *end != L'\0') {
        Fail(L"bad number", text);
    }
    return static_cast<float>(v);
}

int HexDigit(wchar_t c, std::wstring_view whole) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    Fail(L"bad hex color", whole);
}

std::uint8_t HexPair(std::wstring_view digits, size_t i,
                     std::wstring_view whole) {
    return static_cast<std::uint8_t>(HexDigit(digits[i], whole) * 16 +
                                     HexDigit(digits[i + 1], whole));
}

// #RGB, #ARGB, #RRGGBB, #AARRGGBB - the four forms XAML itself accepts. The
// 3- and 4-digit forms expand each nibble by duplication (0x8 -> 0x88), which
// is XAML's own rule.
BlurColor ParseColor(std::wstring_view text) {
    if (text.empty() || text.front() != L'#') {
        Fail(L"color must start with #", text);
    }
    std::wstring_view d = text.substr(1);
    auto nibble = [&](size_t k) -> std::uint8_t {
        return static_cast<std::uint8_t>(HexDigit(d[k], text) * 17);
    };
    BlurColor c{};
    switch (d.size()) {
        case 3:
            c.a = 0xFF;
            c.r = nibble(0);
            c.g = nibble(1);
            c.b = nibble(2);
            return c;
        case 4:
            c.a = nibble(0);
            c.r = nibble(1);
            c.g = nibble(2);
            c.b = nibble(3);
            return c;
        case 6:
            c.a = 0xFF;
            c.r = HexPair(d, 0, text);
            c.g = HexPair(d, 2, text);
            c.b = HexPair(d, 4, text);
            return c;
        case 8:
            c.a = HexPair(d, 0, text);
            c.r = HexPair(d, 2, text);
            c.g = HexPair(d, 4, text);
            c.b = HexPair(d, 6, text);
            return c;
        default:
            Fail(L"color must have 3, 4, 6 or 8 hex digits", text);
    }
}

// Splits `Name="value"` pairs. Quoted values may contain spaces, which is why
// this is a small scanner and not SplitStringView on ' ' - upstream's
// space-split needs a two-state machine to survive
// `TintColor="{ThemeResource X}"` (vendor:15200-15260); scanning to the
// closing quote has no such state.
struct Attribute {
    std::wstring_view name;
    std::wstring_view value;  // Without the quotes.
};

std::vector<Attribute> SplitAttributes(std::wstring_view body) {
    std::vector<Attribute> out;
    size_t i = 0;
    while (i < body.size()) {
        while (i < body.size() && (body[i] == L' ' || body[i] == L'\t' ||
                                   body[i] == L'\r' || body[i] == L'\n')) {
            ++i;
        }
        if (i >= body.size()) {
            break;
        }
        size_t name_start = i;
        while (i < body.size() && body[i] != L'=' && body[i] != L' ') {
            ++i;
        }
        std::wstring_view name = body.substr(name_start, i - name_start);
        while (i < body.size() && body[i] == L' ') {
            ++i;
        }
        if (i >= body.size() || body[i] != L'=') {
            Fail(L"attribute without a value", name);
        }
        ++i;
        if (i >= body.size() || body[i] != L'"') {
            Fail(L"attribute value must be quoted", name);
        }
        ++i;
        size_t value_start = i;
        size_t close = body.find(L'"', i);
        if (close == std::wstring_view::npos) {
            Fail(L"unterminated attribute value", name);
        }
        out.push_back({name, body.substr(value_start, close - value_start)});
        i = close + 1;
    }
    return out;
}

// `{ThemeResource Key}` -> Key, or empty when the value is not one.
std::wstring_view ThemeResourceKey(std::wstring_view value) {
    constexpr std::wstring_view kPrefix = L"{ThemeResource";
    if (!value.starts_with(kPrefix) || !value.ends_with(L"}")) {
        return {};
    }
    std::wstring_view key = detail::Trim(
        value.substr(kPrefix.size(), value.size() - kPrefix.size() - 1));
    if (key.empty()) {
        Fail(L"empty theme resource key", value);
    }
    return key;
}

}  // namespace

std::optional<BlurSpec> ParseWindhawkBlur(std::wstring_view value) {
    std::wstring_view s = detail::Trim(value);
    std::wstring_view body = detail::MatchBlurTagBody(s);
    if (body.empty()) {
        return std::nullopt;
    }
    if (!body.ends_with(L"/>")) {
        Fail(L"element must be self-closing", s);
    }
    body = body.substr(0, body.size() - 2);

    BlurSpec spec;
    std::optional<float> tint_opacity;
    for (const Attribute& attr : SplitAttributes(body)) {
        if (attr.name == L"BlurAmount") {
            spec.blur_amount = ParseFloat(attr.value);
        } else if (attr.name == L"TintColor") {
            if (auto key = ThemeResourceKey(attr.value); !key.empty()) {
                spec.tint_theme_resource = std::wstring(key);
            } else {
                spec.tint = ParseColor(attr.value);
            }
        } else if (attr.name == L"TintOpacity") {
            tint_opacity = ParseFloat(attr.value);
        } else if (attr.name == L"TintLuminosityOpacity") {
            spec.tint_luminosity_opacity = ParseFloat(attr.value);
        } else if (attr.name == L"TintSaturation") {
            spec.tint_saturation = ParseFloat(attr.value);
        } else if (attr.name == L"NoiseOpacity") {
            spec.noise_opacity = ParseFloat(attr.value);
        } else if (attr.name == L"NoiseDensity") {
            spec.noise_density = ParseFloat(attr.value);
        } else if (attr.name == L"FallbackColor") {
            if (auto key = ThemeResourceKey(attr.value); !key.empty()) {
                spec.fallback_theme_resource = std::wstring(key);
            } else {
                spec.fallback_color = ParseColor(attr.value);
            }
        } else {
            Fail(L"unknown attribute", attr.name);
        }
    }

    // Same fold upstream does (vendor:15355-15365): TintOpacity overrides the
    // alpha the author may also have written into TintColor.
    if (tint_opacity) {
        float clamped = std::clamp(*tint_opacity, 0.0f, 1.0f);
        spec.tint.a = static_cast<std::uint8_t>(clamped * 255.0f);
        spec.tint_opacity = spec.tint.a;
    }
    return spec;
}

}  // namespace styler
