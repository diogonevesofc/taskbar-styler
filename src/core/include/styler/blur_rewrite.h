// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

namespace styler {

// The documented FALLBACK for `<WindhawkBlur .../>`. The primary path is
// ParseWindhawkBlur + the real composition brush (src/tap/blur_brush.h); this
// rewrite to a stock AcrylicBrush is what a style falls back to when the
// brush cannot be created - no Compositor for the element, an effect factory
// that refuses the graph, or a machine where Windows.UI.Composition is
// unavailable. It keeps the attributes the two brushes share by name -
// TintColor, TintOpacity, TintLuminosityOpacity, FallbackColor - and drops
// BlurAmount, TintSaturation, NoiseOpacity and NoiseDensity. `<Blur .../>` is
// accepted as a synonym (spec section 5.3). `*rewritten` reports whether
// anything changed, so the caller can count how many styles are running on
// the approximation. Any other value passes through.
std::wstring RewriteWindhawkBlur(std::wstring_view value, bool* rewritten);

}  // namespace styler
