// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/style_engine.h>

#include <atomic>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <tap/log.h>
#include <tap/property_setter.h>

namespace styler::tap {
namespace {

// Heap-leaked like g_session: no namespace-scope destructor at detach.
auto* const g_theme =
    new std::atomic<std::shared_ptr<const styler::ResolvedTheme>>{nullptr};

// winrt/Windows.UI.Xaml.2.h specializes std::hash<DependencyProperty> via
// hash_base, whose operator() takes IUnknown const& - an implicit
// reference-to-base conversion that this SDK/STL pairing cannot resolve
// inside std::_Nothrow_hash's noexcept(...) probe (MSVC error C2056 in
// <xhash>, reproduced on this machine). Hashing the identity pointer
// ourselves - the same value hash_base itself would hash, since
// DependencyProperty's operator== already compares that way - sidesteps the
// probe entirely.
struct DependencyPropertyHash {
    size_t operator()(wux::DependencyProperty const& p) const noexcept {
        return std::hash<void*>{}(winrt::get_abi(p));
    }
};

// One property's value across visual states, in one VsgBucket (Task 6).
struct PropertyState {
    // Value per visual state name; "" is the unconditional value. A null
    // IInspectable means "clear the property" (Prop:= with empty value).
    std::map<std::wstring, wf::IInspectable> values;
    bool applied = false;           // We currently hold a custom value.
    wf::IInspectable original;      // Valid while `applied`.
    wf::IInspectable last_applied;  // ReadLocalValue after our last set.
    long long changed_token = 0;
};

// Every style that applies to an element is grouped into one bucket per
// visual-state group its match named: the group's CurrentStateChanged
// re-evaluates every property in its bucket at once. Styles with no @Group
// live in the one bucket whose `group` is empty.
struct VsgBucket {
    // Strong, not weak: measured (Task 6 smoke test) that a weak_ref here
    // never resolves back - group.get() came back null every time by the
    // time OnElementAdded's second pass (a few lines later, same call)
    // reached it, so RegisterStateWatch never ran and CurrentStateChanged
    // never fired. This does not pin the element indefinitely: the strong
    // ref is dropped on all four paths that tear a bucket down -
    // OnElementRemoved (a real removal), OnElementAdded's re-report branch
    // (RestoreElement, before rebuilding fresh buckets), RestoreAllOnThisThread
    // (theme change or session teardown, which calls OnElementRemoved for
    // every tracked id), and plain destruction of the thread_local t_state
    // map at thread exit, which releases every ElementState - buckets
    // included - without going through RestoreElement at all.
    wux::VisualStateGroup group{nullptr};  // Empty: unconditional.
    long long state_changed_token = 0;
    std::unordered_map<wux::DependencyProperty, PropertyState, DependencyPropertyHash>
        properties;
};

struct ElementState {
    winrt::weak_ref<wux::FrameworkElement> element;
    // A list: the CurrentStateChanged handler captures the bucket index and
    // buckets are never reordered or erased individually.
    std::vector<VsgBucket> buckets;
};

thread_local std::unordered_map<ElementId, ElementState> t_state;
thread_local EngineStats t_stats;

// Per-thread cache of resolved setters, keyed by the immutable PreparedStyle
// the theme owns. Cleared when the theme changes. The value object (a brush,
// a transform) is shared by every element the style hits - upstream shares
// setter.Value() the same way.
thread_local std::unordered_map<const styler::PreparedStyle*, ResolvedSetter>
    t_setter_cache;
// A shared_ptr, not the raw ResolvedTheme* the brief's text used: holding a
// ref keeps the old theme's address from ever being reused, which a raw
// pointer cannot (found in review, I2) - SetTheme(B) can free A, and B's
// allocation can legitimately land at A's old address, aliasing a stale
// PreparedStyle* cache key to the wrong style with no way to detect it.
thread_local std::shared_ptr<const styler::ResolvedTheme> t_cache_theme;

// ElementView over a live FrameworkElement (matcher.h documents the
// contract). Every method is called on the element's own UI thread.
class XamlElementView : public styler::ElementView {
public:
    XamlElementView(wux::FrameworkElement element, std::wstring reported)
        : element_(std::move(element)), reported_(std::move(reported)) {}

