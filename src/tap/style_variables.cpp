// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/style_variables.h>

#include <algorithm>
#include <map>
#include <unordered_map>

#include <tap/log.h>

namespace styler::tap {
namespace {

// One element capturing one variable.
struct CaptureSite {
    ElementId id = ElementId::None;
    winrt::weak_ref<wux::FrameworkElement> element;
    std::vector<void*> chain;  // Root first, the capturing element last.
    styler::StyleVariableValue value;
    uint64_t sequence = 0;  // Registration order, for the closest-tie rule.
};

// One property of one element whose value text references variables.
struct ConsumerSite {
    ElementId id = ElementId::None;
    wux::DependencyProperty property{nullptr};
};

struct CaptureSubscription {
    wux::DependencyProperty property{nullptr};
    std::wstring name;
    long long property_changed_token = 0;  // 0 when SizeChanged covers it.
};

struct ElementCaptures {
    std::vector<CaptureSubscription> subscriptions;
    winrt::event_token size_changed_token{};
    winrt::weak_ref<wux::FrameworkElement> element;
};

// All of this is per XAML host thread, like every other piece of state that
// points at elements (spec section 6.1). NOT keyed by XamlRoot the way
// upstream is (vendor:12054-12100): each taskbar surface already runs on its
// own thread here, and no shipped theme captures the same variable name twice
// (measured over all 55). The cost if that changes: two XamlRoots sharing one
// thread would share a variable of the same name, which reads one surface's
// number on the other - one wrong number, never a crash, and the fix is one
// more map level.
thread_local std::unordered_map<std::wstring, std::vector<CaptureSite>>
    t_variables;
thread_local std::unordered_map<std::wstring, std::vector<ConsumerSite>>
    t_consumers;
thread_local std::unordered_map<ElementId, ElementCaptures> t_element_captures;
// Every variable name each (element, property) consumer last depended on, so
// a re-expansion can retract the ones it stopped using.
thread_local std::map<std::pair<ElementId, void*>, std::vector<std::wstring>>
    t_consumer_deps;
thread_local uint64_t t_sequence = 0;
// Guards against a propagation that re-enters itself: re-applying a property
// can change a captured layout property, which propagates again. Upstream
// caps the same way (vendor:12045-12052).
thread_local int t_propagation_depth = 0;
constexpr int kMaxPropagationDepth = 8;

// thread_local, like every other piece of state here: the engine installs
// this on each host thread's first styled element (style_engine.cpp), so a
// process-global `static` would have every new thread destroy-and-reconstruct
// the std::function while another thread is testing or calling it - a real
// data race as soon as a second taskbar surface exists (found in review).
ReapplyPropertyFn& ReapplyCallback() {
    static thread_local ReapplyPropertyFn fn;
    return fn;
}

bool IsLayoutDrivenProperty(wux::DependencyProperty const& property) {
    return property == wux::FrameworkElement::ActualWidthProperty() ||
           property == wux::FrameworkElement::ActualHeightProperty();
}

// The effective value, not the local one: ActualWidth never has a local value,
// so ReadLocalValue would report Unset for exactly the properties the shipped
// themes capture (vendor:16775-16781).
styler::StyleVariableValue ReadCapturedValue(
    wux::FrameworkElement const& element,
    wux::DependencyProperty const& property) {
    styler::StyleVariableValue out;
    wf::IInspectable value{nullptr};
    try {
        value = element.GetValue(property);
    } catch (winrt::hresult_error const&) {
        return out;
    } catch (...) {
        return out;
    }
    if (!value || value == wux::DependencyProperty::UnsetValue()) {
        return out;
    }
    try {
        if (auto boxed = value.try_as<wf::IPropertyValue>()) {
            switch (boxed.Type()) {
                case wf::PropertyType::Double:
                    out.number = boxed.GetDouble();
                    break;
                case wf::PropertyType::Single:
                    out.number = boxed.GetSingle();
                    break;
                case wf::PropertyType::Int32:
                    out.number = boxed.GetInt32();
                    break;
                case wf::PropertyType::UInt32:
                    out.number = boxed.GetUInt32();
                    break;
                case wf::PropertyType::Int64:
                    out.number = static_cast<double>(boxed.GetInt64());
                    break;
                case wf::PropertyType::Boolean:
                    out.text = boxed.GetBoolean() ? L"True" : L"False";
                    out.substitutable = true;
                    return out;
                case wf::PropertyType::String:
                    out.text = boxed.GetString();
                    out.substitutable = true;
                    return out;
                default:
                    break;
            }
            if (out.number) {
                out.text = styler::FormatDoubleInvariant(*out.number);
                out.substitutable = true;
                return out;
            }
        }
        // A brush, a thickness, an enum box we do not understand: record the
        // class name so `{{Var == `...`}}` can still branch on it, but leave
        // `substitutable` false so a bare `{{Var}}` skips the style instead of
        // writing a class name into the XAML.
        out.text = winrt::get_class_name(value);
    } catch (winrt::hresult_error const&) {
        out.text.clear();
    } catch (...) {
        out.text.clear();
    }
    return out;
}

bool SameValue(const styler::StyleVariableValue& a,
               const styler::StyleVariableValue& b) {
    return a.substitutable == b.substitutable && a.number == b.number &&
           a.text == b.text;
}

void PropagateChange(const std::wstring& name) {
    if (t_propagation_depth >= kMaxPropagationDepth) {
        STYLER_LOG(LogLevel::Error, L"variable %s: propagation depth capped",
                   name.c_str());
        return;
    }
    auto it = t_consumers.find(name);
    if (it == t_consumers.end() || !ReapplyCallback()) {
        return;
    }
    // A copy: re-applying a property can register or drop consumers, and
    // upstream hit exactly this (a nested propagation invalidating the list
    // being walked).
    std::vector<ConsumerSite> sites = it->second;
    ++t_propagation_depth;
    for (const ConsumerSite& site : sites) {
        try {
            ReapplyCallback()(site.id, site.property);
        } catch (winrt::hresult_error const&) {
        } catch (...) {
        }
    }
    --t_propagation_depth;
}

void SetCaptureValue(const std::wstring& name, ElementId id,
                     styler::StyleVariableValue value) {
    auto it = t_variables.find(name);
    if (it == t_variables.end()) {
        return;
    }
    for (CaptureSite& site : it->second) {
        if (site.id != id) {
            continue;
        }
        if (SameValue(site.value, value)) {
            return;  // Nothing to propagate.
        }
        site.value = std::move(value);
        // The chain can have changed since registration (a reparent), and it
        // is what decides which consumer reads this capture.
        if (auto element = site.element.get()) {
            site.chain = AncestorChain(element);
        }
        PropagateChange(name);
        return;
    }
}

}  // namespace

std::vector<void*> AncestorChain(wux::FrameworkElement const& element) {
    std::vector<void*> chain;
    try {
        wux::DependencyObject node = element;
        // 64 is well past any real taskbar depth and bounds a tree that a
        // future Windows build might make pathological.
        for (int depth = 0; node && depth < 64; ++depth) {
            chain.push_back(winrt::get_abi(node));
            node = wuxm::VisualTreeHelper::GetParent(node);
        }
    } catch (winrt::hresult_error const&) {
    } catch (...) {
    }
    std::reverse(chain.begin(), chain.end());  // Root first.
    return chain;
}

void RegisterCapture(ElementId id, wux::FrameworkElement const& element,
                     wux::DependencyProperty const& property,
                     const std::wstring& name) {
    ElementCaptures& captures = t_element_captures[id];
    captures.element = element;
    for (const CaptureSubscription& existing : captures.subscriptions) {
        if (existing.property == property) {
            // Debug, not Error: OnElementAdded treats a re-report as routine,
            // so this is the normal path for any capturing element that gets
            // reported twice - logging it at Error manufactured ERR noise and
            // undermined the smoke test's own "0 ERR" check (found in review).
            STYLER_LOG(LogLevel::Debug,
                       L"capture: this element already captures that property "
                       L"as '%s'; keeping it, dropping '%s'",
                       existing.name.c_str(), name.c_str());
            // The subscription still stands, but the element may have been
            // reparented since: refresh the chain now rather than leaving a
            // stale one until the captured value next changes.
            auto vit = t_variables.find(existing.name);
            if (vit != t_variables.end()) {
                for (CaptureSite& s : vit->second) {
                    if (s.id == id) {
                        s.chain = AncestorChain(element);
                        break;
                    }
                }
            }
            return;
        }
    }

    CaptureSite site;
    site.id = id;
    site.element = element;
    site.chain = AncestorChain(element);
    site.value = ReadCapturedValue(element, property);
    site.sequence = ++t_sequence;

    std::vector<CaptureSite>& sites = t_variables[name];
    std::erase_if(sites, [id](const CaptureSite& s) { return s.id == id; });
    sites.push_back(std::move(site));

    CaptureSubscription subscription;
    subscription.property = property;
    subscription.name = name;

    if (IsLayoutDrivenProperty(property)) {
        // ActualWidth / ActualHeight never raise a property-changed callback;
        // SizeChanged is the notification for them. One subscription per
        // element covers every layout-driven capture it has.
        if (!captures.size_changed_token) {
            winrt::weak_ref<wux::FrameworkElement> weak = element;
            captures.size_changed_token = element.SizeChanged(
                [id, weak](wf::IInspectable const&,
                           wux::SizeChangedEventArgs const&) {
                    try {
                        auto live = weak.get();
                        if (!live) {
                            return;
                        }
                        auto it = t_element_captures.find(id);
                        if (it == t_element_captures.end()) {
                            return;
                        }
                        // A copy: SetCaptureValue propagates, which can touch
                        // t_element_captures.
                        std::vector<CaptureSubscription> subs =
                            it->second.subscriptions;
                        for (const CaptureSubscription& sub : subs) {
                            if (!IsLayoutDrivenProperty(sub.property)) {
                                continue;
                            }
                            SetCaptureValue(sub.name, id,
                                            ReadCapturedValue(live, sub.property));
                        }
                    } catch (winrt::hresult_error const&) {
                    } catch (...) {
                    }
                });
        }
    } else {
        winrt::weak_ref<wux::FrameworkElement> weak = element;
        std::wstring captured_name = name;
        subscription.property_changed_token =
            element.RegisterPropertyChangedCallback(
                property, [id, weak, captured_name](
                              wux::DependencyObject const&,
                              wux::DependencyProperty const& changed) {
                    try {
                        auto live = weak.get();
                        if (!live) {
                            return;
                        }
                        SetCaptureValue(captured_name, id,
                                        ReadCapturedValue(live, changed));
                    } catch (winrt::hresult_error const&) {
                    } catch (...) {
                    }
                });
    }

    captures.subscriptions.push_back(std::move(subscription));

    // A new capture can be closer to consumers that registered before this
    // element was ever matched, so they have to be re-evaluated even though
    // the variable itself is not "new".
    PropagateChange(name);
}

void RegisterConsumer(ElementId id, wux::DependencyProperty const& property,
                      const std::vector<std::wstring>& deps) {
    auto key = std::make_pair(id, winrt::get_abi(property));
    auto it = t_consumer_deps.find(key);
    if (it != t_consumer_deps.end()) {
        for (const std::wstring& old : it->second) {
            auto cit = t_consumers.find(old);
            if (cit == t_consumers.end()) {
                continue;
            }
            std::erase_if(cit->second, [&](const ConsumerSite& s) {
                return s.id == id && s.property == property;
            });
            if (cit->second.empty()) {
                t_consumers.erase(cit);
            }
        }
        t_consumer_deps.erase(it);
    }
    if (deps.empty()) {
        return;
    }
    for (const std::wstring& name : deps) {
        t_consumers[name].push_back(ConsumerSite{id, property});
    }
    t_consumer_deps.emplace(key, deps);
}

const styler::StyleVariableValue* LookupForConsumer(
    std::wstring_view name, const std::vector<void*>& consumer_chain) {
    auto it = t_variables.find(std::wstring(name));
    if (it == t_variables.end() || it->second.empty()) {
        return nullptr;
    }
    const CaptureSite* best = nullptr;
    size_t best_depth = 0;
    for (const CaptureSite& site : it->second) {
        // A dead capturer must not win: a capture-only element has no
        // ElementState, so its handle is released and its death is noticed
        // only by the amortized reaper. Until then its `chain` is a vector of
        // freed ABI pointers, and XAML recycles same-type elements to the
        // same addresses at the same depth - so a stale chain can share a
        // spuriously longer prefix, win this contest, and serve a dead
        // element's frozen number (found in review).
        if (!site.element.get()) {
            continue;
        }
        size_t depth = 0;
        while (depth < site.chain.size() && depth < consumer_chain.size() &&
               site.chain[depth] == consumer_chain[depth]) {
            ++depth;
        }
        // Strictly deeper wins; equally deep, the later registration wins -
        // the documented tie-break (vendor:363-367).
        if (!best || depth > best_depth ||
            (depth == best_depth && site.sequence > best->sequence)) {
            best = &site;
            best_depth = depth;
        }
    }
    return best ? &best->value : nullptr;
}

styler::StyleVariableLookup LookupFor(
    const std::vector<void*>& consumer_chain) {
    return [&consumer_chain](std::wstring_view name)
               -> const styler::StyleVariableValue* {
        return LookupForConsumer(name, consumer_chain);
    };
}

void ForgetElementVariables(ElementId id) {
    auto it = t_element_captures.find(id);
    if (it != t_element_captures.end()) {
        auto element = it->second.element.get();
        if (element) {
            try {
                if (it->second.size_changed_token) {
                    element.SizeChanged(it->second.size_changed_token);
                }
                for (const CaptureSubscription& sub : it->second.subscriptions) {
                    if (sub.property_changed_token) {
                        element.UnregisterPropertyChangedCallback(
                            sub.property, sub.property_changed_token);
                    }
                }
            } catch (winrt::hresult_error const&) {
            } catch (...) {
            }
        }
        t_element_captures.erase(it);
        // Swept by id across every variable, NOT retracted by subscription
        // name: RegisterCapture pushes the CaptureSite before it subscribes,
        // so a throw in between leaves a site whose name appears in no
        // subscription - retracting by name would strand that orphan,
        // publishing a frozen value with nothing able to reach it again
        // (found in review).
        std::vector<std::wstring> names;
        for (auto vit = t_variables.begin(); vit != t_variables.end();) {
            const size_t before = vit->second.size();
            std::erase_if(vit->second,
                          [id](const CaptureSite& s) { return s.id == id; });
            if (vit->second.size() != before) {
                names.push_back(vit->first);
            }
            if (vit->second.empty()) {
                vit = t_variables.erase(vit);
            } else {
                ++vit;
            }
        }
        // Every retraction lands before the first propagation, so a consumer
        // recomputing here sees the state WITHOUT any of this element's
        // captures - not just without the one being retracted.
        for (const std::wstring& name : names) {
            PropagateChange(name);
        }
    }

    // Consumer registrations keyed by this element, whatever the property.
    std::vector<std::pair<ElementId, void*>> keys;
    for (const auto& [key, deps] : t_consumer_deps) {
        if (key.first == id) {
            keys.push_back(key);
        }
    }
    for (const auto& key : keys) {
        auto dit = t_consumer_deps.find(key);
        if (dit == t_consumer_deps.end()) {
            continue;
        }
        for (const std::wstring& name : dit->second) {
            auto cit = t_consumers.find(name);
            if (cit == t_consumers.end()) {
                continue;
            }
            std::erase_if(cit->second, [id](const ConsumerSite& s) {
                return s.id == id;
            });
            if (cit->second.empty()) {
                t_consumers.erase(cit);
            }
        }
        t_consumer_deps.erase(dit);
    }
}

void ClearStyleVariablesOnThisThread() {
    std::vector<ElementId> ids;
    ids.reserve(t_element_captures.size());
    for (const auto& [id, captures] : t_element_captures) {
        ids.push_back(id);
    }
    for (ElementId id : ids) {
        ForgetElementVariables(id);
    }
    t_variables.clear();
    t_consumers.clear();
    t_consumer_deps.clear();
    t_element_captures.clear();
}

void SetReapplyPropertyCallback(ReapplyPropertyFn fn) {
    ReapplyCallback() = std::move(fn);
}

size_t DefinedVariableCount() {
    return t_variables.size();
}

}  // namespace styler::tap
