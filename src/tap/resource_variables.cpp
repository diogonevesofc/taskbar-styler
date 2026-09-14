// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/resource_variables.h>

#include <unordered_map>
#include <vector>

#include <styler/resource_variables.h>
#include <tap/log.h>
#include <tap/property_setter.h>
#include <tap/winrt_common.h>

namespace styler::tap {
namespace {

thread_local std::shared_ptr<const styler::ResolvedTheme> t_merged_theme;
thread_local wux::ResourceDictionary t_theme_dict{nullptr};
thread_local std::unordered_map<std::wstring, wf::IInspectable> t_originals;
thread_local std::vector<styler::ResourceVariable> t_entries;
thread_local winrt::Windows::UI::ViewManagement::UISettings t_ui_settings{nullptr};
thread_local winrt::event_token t_colors_token{};

// `<Setter Property="Tag"><Setter.Value>...` on FrameworkElement: the same
// XamlReader path styles use, aimed at a property every element has
// (upstream ParseXamlValue, vendor:19232-19245).
wf::IInspectable ParseXamlValue(const std::wstring& xaml) {
    styler::PreparedStyle probe;
    probe.property = L"Tag";
    probe.value = xaml;
    probe.is_xaml = true;
    return ResolveSetter(L"FrameworkElement", L"", probe).value;
}

wf::IInspectable ConvertToExistingType(wf::IInspectable const& existing,
                                       const std::wstring& text) {
    winrt::hstring cls = winrt::get_class_name(existing);
    // Unwrap IReference<T> so ConvertValue sees the inner type.
    constexpr std::wstring_view kRef = L"Windows.Foundation.IReference`1<";
    if (std::wstring_view(cls).starts_with(kRef) && cls.back() == L'>') {
        cls = winrt::hstring(std::wstring_view(cls).substr(
            kRef.size(), cls.size() - kRef.size() - 1));
    }
    return wux::Markup::XamlBindingHelper::ConvertValue(
        wux::Interop::TypeName{cls, wux::Interop::TypeKind::Metadata},
        winrt::box_value(winrt::hstring(text)));
}

wf::IInspectable ValueFor(wux::ResourceDictionary const& resources,
                          const styler::ResourceVariable& v,
                          wf::IInspectable const& existing) {
    switch (v.type) {
        case styler::ResourceValueType::Xaml:
            return v.value.empty() ? nullptr : ParseXamlValue(v.value);
        case styler::ResourceValueType::ThemeResourceReference:
            return resources.Lookup(winrt::box_value(winrt::hstring(v.value)));
        case styler::ResourceValueType::String:
        default:
            // Theme-dictionary entries are boxed strings (XAML converts at
            // use); plain overrides take the existing resource's type.
            return existing ? ConvertToExistingType(existing, v.value)
                            : winrt::box_value(winrt::hstring(v.value));
    }
}

void RefreshReferences() {
    try {
        auto resources = wux::Application::Current().Resources();
        wux::ResourceDictionary dark{nullptr}, light{nullptr};
        if (t_theme_dict) {
            dark = t_theme_dict.ThemeDictionaries()
                       .TryLookup(winrt::box_value(L"Dark")).try_as<wux::ResourceDictionary>();
            light = t_theme_dict.ThemeDictionaries()
                        .TryLookup(winrt::box_value(L"Light")).try_as<wux::ResourceDictionary>();
        }
        for (const auto& v : t_entries) {
            if (v.type != styler::ResourceValueType::ThemeResourceReference) {
                continue;
            }
            auto key = winrt::box_value(winrt::hstring(v.key));
            auto value = resources.Lookup(winrt::box_value(winrt::hstring(v.value)));
            if (v.theme == styler::ResourceTheme::Dark && dark) {
                dark.Insert(key, value);
            } else if (v.theme == styler::ResourceTheme::Light && light) {
                light.Insert(key, value);
            } else {
                resources.Insert(key, value);
            }
        }
    } catch (winrt::hresult_error const& ex) {
        STYLER_LOG(LogLevel::Error, L"refresh resources 0x%08X",
                   static_cast<unsigned>(ex.code()));
    } catch (...) {
    }
}

}  // namespace

void MergeResourceVariablesForThisThread(
    const std::shared_ptr<const styler::ResolvedTheme>& theme) {
    if (t_merged_theme.get() == theme.get()) {
        return;
    }
    UnmergeResourceVariablesForThisThread();
    t_merged_theme = theme;
    if (theme->resource_variables.empty()) {
        return;
    }
    std::vector<std::wstring> diag;
    t_entries = styler::ParseResourceVariables(theme->resource_variables, &diag);
    for (const auto& line : diag) {
        STYLER_LOG(LogLevel::Error, L"%s", line.c_str());
    }
    try {
        auto resources = wux::Application::Current().Resources();
        wux::ResourceDictionary dark, light;
        bool any_theme = false, any_reference = false;

        for (const auto& v : t_entries) {
            try {
                auto key = winrt::box_value(winrt::hstring(v.key));
                if (v.theme != styler::ResourceTheme::None) {
                    auto& target = v.theme == styler::ResourceTheme::Dark ? dark : light;
                    if (target.HasKey(key)) {
                        continue;
                    }
                    target.Insert(key, ValueFor(resources, v, nullptr));
                    any_theme = true;
                } else {
                    auto existing = resources.TryLookup(key);
                    if (!existing) {
                        STYLER_LOG(LogLevel::Error, L"resource '%s' not found, skipped",
                                   v.key.c_str());
                        continue;
                    }
                    if (!t_originals.try_emplace(v.key, existing).second) {
                        continue;  // Already overridden once; keep the first original.
                    }
                    resources.Insert(key, ValueFor(resources, v, existing));
                }
                any_reference |= v.type == styler::ResourceValueType::ThemeResourceReference;
            } catch (winrt::hresult_error const& ex) {
                STYLER_LOG(LogLevel::Error, L"resource '%s': 0x%08X", v.key.c_str(),
                           static_cast<unsigned>(ex.code()));
            }
        }
        if (any_theme) {
            t_theme_dict = wux::ResourceDictionary();
            t_theme_dict.ThemeDictionaries().Insert(winrt::box_value(L"Dark"), dark);
            t_theme_dict.ThemeDictionaries().Insert(winrt::box_value(L"Light"), light);
            resources.MergedDictionaries().Append(t_theme_dict);
        }
        if (any_reference) {
            t_ui_settings = winrt::Windows::UI::ViewManagement::UISettings();
            auto queue = winrt::Windows::System::DispatcherQueue::GetForCurrentThread();
            t_colors_token = t_ui_settings.ColorValuesChanged(
                [queue](auto&&, auto&&) {
                    if (queue) {
                        queue.TryEnqueue([] { RefreshReferences(); });
                    }
                });
        }
        STYLER_LOG(LogLevel::Info, L"merged %zu resource variables on thread %lu",
                   t_entries.size(), GetCurrentThreadId());
    } catch (winrt::hresult_error const& ex) {
        STYLER_LOG(LogLevel::Error, L"merge resources 0x%08X",
                   static_cast<unsigned>(ex.code()));
    } catch (...) {
        STYLER_LOG(LogLevel::Error, L"merge resources threw");
    }
}

void UnmergeResourceVariablesForThisThread() {
    if (!t_merged_theme) {
        return;
    }
    try {
        if (t_ui_settings) {
            t_ui_settings.ColorValuesChanged(t_colors_token);
            t_ui_settings = nullptr;
        }
        auto resources = wux::Application::Current().Resources();
        for (const auto& [key, original] : t_originals) {
            resources.Insert(winrt::box_value(winrt::hstring(key)), original);
        }
        if (t_theme_dict) {
            auto merged = resources.MergedDictionaries();
            uint32_t index = 0;
            if (merged.IndexOf(t_theme_dict, index)) {
                merged.RemoveAt(index);
            }
            t_theme_dict = nullptr;
        }
    } catch (winrt::hresult_error const& ex) {
        STYLER_LOG(LogLevel::Error, L"unmerge resources 0x%08X",
                   static_cast<unsigned>(ex.code()));
    } catch (...) {
    }
    t_originals.clear();
    t_entries.clear();
    t_merged_theme = nullptr;
}

}  // namespace styler::tap
