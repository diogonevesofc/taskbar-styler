// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

namespace styler {

// ponytail: interim approximation. `<WindhawkBlur .../>` is upstream's own
// composition brush (blur + tint + noise), ~1000 lines that Plano 3b ports.
// Until then it is rewritten to a stock AcrylicBrush keeping the attributes
// the two share by name - TintColor, TintOpacity, TintLuminosityOpacity,
// FallbackColor - and dropping BlurAmount, TintSaturation, NoiseOpacity and
// NoiseDensity. `<Blur .../>` is accepted as a synonym (spec section 5.3).
// `*rewritten` reports whether anything changed so the caller can count how
// many styles run on the approximation. Any other value passes through.
std::wstring RewriteWindhawkBlur(std::wstring_view value, bool* rewritten);

}  // namespace styler
