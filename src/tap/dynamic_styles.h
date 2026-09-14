// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include <styler/matcher.h>

namespace styler::tap {

// Effective dynamic template and its last expansion dependencies, per visual
// state. The thread's theme cache and in-flight reapply snapshots keep the
// templates' owning theme alive while these exist.
struct DynamicStyleState {
    const styler::PreparedStyle* style = nullptr;
    std::vector<std::wstring> dependencies;
};

using DynamicStyleStates = std::map<std::wstring, DynamicStyleState>;

// Last style for a state wins, including a static style replacing a dynamic
// one. Removing its template also removes the dependencies it contributed.
inline void SelectDynamicStyle(DynamicStyleStates& states,
                               const styler::PreparedStyle& style) {
    if (style.dynamic) {
        states[style.visual_state] = DynamicStyleState{&style, {}};
    } else {
        states.erase(style.visual_state);
    }
}

// RegisterConsumer is keyed by element/property, not by visual state. Publish
// the union so resolving one state cannot unsubscribe another state.
inline std::vector<std::wstring> DynamicStyleDependencies(
    const DynamicStyleStates& states) {
    std::vector<std::wstring> dependencies;
    for (const auto& [state, dynamic] : states) {
        (void)state;
        dependencies.insert(dependencies.end(), dynamic.dependencies.begin(),
                            dynamic.dependencies.end());
    }
    std::sort(dependencies.begin(), dependencies.end());
    dependencies.erase(std::unique(dependencies.begin(), dependencies.end()),
                       dependencies.end());
    return dependencies;
}

}  // namespace styler::tap
