// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace styler {

// Constants ready for substitution: nested `$Name` references already
// expanded, sorted by name length descending so the longest name that
// prefixes the text after a `$` wins - exactly upstream's LoadStyleConstants
// order (vendor/upstream/windows-11-taskbar-styler.wh.cpp:18580). 14 shipped
// themes have constant names that prefix one another (Aeris themeColor /
// themeColorOpacity, Matter overlay / overlay2, ...) and reference the long
// one; any other order would splice the short value into the long name.
using ResolvedConstants = std::vector<std::pair<std::wstring, std::wstring>>;

ResolvedConstants ResolveConstants(
    const std::map<std::wstring, std::wstring>& constants);

// Replaces every `$Name` whose Name is a prefix-match of a resolved constant
// with its value, anywhere in `text`. A `$` matching no constant stays as a
// literal - three shipped themes rely on that (spec section 7.6), so this is
// deliberately not an error.
std::wstring ApplyStyleConstants(std::wstring_view text,
                                 const ResolvedConstants& constants);

}  // namespace styler
