// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/property_setter.h>

#include <utility>
#include <vector>

#include <tap/log.h>

namespace styler::tap {
namespace {

// XAML's "Failed to create a 'System.Type' from the text ..." (a stowed
// exception): the TargetType is unknown to the parser.
constexpr HRESULT kUnknownTypeInXaml = 0x802B000A;

wux::Style LoadStyle(std::wstring_view type, const std::wstring& setter_xaml) {
    std::wstring xaml =
        LR"(<ResourceDictionary
    xmlns="http://schemas.microsoft.com/winfx/2006/xaml/presentation"
    xmlns:x="http://schemas.microsoft.com/winfx/2006/xaml"
    xmlns:muxc="using:Microsoft.UI.Xaml.Controls")";
    if (auto dot = type.rfind(L'.'); dot != std::wstring_view::npos) {
        xaml += L"\n    xmlns:styler=\"using:";
        xaml += EscapeXmlAttribute(type.substr(0, dot));
        xaml += L"\">\n    <Style TargetType=\"styler:";
        xaml += EscapeXmlAttribute(type.substr(dot + 1));
        xaml += L"\">\n";
    } else {
        xaml += L">\n    <Style TargetType=\"";
        xaml += EscapeXmlAttribute(type);
        xaml += L"\">\n";
    }
    xaml += setter_xaml;
    xaml += L"    </Style>\n</ResourceDictionary>";

    auto dictionary = wux::Markup::XamlReader::Load(xaml).as<wux::ResourceDictionary>();
    auto first = dictionary.First();
    return first.Current().Value().as<wux::Style>();
}

wux::Style LoadStyleWithFallbacks(std::wstring_view type,
                                  std::wstring_view fallback_type,
                                  const std::wstring& setter_xaml) {
    std::vector<std::wstring_view> attempts;
    if (!type.empty()) {
        attempts.push_back(type);
    }
    if (!fallback_type.empty() && fallback_type != type) {
        attempts.push_back(fallback_type);
    }
    attempts.push_back(L"FrameworkElement");
    for (size_t i = 0; i < attempts.size(); ++i) {
        try {
            return LoadStyle(attempts[i], setter_xaml);
        } catch (winrt::hresult_error const& ex) {
            if (ex.code() != kUnknownTypeInXaml || i + 1 == attempts.size()) {
                throw;
            }
            STYLER_LOG(LogLevel::Debug, L"type %.*s unknown to XAML, retrying",
                       static_cast<int>(attempts[i].size()), attempts[i].data());
        }
    }
    throw winrt::hresult_error(E_UNEXPECTED);
}

// Set while this thread is inside a write ApplyProperty/RestoreElement (or
// this file's own deferred BackgroundFill.Fill callback) made itself. See
// IsModifying()/ModifyingGuard in property_setter.h.
thread_local bool t_modifying = false;

// Pending deferred BackgroundFill sets on this thread, so a second apply to
// the same element cancels the first instead of racing it.
// CoreDispatcher::TryRunAsync returns IAsyncOperation<bool> (whether the
// callback got to run), not IAsyncAction - measured here: MSVC rejects the
// IAsyncAction element type the brief's text used, with an exact-type
// mismatch against TryRunAsync's real return type.
thread_local std::vector<std::pair<winrt::weak_ref<wux::DependencyObject>,
                                   wf::IAsyncOperation<bool>>>
    t_delayed_fill;

bool IsBackgroundFill(wux::DependencyObject const& object,
                      wux::DependencyProperty const& property) {
    if (property != wux::Shapes::Shape::FillProperty()) {
        return false;
    }
    auto fe = object.try_as<wux::FrameworkElement>();
    return fe && fe.Name() == L"BackgroundFill" &&
           winrt::get_class_name(object) == L"Windows.UI.Xaml.Shapes.Rectangle";
}

}  // namespace

std::wstring EscapeXmlAttribute(std::wstring_view text) {
    std::wstring out;
    out.reserve(text.size());
    for (wchar_t c : text) {
        switch (c) {
            case L'&': out += L"&amp;"; break;
            case L'"': out += L"&quot;"; break;
            case L'<': out += L"&lt;"; break;
            case L'>': out += L"&gt;"; break;
            default: out.push_back(c);
        }
    }
    return out;
}