    std::wstring TypeName() const override {
        return std::wstring(winrt::get_class_name(element_));
    }
    std::wstring ReportedTypeName() const override { return reported_; }
    std::wstring Name() const override { return std::wstring(element_.Name()); }

    std::unique_ptr<styler::ElementView> Parent() const override {
        auto parent = wuxm::VisualTreeHelper::GetParent(element_)
                          .try_as<wux::FrameworkElement>();
        if (!parent) {
            return nullptr;
        }
        return std::make_unique<XamlElementView>(parent, std::wstring());
    }

    int IndexInParent() const override {
        auto parent = wuxm::VisualTreeHelper::GetParent(element_);
        if (!parent) {
            return -1;
        }
        int count = wuxm::VisualTreeHelper::GetChildrenCount(parent);
        for (int i = 0; i < count; ++i) {
            if (wuxm::VisualTreeHelper::GetChild(parent, i) == element_) {
                return i;
            }
        }
        return -1;
    }

    // Reads the local value and materializes `expected` through the same
    // <Setter> path the styles use, then compares as XAML would: primitives
    // unboxed (enums arrive as int32), anything else by reference.
    std::optional<bool> PropertyEquals(std::wstring_view property,
                                       std::wstring_view expected) const override {
        try {
            styler::PreparedStyle probe;
            probe.property = std::wstring(property);
            probe.value = std::wstring(expected);
            ResolvedSetter setter = ResolveSetter(TypeName(), reported_, probe);
            wf::IInspectable actual = ReadLocalValueWithWorkaround(element_, setter.property);
            if (!actual || actual == wux::DependencyProperty::UnsetValue()) {
                return false;
            }
            auto a = actual.try_as<wf::IPropertyValue>();
            auto e = setter.value.try_as<wf::IPropertyValue>();
            if (!a || !e) {
                return actual == setter.value;
            }
            if (a.Type() == wf::PropertyType::String &&
                e.Type() == wf::PropertyType::String) {
                return a.GetString() == e.GetString();
            }
            if (a.Type() == wf::PropertyType::Boolean &&
                e.Type() == wf::PropertyType::Boolean) {
                return a.GetBoolean() == e.GetBoolean();
            }
            // Numbers and enums: compare as double.
            auto as_number = [](wf::IPropertyValue const& v) -> std::optional<double> {
                switch (v.Type()) {
                    case wf::PropertyType::Double: return v.GetDouble();
                    case wf::PropertyType::Single: return v.GetSingle();
                    case wf::PropertyType::Int32: return v.GetInt32();
                    case wf::PropertyType::UInt32: return v.GetUInt32();
                    case wf::PropertyType::Int64: return static_cast<double>(v.GetInt64());
                    case wf::PropertyType::UInt64: return static_cast<double>(v.GetUInt64());
                    case wf::PropertyType::Int16: return v.GetInt16();
                    case wf::PropertyType::UInt16: return v.GetUInt16();
                    case wf::PropertyType::UInt8: return v.GetUInt8();
                    default: break;
                }
                // Enums box as OtherType, not Int32: a boxed WinRT enum is
                // IReference<TEnum>, a distinct parameterized interface (its
                // own IID, derived from TEnum's signature) from
                // IReference<int32_t> - the old code's try_as<int32_t> QI'd
                // for the latter and could never match the former. Confirmed
                // dead with a live [Prop=EnumValue] selector: the class name
                // read back off the local value was
                // Windows.Foundation.IReference`1<Windows.UI.Xaml.Whatever>,
                // and try_as<int32_t> never matched it, so the filter never
                // fired. GetInt32()/GetUInt32 called directly on the
                // IPropertyValue itself, by contrast, DO retrieve an enum's
                // underlying integer despite Type() reporting OtherType -
                // confirmed with the same selector once switched over. Most
                // XAML enums are Int32-backed, a few (flags-style) UInt32.
                try {
                    return v.GetInt32();
                } catch (winrt::hresult_error const&) {
                }
                try {
                    return v.GetUInt32();
                } catch (winrt::hresult_error const&) {
                    return std::nullopt;
                }
            };
            auto an = as_number(a);
            auto en = as_number(e);
            if (an && en) {
                return *an == *en;
            }
            return std::nullopt;
        } catch (winrt::hresult_error const&) {
            return std::nullopt;
        } catch (...) {
            return std::nullopt;
        }
    }

private:
    wux::FrameworkElement element_;
    std::wstring reported_;
};

const ResolvedSetter* CachedSetter(
    const std::shared_ptr<const styler::ResolvedTheme>& theme,
    const styler::PreparedStyle& style, std::wstring_view type,
    std::wstring_view fallback) {
    if (t_cache_theme.get() != theme.get()) {
        t_setter_cache.clear();
        t_cache_theme = theme;
    }
    auto it = t_setter_cache.find(&style);
    if (it != t_setter_cache.end()) {
        return &it->second;
    }
    ResolvedSetter resolved = ResolveSetter(type, fallback, style);  // May throw.
    return &t_setter_cache.emplace(&style, std::move(resolved)).first->second;
}

// Two elements whose group list reports one item that crashes on access
// (upstream vendor:15683-15720). Skip them outright.
wux::VisualStateGroup GetVisualStateGroup(wux::FrameworkElement const& element,
                                          std::wstring_view name) {
    auto cls = winrt::get_class_name(element);
    auto parent_is = [&](std::wstring_view parent_cls) {
        auto parent = wuxm::VisualTreeHelper::GetParent(element)
                          .try_as<wux::FrameworkElement>();
        return parent && winrt::get_class_name(parent) == parent_cls;
    };
    if (cls == L"Taskbar.TaskListButtonPanel" &&
        parent_is(L"Taskbar.SearchBoxLaunchListButton")) {
        return nullptr;
    }
    if (cls == L"SearchUx.SearchUI.SearchButtonRootGrid" &&
        parent_is(L"SearchUx.SearchUI.SearchPillButton")) {
        return nullptr;
    }
    for (auto const& group : wux::VisualStateManager::GetVisualStateGroups(element)) {
        if (group.Name() == name) {
            return group;
        }
    }
    return nullptr;
}

// The element `depth` parents above `leaf`, or null if the tree is shorter.
wux::FrameworkElement AncestorAt(wux::FrameworkElement leaf, int depth) {
    wux::FrameworkElement cur = leaf;
    for (int i = 0; i < depth && cur; ++i) {
        cur = wuxm::VisualTreeHelper::GetParent(cur).try_as<wux::FrameworkElement>();
    }
    return cur;
}

std::wstring CurrentStateName(wux::VisualStateGroup const& group) {
    if (!group) {
        return L"";
    }
    auto state = group.CurrentState();
    return state ? std::wstring(state.Name()) : L"";
}

// Value for `state`, else the unconditional one. Returns whether one exists.
bool PickValue(const PropertyState& prop, const std::wstring& state,
               wf::IInspectable* out) {
    auto it = prop.values.find(state);
    if (it == prop.values.end() && !state.empty()) {
        it = prop.values.find(L"");
    }
    if (it == prop.values.end()) {
        return false;
    }
    *out = it->second;
    return true;
}

wf::IInspectable OrUnset(wf::IInspectable const& v) {
    return v ? v : wux::DependencyProperty::UnsetValue();
}

// Applies `value`, capturing the pre-existing local value the first time this
// property is customized. `initial` forwards to SetOrClearValue's own
// BackgroundFill.Fill deferral.
void SetCustom(wux::FrameworkElement const& element,
               wux::DependencyProperty const& property, PropertyState& prop,
               wf::IInspectable const& value, bool initial) {
    if (!prop.applied) {
        prop.original = ReadLocalValueWithWorkaround(element, property);
        prop.applied = true;
    }
    {
        ModifyingGuard guard;
        SetOrClearValue(element, property, OrUnset(value), initial);
    }
    prop.last_applied = ReadLocalValueWithWorkaround(element, property);
}

// Restores the pre-existing value captured by SetCustom, if any is applied.
void Unapply(wux::FrameworkElement const& element,
             wux::DependencyProperty const& property, PropertyState& prop) {
    if (!prop.applied) {
        return;
    }
    {
        ModifyingGuard guard;
        SetOrClearValue(element, property, OrUnset(prop.original), false);
    }
    prop.applied = false;
    prop.original = nullptr;
}

// Re-evaluates one bucket against `state_name`: apply, re-apply or restore
// each property. Runs on state change and on first apply. Holds `bucket` -
// a reference into some ElementState's `buckets` vector, itself a value
// inside the thread_local t_state map - across every SetOrClearValue call
// below. Safe against a nested report for a DIFFERENT id arriving on this
// thread while one of those calls is in flight: t_state and bucket.properties
// are unordered_maps, and inserting or erasing a different key never
// invalidates a reference to this one. NOT safe against a nested report for
// THIS SAME id, which could call RestoreElement (clearing or reallocating
// the very buckets vector `bucket` points into) before this call returns -
// same class of risk as element_registry.cpp:29-45 (Entry& held across a
// call that can re-enter and mutate the map), and present upstream too.
void ApplyBucketForState(ElementId id, wux::FrameworkElement const& element,
                         VsgBucket& bucket, const std::wstring& state_name,
                         bool initial) {
    if (!state_name.empty()) {
        STYLER_LOG(LogLevel::Debug, L"apply %llu state '%s'",
                   static_cast<unsigned long long>(id), state_name.c_str());
    }
    for (auto& [property, prop] : bucket.properties) {
        try {
            wf::IInspectable value;
            if (PickValue(prop, state_name, &value)) {
                SetCustom(element, property, prop, value, initial);
            } else {
                Unapply(element, property, prop);
            }
        } catch (winrt::hresult_error const& ex) {
            ++t_stats.failed_styles;
            STYLER_LOG(LogLevel::Error, L"apply %llu state '%s' failed 0x%08X",
                       static_cast<unsigned long long>(id), state_name.c_str(),
                       static_cast<unsigned>(ex.code()));
        } catch (...) {
            ++t_stats.failed_styles;
        }
    }
}

void RegisterPropertyWatch(ElementId id, size_t bucket_index,
                           wux::FrameworkElement const& element,
                           wux::DependencyProperty const& property,
                           PropertyState& prop) {
    prop.changed_token = element.RegisterPropertyChangedCallback(
        property, [id, bucket_index](wux::DependencyObject const& sender,
                                     wux::DependencyProperty const& changed) {
            try {
                if (IsModifying()) {
                    return;
                }
                auto it = t_state.find(id);
                if (it == t_state.end() || bucket_index >= it->second.buckets.size()) {
                    return;
                }
                VsgBucket& bucket = it->second.buckets[bucket_index];
                auto pit = bucket.properties.find(changed);
                if (pit == bucket.properties.end() || !pit->second.applied) {
                    return;
                }
                PropertyState& p = pit->second;
                wf::IInspectable local = ReadLocalValueWithWorkaround(sender, changed);
                if (local != p.last_applied) {
                    p.original = local;  // The shell changed its mind; honour it on restore.
                }
                wf::IInspectable value;
                if (!PickValue(p, CurrentStateName(bucket.group), &value)) {
                    return;
                }
                auto live = sender.try_as<wux::FrameworkElement>();
                if (!live) {
                    return;
                }
                {
                    ModifyingGuard guard;
                    SetOrClearValue(live, changed, OrUnset(value), false);
                }
                p.last_applied = ReadLocalValueWithWorkaround(live, changed);
            } catch (winrt::hresult_error const&) {
            } catch (...) {
            }
        });
}

void RegisterStateWatch(ElementId id, size_t bucket_index,
                        wux::VisualStateGroup const& group, VsgBucket& bucket) {
    bucket.state_changed_token = group.CurrentStateChanged(
        [id, bucket_index](wf::IInspectable const&,
                           wux::VisualStateChangedEventArgs const& e) {
            try {
                auto it = t_state.find(id);
                if (it == t_state.end() || bucket_index >= it->second.buckets.size()) {
                    return;
                }
                auto element = it->second.element.get();
                if (!element) {
                    return;
                }
                auto new_state = e.NewState();
                std::wstring name = new_state ? std::wstring(new_state.Name()) : L"";
                ApplyBucketForState(id, element, it->second.buckets[bucket_index],
                                    name, false);
            } catch (winrt::hresult_error const&) {
            } catch (...) {
            }
        }).value;
}

void RestoreElement(ElementId id, ElementState& state) {
    auto element = state.element.get();
    for (VsgBucket& bucket : state.buckets) {
        if (auto group = bucket.group; group && bucket.state_changed_token) {
            try {
                group.CurrentStateChanged(winrt::event_token{bucket.state_changed_token});
            } catch (winrt::hresult_error const&) {
            } catch (...) {
            }
        }
        for (auto& [property, prop] : bucket.properties) {
            if (!element) {
                break;
            }
            try {
                if (prop.changed_token) {
                    element.UnregisterPropertyChangedCallback(property, prop.changed_token);
                }
                Unapply(element, property, prop);
            } catch (winrt::hresult_error const& ex) {
                STYLER_LOG(LogLevel::Error, L"restore %llu failed 0x%08X",
                           static_cast<unsigned long long>(id),
                           static_cast<unsigned>(ex.code()));
            } catch (...) {
            }
        }
    }
    state.buckets.clear();
}

}  // namespace

void SetTheme(std::shared_ptr<const styler::ResolvedTheme> theme) {
    g_theme->store(std::move(theme));
}

std::shared_ptr<const styler::ResolvedTheme> CurrentTheme() {
    return g_theme->load();
}

void OnElementAdded(ElementId id, wux::FrameworkElement const& element,
                    const wchar_t* reported_type) {
    std::shared_ptr<const styler::ResolvedTheme> theme = CurrentTheme();
    if (!theme) {
        return;
    }
    std::wstring reported = reported_type ? reported_type : L"";
    XamlElementView view(element, reported);
    std::vector<styler::RuleMatch> matches = styler::FindMatchingRules(*theme, view);
    if (matches.empty()) {
        return;
    }

    ElementState& state = t_state[id];
    if (!state.buckets.empty()) {
        RestoreElement(id, state);  // Re-reported: start clean.
    }
    state.element = element;

    std::wstring type = view.TypeName();
    std::unordered_set<wux::DependencyProperty, DependencyPropertyHash> claimed;
    for (const styler::RuleMatch& match : matches) {  // Last theme rule first.
        // Resolve this match's bucket: the group on the ancestor the
        // selector named, or the unconditional bucket.
        wux::VisualStateGroup group{nullptr};
        if (match.vsg) {
            try {
                if (auto owner = AncestorAt(element, match.vsg->ancestor_depth)) {
                    group = GetVisualStateGroup(owner, match.vsg->name);
                }
            } catch (winrt::hresult_error const&) {
            } catch (...) {
            }
            if (!group) {
                STYLER_LOG(LogLevel::Debug, L"rule %zu: group %s not found on %s",
                           match.rule->source_index, match.vsg->name.c_str(),
                           type.c_str());
            }
        }
        size_t bucket_index = state.buckets.size();
        for (size_t i = 0; i < state.buckets.size(); ++i) {
            auto existing = state.buckets[i].group;
            if ((!group && !existing) || (group && existing == group)) {
                bucket_index = i;
                break;
            }
        }
        if (bucket_index == state.buckets.size()) {
            VsgBucket b;
            if (group) {
                b.group = group;
            }
            state.buckets.push_back(std::move(b));
        }
        VsgBucket& bucket = state.buckets[bucket_index];

        // A property is claimed once across all buckets, by whichever MATCH
        // reaches it first in FindMatchingRules order (last theme rule
        // first) - fidelity with upstream's propertiesAdded (vendor
        // :15933-16031), which is inserted once per (rule, property) AFTER a
        // rule's own states are merged into one per-property entry, not once
        // per style line. So every style below is let through the `claimed`
        // check (a per-match set, folded into `claimed` once the match is
        // done): a rule with both Background@ActiveNormal=... and
        // Background@ActivePointerOver=... claims Background exactly once
        // and keeps every one of its own states. A LATER rule's
        // unconditional Background=... and an EARLIER rule's
        // Background@State=... on the same property never coexist though:
        // the later rule (seen first here) wins the property outright and
        // the earlier one never applies. That is fidelity, not a bug.
        std::unordered_set<wux::DependencyProperty, DependencyPropertyHash>
            claimed_by_match;
        for (const styler::PreparedStyle& style : match.rule->styles) {
            try {
                const ResolvedSetter* setter = CachedSetter(theme, style, type, reported);
                if (claimed.contains(setter->property)) {
                    continue;  // An earlier (later-in-theme) match owns it.
                }
                // Claimed regardless of whether it turns out inert just
                // below - fidelity with upstream's propertiesAdded
                // (vendor:16022), which inserts a rule's property before
                // group resolution is even consulted. So a group-less
                // Prop@State still blocks an EARLIER rule's unconditional
                // Prop=... on that same property from ever applying, even
                // though this rule's own value never activates.
                claimed_by_match.insert(setter->property);
                if (!style.visual_state.empty() && !group) {
                    STYLER_LOG(LogLevel::Debug, L"rule %zu: %s@%s without a group, inert",
                               match.rule->source_index, style.property.c_str(),
                               style.visual_state.c_str());
                    continue;
                }
                PropertyState& prop = bucket.properties[setter->property];
                prop.values[style.visual_state] = setter->clear ? nullptr : setter->value;
            } catch (winrt::hresult_error const& ex) {
                ++t_stats.failed_styles;
                STYLER_LOG(LogLevel::Error, L"rule %zu %s=%s on %s: 0x%08X",
                           match.rule->source_index, style.property.c_str(),
                           style.value.c_str(), type.c_str(),
                           static_cast<unsigned>(ex.code()));
            } catch (...) {
                ++t_stats.failed_styles;
            }
        }
        claimed.insert(claimed_by_match.begin(), claimed_by_match.end());
    }

    // Drop empty buckets, then apply each for its current state and watch.
    std::erase_if(state.buckets, [](const VsgBucket& b) { return b.properties.empty(); });
    if (state.buckets.empty()) {
        t_state.erase(id);
        return;
    }
    size_t applied = 0;
    size_t total_properties = 0;
    for (size_t i = 0; i < state.buckets.size(); ++i) {
        VsgBucket& bucket = state.buckets[i];
        auto group = bucket.group;
        // CurrentStateName(group) - specifically group.CurrentState() - is a
        // WinRT call evaluated as an argument here, outside ApplyBucketForState's
        // own per-property try/catch: left bare, a throw would unwind straight
        // out of OnElementAdded, skipping the property/state watches for this
        // bucket AND every bucket after it, half-wiring the element (some
        // buckets applied and watched, the rest with neither). Wrapping just
        // this call keeps the watches below reachable for this bucket, and
        // lets the loop move on to the next one.
        try {
            ApplyBucketForState(id, element, bucket, CurrentStateName(group), true);
        } catch (winrt::hresult_error const& ex) {
            ++t_stats.failed_styles;
            STYLER_LOG(LogLevel::Error, L"initial apply %llu failed 0x%08X",
                       static_cast<unsigned long long>(id),
                       static_cast<unsigned>(ex.code()));
        } catch (...) {
            ++t_stats.failed_styles;
        }
        for (auto& [property, prop] : bucket.properties) {
            try {
                RegisterPropertyWatch(id, i, element, property, prop);
            } catch (winrt::hresult_error const&) {
            } catch (...) {
            }
            applied += prop.applied ? 1 : 0;
        }
        total_properties += bucket.properties.size();
        if (group) {
            try {
                RegisterStateWatch(id, i, group, bucket);
            } catch (winrt::hresult_error const& ex) {
                STYLER_LOG(LogLevel::Error, L"CurrentStateChanged subscribe 0x%08X",
                           static_cast<unsigned>(ex.code()));
            } catch (...) {
            }
        }
    }
    t_stats.applied_properties += applied;
    ++t_stats.styled_elements;
    STYLER_LOG(LogLevel::Debug, L"styled %s#%s: %zu properties in %zu buckets",
               type.c_str(), view.Name().c_str(), total_properties,
               state.buckets.size());
}

void OnElementRemoved(ElementId id) {
    auto it = t_state.find(id);
    if (it == t_state.end()) {
        return;
    }
    RestoreElement(id, it->second);
    t_state.erase(id);
}

bool ElementHasState(ElementId id) {
    return id != ElementId::None && t_state.contains(id);
}

void RestoreAllOnThisThread() {
    std::vector<ElementId> ids;
    ids.reserve(t_state.size());
    for (const auto& [id, _] : t_state) {
        ids.push_back(id);
    }
    for (ElementId id : ids) {
        OnElementRemoved(id);
    }
    t_setter_cache.clear();
    t_cache_theme = nullptr;
    STYLER_LOG(LogLevel::Info, L"restored %zu elements on thread %lu", ids.size(),
               GetCurrentThreadId());
    t_stats = EngineStats{};
}

EngineStats StatsForThisThread() {
    return t_stats;
}

}  // namespace styler::tap
