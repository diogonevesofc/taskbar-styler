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
// discards just that one target's customization (see
// AddElementCustomizationRules, vendor/upstream/...:18956) rather than
// aborting the theme - unlike upstream, this loader never does it silently:
// the rule is kept in `Theme::rules` with `ThemeRule::dead = true` and one
// line describing why is appended to `Theme::diagnostics` (spec §7.6 requires
// "não aplica ... e reporta"; consumers MUST check `dead` before applying a
// rule):
//   - an unparseable style entry (today: a literal empty string) mirrors
//     upstream's ParseRule throwing "'=' is missing" - the WHOLE style list
//     for that rule is discarded, not just the bad entry, because upstream's
//     per-target catch discards everything already parsed for that target
//     too;
//   - a selector chain with more than one `#Name` (AmbiguousMatcherError, a
//     narrower ParseError) is dropped chain-by-chain by ParseSelectorGroups;
//     sibling chains from the same comma-separated target still parse and
//     still apply. Only when every chain of a target is bad does `selector`
//     end up empty, meaning the rule matches nothing.
// Either way the rule stays in `Theme::rules`, so the rule count some tools
// rely on (see test_corpus.cpp) still reflects one entry per shipped
// ThemeTargetStyles block. Every other malformed rule still fails the whole
// theme closed, including every other selector error (empty type, unmatched
// bracket, ...).
Theme LoadThemeFromJson(std::string_view utf8);

Theme LoadThemeFromFile(const std::filesystem::path& path);

}  // namespace styler
