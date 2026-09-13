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

// One Add report from the mutation stream, as tree_export.cpp's
// SnapshotCallback collects it. The handle fields are `unsigned long long`,
// not `InstanceHandle` (xamlom.h's typedef for the same 64-bit value) - this
// header stays free of XAML/COM includes so BuildForest can live in
// tree_format.cpp, in the static lib the tests link against.
struct Reported {
    unsigned long long handle = 0;
    unsigned long long parent = 0;
    unsigned int child_index = 0;
    std::wstring type;
    std::wstring name;
};

// Builds the forest from the reported parent/child pairs. An element whose
// parent handle was never itself reported is a root - the stream reports the
// taskbar's hosts without reporting whatever contains them. A handle
// reported more than once keeps only its first report; later ones are
// dropped. A cycle among the reported relations (each element's parent is
// itself, directly or transitively, one of its descendants) leaves every
// member of the cycle unreachable from any root - see the function body for
// why - which shows up as those elements simply missing from the result, not
// as an infinite loop. Pure.
std::vector<TreeNode> BuildForest(const std::vector<Reported>& reported);

// Fills one_based_index only where a type repeats among siblings. An index on a
// unique child would be noise in a selector. Pure.
void AssignSiblingIndices(TreeNode& parent);

// Pure: no XAML, no COM. This is the part worth unit testing.
std::wstring FormatTree(const TreeNode& root);

// Walks the live visual tree and writes the formatted result.
HRESULT ExportTreeToFile(const std::wstring& path);

}  // namespace styler::tap
