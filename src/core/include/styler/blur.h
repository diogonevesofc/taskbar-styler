// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace styler {

// One `<WindhawkBlur .../>` pseudo-element, parsed. Field names and optional-
// ness mirror upstream's XamlBlurBrushParams (vendor:15373-15392) so the TAP
// side reads like the reference implementation.
//
// Colors are stored as straight ARGB bytes rather than a WinRT Color, since
// styler_core may not include winrt/. `tint_theme_resource` and
// `fallback_theme_resource` hold the KEY from `TintColor="{ThemeResource X}"`;
// when one is set the matching color field is a placeholder the TAP replaces
// with the live theme color.
struct BlurColor {
    std::uint8_t a = 0;
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;

    friend bool operator==(const BlurColor&, const BlurColor&) = default;
};

struct BlurSpec {
    float blur_amount = 0.0f;
    BlurColor tint{};
    // Engaged when the markup carried TintOpacity: the value has already been
    // folded into `tint.a`, and the TAP re-applies it after resolving a theme
    // resource tint (whose own alpha must not win over the author's).
    std::optional<std::uint8_t> tint_opacity;
    std::wstring tint_theme_resource;
    std::optional<float> tint_luminosity_opacity;
    std::optional<float> tint_saturation;
    std::optional<float> noise_opacity;
    std::optional<float> noise_density;
    std::optional<BlurColor> fallback_color;
    std::wstring fallback_theme_resource;

    friend bool operator==(const BlurSpec&, const BlurSpec&) = default;
};

// Parses `<WindhawkBlur .../>` or its `<Blur .../>` synonym (spec section
// 5.3). Returns nullopt when `value` is not a blur element at all - that is
// the overwhelming majority of style values and is not an error. Throws
// styler::ParseError when the value IS a blur element but is malformed
// (unknown attribute, unterminated theme resource, unparseable number), so a
// typo fails the rule closed instead of silently rendering a different brush.
std::optional<BlurSpec> ParseWindhawkBlur(std::wstring_view value);

}  // namespace styler
