// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/element_registry.h>

#include <unordered_map>
#include <vector>

#include <tap/log.h>
#include <tap/style_engine.h>

namespace styler::tap {
namespace {

struct Entry {
    ElementId id = ElementId::None;
    winrt::weak_ref<wf::IInspectable> element;
};

thread_local std::unordered_map<InstanceHandle, Entry> t_ids;
thread_local unsigned long long t_last_id = 0;
thread_local size_t t_reap_threshold = 64;

}  // namespace

ElementId GetOrCreateElementId(InstanceHandle handle,
                               wf::IInspectable const& element) {
    if (!handle || !element) {
        return ElementId::None;
    }
    Entry& entry = t_ids[handle];
    if (entry.id != ElementId::None && entry.element.get() == element) {
        return entry.id;
    }
    entry.id = static_cast<ElementId>(++t_last_id);
    try {
        entry.element = winrt::make_weak(element);
    } catch (winrt::hresult_error const& ex) {
        // Without a weak reference the entry cannot be told apart from a
        // successor at the same address; keep neither it nor the id.
        STYLER_LOG(LogLevel::Error, L"make_weak failed 0x%08X",
                   static_cast<unsigned>(ex.code()));
        t_ids.erase(handle);
        return ElementId::None;
    }
    return entry.id;
}

ElementId FindElementId(InstanceHandle handle) {
    auto it = t_ids.find(handle);
    return it != t_ids.end() ? it->second.id : ElementId::None;
}

void ForgetElementId(InstanceHandle handle) {
    t_ids.erase(handle);
}

bool ForgetElementIdIfDead(InstanceHandle handle) {
    auto it = t_ids.find(handle);
    if (it == t_ids.end()) {
        return false;
    }
    if (it->second.element.get()) {
        return false;  // Still alive: a later report may name it again.
    }
    t_ids.erase(it);
    return true;
}

void ReapDeadElementIdsIfNeeded() {
    if (t_ids.size() < t_reap_threshold) {
        return;
    }
    std::vector<std::pair<InstanceHandle, ElementId>> dead;
    for (const auto& [handle, entry] : t_ids) {
        if (!entry.element.get()) {
            dead.push_back({handle, entry.id});
        }
    }
    for (const auto& [handle, id] : dead) {
        t_ids.erase(handle);
        OnElementRemoved(id);  // Tear down as its removal would have.
    }
    t_reap_threshold = t_ids.size() * 2 + 64;
    if (!dead.empty()) {
        STYLER_LOG(LogLevel::Debug, L"reaped %zu dead element ids", dead.size());
    }
}

}  // namespace styler::tap
