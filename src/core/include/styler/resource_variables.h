// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <map>
#include <string>
#include <vector>

namespace styler {

enum class ResourceTheme { None, Dark, Light };
enum class ResourceValueType { String, Xaml, ThemeResourceReference };

// One `resourceVariables` entry, key syntax decoded: `Name` overrides an
// existing application resource (converted to its existing type at merge
// time); `Name@Dark` / `Name@Light` adds to a theme dictionary; a trailing
// `:` on the name means the value is XAML; a `{ThemeResource Key}` value is
// a reference resolved at merge time (and refreshed when system colours
// change). Mirrors upstream ParseResourceVariable (vendor:19000-19080).
struct ResourceVariable {
    std::wstring key;
    std::wstring value;
    ResourceTheme theme = ResourceTheme::None;
    ResourceValueType type = ResourceValueType::String;
};

std::vector<ResourceVariable> ParseResourceVariables(
    const std::map<std::wstring, std::wstring>& variables,
    std::vector<std::wstring>* diagnostics);

}  // namespace styler
