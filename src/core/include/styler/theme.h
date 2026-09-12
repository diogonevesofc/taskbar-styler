// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <styler/selector.h>
#include <styler/style_rule.h>

namespace styler {

struct ThemeRule {
    std::wstring target;  // The original selector text, kept for diagnostics.
    // One or more alternative selector chains, split from `target` on its
    // top-level commas (see SplitTargetString). The rule's styles apply to
    // an element that matches ANY chain here - the comma is an OR, not an
    // AND, mirroring upstream's per-target-part rule expansion.
    std::vector<std::vector<ElementMatcher>> selector;
    std::vector<StyleRule> styles;

    // True when this rule cannot be applied: every selector chain was
    // unparseable (`selector` is empty, so nothing would ever match) or its
    // style list was thrown out because one entry was unparseable (`styles`
    // is empty even though the shipped JSON listed some). The rule stays in
    // `Theme::rules` either way - see theme_loader.h - so consumers MUST
    // check this flag before applying a rule rather than assuming a
    // non-empty `rules` entry is always usable. `Theme::diagnostics` carries
    // one human-readable line per rule this is set on.
    bool dead = false;
};

// The one runtime conditional in the upstream mod: a theme that swaps itself
// for a variant when a Windows feature flag is on.
struct OsFeatureVariant {
    std::uint32_t feature_id = 0;
    std::wstring theme_id;
};

struct Theme {
    std::wstring id;
    std::wstring name;
    std::wstring author;
    std::map<std::wstring, std::wstring> constants;
    std::map<std::wstring, std::wstring> resource_variables;
    std::vector<ThemeRule> rules;
    std::optional<OsFeatureVariant> os_feature_variant;

    // One human-readable line per rule the loader marked `dead`, naming the
    // theme id, the offending target, and why - see theme_loader.h §"fails
    // closed" exceptions. Empty for the overwhelming majority of themes;
    // never silent when non-empty, per spec §7.6.
    std::vector<std::wstring> diagnostics;
};

}  // namespace styler
