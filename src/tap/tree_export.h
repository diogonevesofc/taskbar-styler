// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace styler::tap {

// A snapshot of one element, in the same vocabulary the theme selectors use, so
// a line of the export can be pasted into a theme JSON with no translation.
struct TreeNode {
    std::wstring type;
    std::wstring name;
    int one_based_index = 0;  // 0 when the element needs no index to be unique.
    std::vector<TreeNode> children;
};

// Fills one_based_index only where a type repeats among siblings. An index on a
// unique child would be noise in a selector. Pure.
void AssignSiblingIndices(TreeNode& parent);

// Pure: no XAML, no COM. This is the part worth unit testing.
std::wstring FormatTree(const TreeNode& root);

// Walks the live visual tree and writes the formatted result.
HRESULT ExportTreeToFile(const std::wstring& path);

}  // namespace styler::tap
