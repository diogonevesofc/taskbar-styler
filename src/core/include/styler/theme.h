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
    std::vector<ElementMatcher> selector;
    std::vector<StyleRule> styles;
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
};

}  // namespace styler
