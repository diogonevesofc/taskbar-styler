// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/tree_export.h>

#include <cstdio>

namespace styler::tap {
namespace {

void AppendNode(std::wstring& out, const TreeNode& node, int depth) {
    out.append(static_cast<size_t>(depth) * 2, L' ');
    out += node.type;

    if (!node.name.empty()) {
        out += L'#';
        out += node.name;
    }
    if (node.one_based_index > 0) {
        wchar_t idx[16]{};
        swprintf_s(idx, L"[%d]", node.one_based_index);
        out += idx;
    }
    out += L'\n';

    for (const auto& child : node.children) {
        AppendNode(out, child, depth + 1);
    }
}

}  // namespace

void AssignSiblingIndices(TreeNode& parent) {
    for (size_t i = 0; i < parent.children.size(); i++) {
        int seen = 0;
        int position = 0;
        for (size_t j = 0; j < parent.children.size(); j++) {
            if (parent.children[j].type == parent.children[i].type) {
                seen++;
                if (j == i) {
                    position = seen;
                }
            }
        }
        parent.children[i].one_based_index = (seen > 1) ? position : 0;
    }
    for (auto& child : parent.children) {
        AssignSiblingIndices(child);
    }
}

std::wstring FormatTree(const TreeNode& root) {
    std::wstring out;
    AppendNode(out, root, 0);
    return out;
}

}  // namespace styler::tap
