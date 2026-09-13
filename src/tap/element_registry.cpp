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
    // spike-standing-crash E4: no reference into t_ids may be held across
    // make_weak specifically. It queries the object for IWeakReferenceSource
    // and creates a new weak reference, which can re-enter XAML; that
    // reentrant call can report another mutation on this thread, which can
    // insert into or erase from t_ids and rehash it, leaving a held Entry&
    // dangling (upstream has the same Entry&-across-make_weak shape,
    // vendor:11853 - a shared latent defect, not something specific to us).
    // Look up, call make_weak with nothing borrowed from the map, then write
    // the result back.
    //
    // This is narrower than "no reference across any WinRT call": resolving
    // an EXISTING weak_ref via .get() (below, and in ForgetElementIdIfDead
    // and ReapDeadElementIdsIfNeeded) is a different, lighter operation - an
    // interlocked refcount-promotion attempt with no call into the object or
    // XAML - so it cannot report a mutation and cannot mutate the map.
    // Holding an iterator or a const reference across it is fine, and those
    // three sites do.
    {
        auto it = t_ids.find(handle);
        if (it != t_ids.end() && it->second.id != ElementId::None &&
            it->second.element.get() == element) {
            return it->second.id;
        }
    }
    ElementId id = static_cast<ElementId>(++t_last_id);
    winrt::weak_ref<wf::IInspectable> weak;
    try {
        weak = winrt::make_weak(element);
    } catch (winrt::hresult_error const& ex) {
        // Without a weak reference the entry cannot be told apart from a
        // successor at the same address; keep neither it nor the id.
        STYLER_LOG(LogLevel::Error, L"make_weak failed 0x%08X",
                   static_cast<unsigned>(ex.code()));
        t_ids.erase(handle);
        return ElementId::None;
    }
    Entry& entry = t_ids[handle];
    entry.id = id;
    entry.element = std::move(weak);
    return id;
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
