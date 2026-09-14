// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/tree_export.h>

#include <algorithm>
#include <cstdio>
#include <map>

#include <tap/log.h>

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

// Bounds the recursion in Materialise, as cheap insurance against stack death
// on a pathological real tree. This is NOT cycle protection: BuildForest's
// kids[] arrays give every reported element at most one parent, so the
// parent relation is a function and a cycle has no member reachable from any
// root in the first place (see BuildForest below) - recursion here never
// even reaches a cycle. The real, and only, symptom of a cycle in the
// reported relations is an empty forest (and hence empty output), which
// ExportTreeToFile treats as failure. A genuinely 129-deep real tree would
// silently truncate here instead - nothing in the taskbar comes close.
constexpr int kMaxDepth = 128;

TreeNode Materialise(size_t i, const std::vector<Reported>& reported,
                      const std::vector<std::vector<size_t>>& kids,
                      int depth) {
    TreeNode node;
    node.type = reported[i].type;
    node.name = reported[i].name;
    if (depth >= kMaxDepth) {
        STYLER_LOG(LogLevel::Error, L"tree depth cap %d hit at %s", kMaxDepth,
                   node.type.c_str());
        return node;
    }
    for (size_t k : kids[i]) {
        node.children.push_back(Materialise(k, reported, kids, depth + 1));
    }
    return node;
}

}  // namespace

std::vector<TreeNode> BuildForest(const std::vector<Reported>& reported) {
    std::map<unsigned long long, size_t> index_of;
    for (size_t i = 0; i < reported.size(); ++i) {
        // First report of a handle wins: a handle reported twice would
        // otherwise leave the first copy's children pointing at a stale node.
        index_of.emplace(reported[i].handle, i);
    }

    std::vector<std::vector<size_t>> kids(reported.size());
    std::vector<size_t> roots;
    for (size_t i = 0; i < reported.size(); ++i) {
        if (index_of[reported[i].handle] != i) {
            continue;  // Duplicate report of an earlier handle.
        }
        auto parent = index_of.find(reported[i].parent);
        if (reported[i].parent == 0 || parent == index_of.end()) {
            roots.push_back(i);
        } else {
            // Each index is pushed into at most one kids[] list, so the
            // parent relation this builds is a function: every reported
            // element has exactly one parent slot. A cycle in the reported
            // relations (i is an ancestor of the element whose handle i's
            // own parent field names) therefore has no entry point - no
            // cycle member is ever a root, and no root's descent ever
            // reaches back into one - so it is simply absent from `forest`
            // below, not an infinite loop.
            kids[parent->second].push_back(i);
        }
    }

    for (std::vector<size_t>& k : kids) {
        std::stable_sort(k.begin(), k.end(), [&](size_t a, size_t b) {
            return reported[a].child_index < reported[b].child_index;
        });
    }

    std::vector<TreeNode> forest;
    for (size_t r : roots) {
        forest.push_back(Materialise(r, reported, kids, 0));
    }
    return forest;
}

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

std::vector<std::wstring> DescribeIncompleteTree(
    const std::vector<Reported>& reported) {
    // Count how many children each reported handle actually received. A
    // handle reported twice keeps its first report (same rule BuildForest
    // uses): the declared count comes from that first entry, and so does its
    // contribution to the parent's delivered count - a duplicate report of
    // the same child is skipped entirely, not counted as a second child, or
    // the function would stay quiet on a batch that truncated after
    // re-reporting one child instead of delivering the next one.
    std::map<unsigned long long, unsigned int> declared;
    std::map<unsigned long long, unsigned int> delivered;
    std::vector<unsigned long long> order;
    for (const auto& r : reported) {
        if (declared.emplace(r.handle, r.num_children).second) {
            order.push_back(r.handle);
            if (r.parent != 0) {
                ++delivered[r.parent];
            }
        }
    }

    std::vector<std::wstring> out;
    for (unsigned long long handle : order) {
        unsigned int want = declared[handle];
        auto it = delivered.find(handle);
        unsigned int got = it == delivered.end() ? 0u : it->second;
        if (got >= want) {
            continue;
        }
        // Find the element's own report for a readable name.
        std::wstring label;
        for (const auto& r : reported) {
            if (r.handle == handle) {
                label = r.type;
                if (!r.name.empty()) {
                    label += L'#';
                    label += r.name;
                }
                break;
            }
        }
        wchar_t buf[64]{};
        swprintf_s(buf, L": %u of %u children reported", got, want);
        out.push_back(label + buf);
    }
    return out;
}

std::wstring FormatTree(const TreeNode& root) {
    std::wstring out;
    AppendNode(out, root, 0);
    return out;
}

}  // namespace styler::tap
