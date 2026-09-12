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
//
// Two narrow exceptions mirror upstream's own resilience instead of failing
// the whole file, both verified against the real 55-theme corpus and both
// cases where upstream's own C++ implementation throws internally and
// silently discards just that one rule's customization (see
// AddElementCustomizationRules, vendor/upstream/...:18956) rather than
// aborting the theme:
//   - a literal empty style string (skipped; the rule keeps its other
//     styles, if any);
//   - a selector segment with more than one `#Name` (AmbiguousMatcherError,
//     a narrower ParseError - the rule is kept with an empty `selector`, so
//     it matches nothing).
// Either way the rule stays in `Theme::rules`, so the rule count some tools
// rely on (see test_corpus.cpp) still reflects one entry per shipped
// ThemeTargetStyles block. Every other malformed rule still fails closed,
// including every other selector error (empty type, unmatched bracket, ...).
Theme LoadThemeFromJson(std::string_view utf8);

Theme LoadThemeFromFile(const std::filesystem::path& path);

}  // namespace styler
