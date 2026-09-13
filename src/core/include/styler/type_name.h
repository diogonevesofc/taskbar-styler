// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

namespace styler {

// Expands the short type names theme JSON uses (`Grid`) into the runtime
// class names the visual tree reports (`Windows.UI.Xaml.Controls.Grid`),
// mirroring upstream's AdjustTypeName
// (vendor/upstream/windows-11-taskbar-styler.wh.cpp:18795). The export in
// visual-tree.txt already shows full names; this is what lets a selector
// written against either form match.
std::wstring AdjustTypeName(std::wstring_view type);

}  // namespace styler
