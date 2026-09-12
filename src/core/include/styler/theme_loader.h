// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <filesystem>
#include <string_view>

#include <styler/theme.h>

namespace styler {

// Parses a theme and validates what can be validated statically: the JSON
// shape, every selector, and every style rule. Throws ParseError on any of
// those — a half-parsed theme is worse than none, so loading fails closed.
//
// It deliberately does NOT validate `$Constant` references. Upstream resolves
// them by prefix substitution anywhere in a value and lets an unmatched `$`
// through as a literal; three shipped themes rely on that. Resolution happens
// at apply time, not load time.
Theme LoadThemeFromJson(std::string_view utf8);

Theme LoadThemeFromFile(const std::filesystem::path& path);

}  // namespace styler