ResolvedSetter ResolveSetter(std::wstring_view type,
                             std::wstring_view fallback_type,
                             const styler::PreparedStyle& style) {
    std::wstring xaml = L"        <Setter Property=\"";
    xaml += EscapeXmlAttribute(style.property);
    xaml += L"\"";
    const bool clear = style.is_xaml && style.value.find_first_not_of(L" \t") ==
                                            std::wstring::npos;
    if (clear) {
        xaml += L" Value=\"{x:Null}\" />\n";
    } else if (!style.is_xaml) {
        xaml += L" Value=\"";
        xaml += EscapeXmlAttribute(style.value);
        xaml += L"\" />\n";
    } else {
        xaml += L">\n            <Setter.Value>\n";
        xaml += style.value;
        xaml += L"\n            </Setter.Value>\n        </Setter>\n";
    }

    wux::Style s = LoadStyleWithFallbacks(type, fallback_type, xaml);
    auto setter = s.Setters().GetAt(0).as<wux::Setter>();
    ResolvedSetter out;
    out.property = setter.Property();
    out.clear = clear;
    if (!clear) {
        out.value = setter.Value();
    }
    // The blur spec rides along with the resolved property; the value stays
    // the AcrylicBrush the markup already parsed into, so a caller that does
    // not know about blur still gets something drawable.
    out.blur = style.blur ? &*style.blur : nullptr;
    return out;
}

wf::IInspectable ReadLocalValueWithWorkaround(
    wux::DependencyObject const& object, wux::DependencyProperty const& property) {
    wf::IInspectable value = object.ReadLocalValue(property);
    if (value) {
        auto cls = winrt::get_class_name(value);
        if (cls == L"Windows.UI.Xaml.Data.BindingExpressionBase" ||
            cls == L"Windows.UI.Xaml.Data.BindingExpression") {
            value = object.GetAnimationBaseValue(property);
        }
    }
    return value;
}

void SetOrClearValue(wux::DependencyObject const& object,
                     wux::DependencyProperty const& property,
                     wf::IInspectable const& value, bool initial_apply) {
    if (IsBackgroundFill(object, property)) {
        auto it = t_delayed_fill.begin();
        for (; it != t_delayed_fill.end(); ++it) {
            if (auto live = it->first.get(); live && live == object) {
                break;
            }
        }
        if (value != wux::DependencyProperty::UnsetValue() && initial_apply &&
            it == t_delayed_fill.end()) {
            STYLER_LOG(LogLevel::Debug, L"deferring BackgroundFill.Fill");
            auto op = object.Dispatcher().TryRunAsync(
                winrt::Windows::UI::Core::CoreDispatcherPriority::High,
                [object, property, value]() {
                    // Runs later, on the dispatcher, well outside whatever
                    // ModifyingGuard scope the original SetOrClearValue call
                    // held (that one is long gone by now) - so this needs
                    // its own guard around SetValue itself, or the
                    // PropertyChanged callback it triggers looks like an
                    // external change and clobbers `original` with our own
                    // brush (review finding C2). The whole body, not just
                    // SetValue, sits inside one try: erase_if's
                    // weak_ref::get() and the captured objects' destructors
                    // ran unguarded before (review finding I1).
                    try {
                        {
                            ModifyingGuard guard;
                            object.SetValue(property, value);
                        }
                        std::erase_if(t_delayed_fill, [&](const auto& e) {
                            auto live = e.first.get();
                            return live && live == object;
                        });
                    } catch (winrt::hresult_error const& ex) {
                        STYLER_LOG(LogLevel::Error, L"deferred SetValue 0x%08X",
                                   static_cast<unsigned>(ex.code()));
                    } catch (...) {
                        STYLER_LOG(LogLevel::Error, L"deferred SetValue threw");
                    }
                });
            t_delayed_fill.push_back({winrt::make_weak(object), op});
            return;
        }
        if (it != t_delayed_fill.end()) {
            it->second.Cancel();
            t_delayed_fill.erase(it);
        }
    }

    if (value == wux::DependencyProperty::UnsetValue()) {
        object.ClearValue(property);
        return;
    }
    object.SetValue(property, value);
}

bool IsModifying() {
    return t_modifying;
}

ModifyingGuard::ModifyingGuard() : prev_(t_modifying) {
    t_modifying = true;
}

ModifyingGuard::~ModifyingGuard() {
    t_modifying = prev_;
}

}  // namespace styler::tap
