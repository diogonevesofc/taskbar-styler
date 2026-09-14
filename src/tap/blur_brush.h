// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <styler/blur.h>
#include <tap/winrt_common.h>

namespace styler::tap {

// The real `<WindhawkBlur .../>`: a XamlCompositionBrushBase whose
// CompositionBrush is an effect graph over the window backdrop (blur, then
// optional saturation, luminosity and noise, then the tint composited on
// top). Ported from upstream's XamlBlurBrush (vendor:13356-13905), which is
// itself TranslucentTB's.
//
// Deliberately NOT ported: the energy-saver / "transparency effects off"
// switch to a flat fallback brush. Upstream's own ShouldUseFallback returns
// false outright when the markup declares no FallbackColor
// (vendor:13843-13854), and no `<WindhawkBlur>` in the 55 shipped themes
// declares one (272 tags measured) - so that whole path, including its
// RegNotifyChangeKeyValue watch on the Power key, is unreachable for every
// theme this project ships. `FallbackColor` is still parsed and is still the
// color used when the effect graph cannot be built at all.
class XamlBlurBrush : public wuxm::XamlCompositionBrushBaseT<XamlBlurBrush> {
   public:
    XamlBlurBrush(wux::UIElement const& element, const styler::BlurSpec& spec);
    ~XamlBlurBrush();

    void OnConnected();
    void OnDisconnected();

   private:
    wuc::CompositionBrush CreateEffectBrush();
    wuc::CompositionBrush CreateFallbackBrush();
    void RefreshThemeTint();
    void RefreshBrush();

    wuc::Compositor m_compositor{nullptr};
    styler::BlurSpec m_spec;
    winrt::Windows::UI::Color m_tint{};
    // Set only when the markup used TintColor="{ThemeResource Key}": a
    // SolidColorBrush bound to that key, parked in the element's Resources so
    // XAML keeps re-evaluating it across light/dark switches, and watched so
    // the effect graph is rebuilt when it changes.
    wuxm::SolidColorBrush m_tint_proxy{nullptr};
    winrt::weak_ref<wux::FrameworkElement> m_proxy_owner;
    winrt::hstring m_proxy_key;
    long long m_tint_changed_token = 0;
};

// Builds a XamlBlurBrush for `element`, or returns nullptr when it cannot be
// built (no Compositor for this element, or the effect factory refused the
// graph). Never throws: the caller is inside the style engine's per-property
// try, but a null return is the documented way to ask for the AcrylicBrush
// fallback, and distinguishing "failed" from "threw" would not change what
// the caller does.
wuxm::Brush MakeBlurBrush(wux::UIElement const& element,
                          const styler::BlurSpec& spec);

}  // namespace styler::tap
