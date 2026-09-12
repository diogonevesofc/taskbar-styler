// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string_view>

namespace styler {

// Version of the theme JSON schema this build understands.
inline constexpr int kThemeSchemaVersion = 1;

std::wstring_view CoreVersion();

}  // namespace styler
