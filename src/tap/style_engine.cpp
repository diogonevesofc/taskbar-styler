// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/style_engine.h>

#include <atomic>
#include <optional>
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

// One customized property on one element.
struct PropertyState {
    wf::IInspectable original;      // What ReadLocalValue gave before us.
    wf::IInspectable custom;        // What we set (null when clearing).
    wf::IInspectable last_applied;  // ReadLocalValue right after our set.
    long long changed_token = 0;    // RegisterPropertyChangedCallback.
};

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

struct ElementState {
    winrt::weak_ref<wux::FrameworkElement> element;
    std::unordered_map<wux::DependencyProperty, PropertyState, DependencyPropertyHash>
        properties;
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
            // Numbers and enums: compare as double; enums box as int32.
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
                if (auto i = v.try_as<int32_t>()) {  // Enums.
                    return *i;
                }
                return std::nullopt;
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

void RestoreElement(ElementId id, ElementState& state) {
    auto element = state.element.get();
    for (auto& [property, prop] : state.properties) {
        if (!element) {
            break;
        }
        try {
            if (prop.changed_token) {
                element.UnregisterPropertyChangedCallback(property, prop.changed_token);
            }
            // Always calls through, even when `original` is null (never had
            // a local value before us): SetOrClearValue is also what
            // cancels a still-pending deferred BackgroundFill.Fill set
            // (review minor), and that must happen regardless of whether
            // there is anything to restore to. UnsetValue() there means
            // ClearValue - the correct outcome when there was no original.
            ModifyingGuard guard;
            SetOrClearValue(element, property,
                            prop.original ? prop.original
                                          : wux::DependencyProperty::UnsetValue(),
                            false);
        } catch (winrt::hresult_error const& ex) {
            STYLER_LOG(LogLevel::Error, L"restore %llu failed 0x%08X",
                       static_cast<unsigned long long>(id),
                       static_cast<unsigned>(ex.code()));
        } catch (...) {
        }
    }
    state.properties.clear();
}

void ApplyProperty(ElementId id, wux::FrameworkElement const& element,
                   ElementState& state, wux::DependencyProperty const& property,
                   wf::IInspectable const& custom_or_unset) {
    PropertyState prop;
    prop.original = ReadLocalValueWithWorkaround(element, property);
    prop.custom = custom_or_unset;
    // The brief's text set/cleared t_modifying by hand around this call
    // with no try/catch at all - if SetOrClearValue threw (it can; it is
    // documented to), the flag was stuck true for the rest of the thread's
    // life: every later PropertyChanged callback would return early at its
    // own IsModifying() check forever, and the shell's own overwrites would
    // never be captured as a new `original` again (review finding C1). The
    // guard's destructor runs during unwinding too, so this cannot happen.
    {
        ModifyingGuard guard;
        SetOrClearValue(element, property, custom_or_unset, true);
    }
    prop.last_applied = ReadLocalValueWithWorkaround(element, property);

    // Something else (a Setter, a template) overwriting our value gets our
    // value back - and becomes the new original, so restore returns to what
    // the shell last wanted, not to what it wanted before we arrived.
    prop.changed_token = element.RegisterPropertyChangedCallback(
        property, [id](wux::DependencyObject const& sender,
                       wux::DependencyProperty const& changed) {
            try {
                if (IsModifying()) {
                    return;
                }
                auto it = t_state.find(id);
                if (it == t_state.end()) {
                    return;
                }
                auto pit = it->second.properties.find(changed);
                if (pit == it->second.properties.end()) {
                    return;
                }
                PropertyState& p = pit->second;
                wf::IInspectable local = ReadLocalValueWithWorkaround(sender, changed);
                if (local != p.last_applied) {
                    p.original = local;
                }
                {
                    ModifyingGuard guard;
                    SetOrClearValue(sender, changed,
                                    p.custom ? p.custom : wux::DependencyProperty::UnsetValue(),
                                    false);
                }
                p.last_applied = ReadLocalValueWithWorkaround(sender, changed);
            } catch (winrt::hresult_error const&) {
            } catch (...) {
            }
        });
    state.properties[property] = std::move(prop);
    ++t_stats.applied_properties;
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
    if (!state.properties.empty()) {
        RestoreElement(id, state);  // Re-reported: start clean.
    }
    state.element = element;

    std::wstring type = view.TypeName();
    std::unordered_set<wux::DependencyProperty, DependencyPropertyHash> claimed;
    for (const styler::RuleMatch& match : matches) {  // Last theme rule first.
        for (const styler::PreparedStyle& style : match.rule->styles) {
            if (!style.visual_state.empty() || match.vsg) {
                ++t_stats.deferred_visual_state_styles;  // Task 6.
                continue;
            }
            try {
                const ResolvedSetter* setter =
                    CachedSetter(theme, style, type, reported);
                if (!claimed.insert(setter->property).second) {
                    continue;  // An earlier (later-in-theme) rule owns it.
                }
                ApplyProperty(id, element, state, setter->property,
                              setter->clear ? wux::DependencyProperty::UnsetValue()
                                            : setter->value);
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
    }
    if (state.properties.empty()) {
        t_state.erase(id);
        return;
    }
    ++t_stats.styled_elements;
    STYLER_LOG(LogLevel::Debug, L"styled %s#%s: %zu properties", type.c_str(),
               view.Name().c_str(), state.properties.size());
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
