// SPDX-License-Identifier: GPL-3.0-or-later
#include <styler/constants.h>

#include <algorithm>

namespace styler {

std::wstring ApplyStyleConstants(std::wstring_view text,
                                 const ResolvedConstants& constants) {
    std::wstring result;
    size_t last = 0;
    size_t pos;
    while ((pos = text.find(L'$', last)) != std::wstring_view::npos) {
        result.append(text, last, pos - last);
        const std::pair<std::wstring, std::wstring>* hit = nullptr;
        for (const auto& c : constants) {
            if (text.substr(pos + 1, c.first.size()) == c.first) {
                hit = &c;  // First hit is the longest: `constants` is sorted.
                break;
            }
        }
        if (hit) {
            result += hit->second;
            last = pos + 1 + hit->first.size();
        } else {
            result += L'$';
            last = pos + 1;
        }
    }
    result.append(text.substr(last));
    return result;
}

ResolvedConstants ResolveConstants(
    const std::map<std::wstring, std::wstring>& constants) {
    ResolvedConstants resolved(constants.begin(), constants.end());
    std::stable_sort(resolved.begin(), resolved.end(),
                     [](const auto& a, const auto& b) {
                         return a.first.size() > b.first.size();
                     });

    // Upstream expands a constant's value against the constants declared
    // before it, in declaration order. The map has lost that order, so
    // iterate to a fixed point instead: the corpus only ever references
    // earlier declarations (measured, test_corpus_resolution.cpp), where
    // both give the same answer. The pass cap makes a self-reference like
    // A=$A terminate instead of growing forever.
    constexpr int kMaxPasses = 8;
    for (int pass = 0; pass < kMaxPasses; ++pass) {
        bool changed = false;
        for (auto& [name, value] : resolved) {
            if (value.find(L'$') == std::wstring::npos) {
                continue;
            }
            std::wstring expanded = ApplyStyleConstants(value, resolved);
            if (expanded != value) {
                value = std::move(expanded);
                changed = true;
            }
        }
        if (!changed) {
            break;
        }
    }
    return resolved;
}

}  // namespace styler
