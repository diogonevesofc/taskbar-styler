// SPDX-License-Identifier: GPL-3.0-or-later
#include <tap/blur_brush.h>

#include <algorithm>
#include <atomic>
#include <string>

#include <tap/blur_effects.h>
#include <tap/log.h>

namespace styler::tap {
namespace {

// Rec. 709 luma coefficients - the same ones upstream uses for both the
// saturation and the luminosity matrices (vendor:13690-13693).
constexpr float kLumaR = 0.2126f;
constexpr float kLumaG = 0.7152f;
constexpr float kLumaB = 0.0722f;

std::atomic<uint64_t> g_proxy_counter{0};

}  // namespace

XamlBlurBrush::XamlBlurBrush(wux::UIElement const& element,
                             const styler::BlurSpec& spec)
    : m_compositor(
          wuxh::ElementCompositionPreview::GetElementVisual(element).Compositor()),
      m_spec(spec),
      m_tint(ToWinRtColor(spec.tint)) {
    if (m_spec.tint_theme_resource.empty()) {
        return;
    }
    auto fe = element.try_as<wux::FrameworkElement>();
    if (!fe) {
        STYLER_LOG(LogLevel::Error,
                   L"blur: theme resource tint needs a FrameworkElement");
        return;
    }

    // XAML has no API to evaluate a ThemeResource by key, so park a brush
    // that IS bound to it in the element's own ResourceDictionary and read
    // its Color. XAML then keeps it up to date across light/dark switches
    // for free, and the property-changed callback below turns that into a
    // rebuild of the effect graph.
    std::wstring xaml =
        L"<SolidColorBrush xmlns=\"http://schemas.microsoft.com/winfx/2006/"
        L"xaml/presentation\" Color=\"{ThemeResource " +
        m_spec.tint_theme_resource + L"}\"/>";
    try {
        m_tint_proxy = wux::Markup::XamlReader::Load(winrt::hstring(xaml))
                           .try_as<wuxm::SolidColorBrush>();
    } catch (winrt::hresult_error const& ex) {
        STYLER_LOG(LogLevel::Error, L"blur: proxy brush for %s failed 0x%08X",
                   m_spec.tint_theme_resource.c_str(),
                   static_cast<unsigned>(ex.code()));
        return;
    } catch (...) {
        return;
    }
    if (!m_tint_proxy) {
        return;
    }

    try {
        m_proxy_key = winrt::hstring(L"__TsBlurProxy_" +
                                     std::to_wstring(++g_proxy_counter));
        fe.Resources().Insert(winrt::box_value(m_proxy_key), m_tint_proxy);
        m_proxy_owner = fe;
        m_tint_changed_token = m_tint_proxy.RegisterPropertyChangedCallback(
            wuxm::SolidColorBrush::ColorProperty(),
            [weak = get_weak()](wux::DependencyObject const&,
                                wux::DependencyProperty const&) {
                try {
                    if (auto self = weak.get()) {
                        self->RefreshBrush();
                    }
                } catch (winrt::hresult_error const&) {
                } catch (...) {
                }
            });
    } catch (winrt::hresult_error const& ex) {
        STYLER_LOG(LogLevel::Error, L"blur: proxy registration failed 0x%08X",
                   static_cast<unsigned>(ex.code()));
    } catch (...) {
    }
}

XamlBlurBrush::~XamlBlurBrush() {
    try {
        if (m_tint_proxy && m_tint_changed_token) {
            m_tint_proxy.UnregisterPropertyChangedCallback(
                wuxm::SolidColorBrush::ColorProperty(), m_tint_changed_token);
        }
        // Leaving the proxy key behind would grow the element's
        // ResourceDictionary by one entry per apply/reset cycle.
        if (auto owner = m_proxy_owner.get(); owner && !m_proxy_key.empty()) {
            owner.Resources().Remove(winrt::box_value(m_proxy_key));
        }
    } catch (...) {
        // A destructor is the one place that must never let anything out.
    }
}

void XamlBlurBrush::RefreshThemeTint() {
    if (!m_tint_proxy) {
        return;
    }
    m_tint = m_tint_proxy.Color();
    // TintOpacity, when the author wrote one, wins over the alpha the theme
    // resource carries - upstream does the same (vendor:13818-13826).
    if (m_spec.tint_opacity) {
        m_tint.A = *m_spec.tint_opacity;
    }
}

void XamlBlurBrush::OnConnected() {
    try {
        if (CompositionBrush()) {
            return;
        }
        RefreshThemeTint();
        wuc::CompositionBrush brush{nullptr};
        try {
            brush = CreateEffectBrush();
        } catch (winrt::hresult_error const& ex) {
            STYLER_LOG(LogLevel::Error, L"blur: effect graph failed 0x%08X",
                       static_cast<unsigned>(ex.code()));
        }
        if (!brush) {
            brush = CreateFallbackBrush();
        }
        CompositionBrush(brush);
    } catch (winrt::hresult_error const&) {
    } catch (...) {
    }
}

void XamlBlurBrush::OnDisconnected() {
    try {
        if (auto brush = CompositionBrush()) {
            brush.Close();
            CompositionBrush(nullptr);
        }
    } catch (winrt::hresult_error const&) {
    } catch (...) {
    }
}

void XamlBlurBrush::RefreshBrush() {
    // Only when already connected: rebuilding a disconnected brush would
    // create a composition object nothing will ever show or close.
    if (!CompositionBrush()) {
        return;
    }
    // The brief's test cycle asks for one log line per proxy rebuild: it is
    // the only externally visible sign that the ThemeResource tint survived
    // a light/dark switch.
    STYLER_LOG(LogLevel::Debug, L"blur: tint proxy %s changed, rebuilding",
               m_spec.tint_theme_resource.c_str());
    OnDisconnected();
    OnConnected();
}

wuc::CompositionBrush XamlBlurBrush::CreateFallbackBrush() {
    return m_compositor.CreateColorBrush(
        m_spec.fallback_color ? ToWinRtColor(*m_spec.fallback_color) : m_tint);
}

wuc::CompositionBrush XamlBlurBrush::CreateEffectBrush() {
    auto backdrop = m_compositor.CreateBackdropBrush();

    // 1. Blur the backdrop.
    auto blur = winrt::make_self<GaussianBlurEffect>();
    blur->Source = wuc::CompositionEffectSourceParameter(L"backdrop");
    blur->BlurAmount = m_spec.blur_amount;
    blur->Name(L"BlurEffect");
    wge::IGraphicsEffectSource top = *blur;

    // 2. Saturation, as a lerp between luminance and identity.
    if (m_spec.tint_saturation && *m_spec.tint_saturation != 1.0f) {
        // Parenthesized: <windows.h> defines a `max` macro and this TU
        // does not define NOMINMAX (measured: C2589 without the parens).
        const float s = (std::max)(*m_spec.tint_saturation, 0.0f);
        const float inv = 1.0f - s;
        auto sat = winrt::make_self<ColorMatrixEffect>();
        sat->Source = top;
        auto& m = sat->Matrix;
        m = {inv * kLumaR + s, inv * kLumaR,     inv * kLumaR,     0.0f,
             inv * kLumaG,     inv * kLumaG + s, inv * kLumaG,     0.0f,
             inv * kLumaB,     inv * kLumaB,     inv * kLumaB + s, 0.0f,
             0.0f,             0.0f,             0.0f,             1.0f,
             0.0f,             0.0f,             0.0f,             0.0f};
        sat->Name(L"SaturationEffect");
        top = *sat;
    }

    // 3. Luminosity: pull each pixel's luma towards the tint's, by `op`.
    if (m_spec.tint_luminosity_opacity && *m_spec.tint_luminosity_opacity > 0.0f) {
        const float op = std::clamp(*m_spec.tint_luminosity_opacity, 0.0f, 1.0f);
        const float tint_luma = (m_tint.R / 255.0f) * kLumaR +
                                (m_tint.G / 255.0f) * kLumaG +
                                (m_tint.B / 255.0f) * kLumaB;
        auto lum = winrt::make_self<ColorMatrixEffect>();
        lum->Source = top;
        auto& m = lum->Matrix;
        m = {1.0f - kLumaR * op, -(kLumaR * op),     -(kLumaR * op),     0.0f,
             -(kLumaG * op),     1.0f - kLumaG * op, -(kLumaG * op),     0.0f,
             -(kLumaB * op),     -(kLumaB * op),     1.0f - kLumaB * op, 0.0f,
             0.0f,               0.0f,               0.0f,               1.0f,
             tint_luma * op,     tint_luma * op,     tint_luma * op,     0.0f};
        lum->Name(L"LuminosityBlend");
        top = *lum;
    }

    // 4. Noise: a wrapped tile, scaled down to NoiseOpacity, over the stack.
    wuc::CompositionSurfaceBrush noise_brush{nullptr};
    if (m_spec.noise_opacity && *m_spec.noise_opacity > 0.0f) {
        auto stream = CreateNoiseStream(m_spec.noise_density.value_or(1.0f));
        auto surface = wuxm::LoadedImageSurface::StartLoadFromStream(stream);
        noise_brush = m_compositor.CreateSurfaceBrush(surface);
        noise_brush.Stretch(wuc::CompositionStretch::None);

        auto border = winrt::make_self<BorderEffect>();
        border->Source = wuc::CompositionEffectSourceParameter(L"NoiseSource");
        border->Name(L"NoiseTile");

        const float n = std::clamp(*m_spec.noise_opacity, 0.0f, 1.0f);
        auto opacity = winrt::make_self<ColorMatrixEffect>();
        opacity->Source = *border;
        // Scale every channel, alpha included, matching upstream. D2D's
        // ColorMatrix de-premultiplies before applying, so noise amplitude
        // ends up scaling as NoiseOpacity squared (measured, Task 4 report).
        opacity->Matrix = {n,    0.0f, 0.0f, 0.0f, 0.0f, n,    0.0f, 0.0f,
                           0.0f, 0.0f, n,    0.0f, 0.0f, 0.0f, 0.0f, n,
                           0.0f, 0.0f, 0.0f, 0.0f};
        opacity->Name(L"NoiseOpacityEffect");

        auto noise_composite = winrt::make_self<CompositeEffect>();
        noise_composite->Mode = D2D1_COMPOSITE_MODE_SOURCE_OVER;
        noise_composite->Sources.push_back(top);
        noise_composite->Sources.push_back(*opacity);
        noise_composite->Name(L"NoiseComposite");
        top = *noise_composite;
    }

    // 5. The tint, flooded over everything.
    auto flood = winrt::make_self<FloodEffect>();
    flood->Color = m_tint;
    flood->Name(L"FloodEffect");

    auto composite = winrt::make_self<CompositeEffect>();
    composite->Mode = D2D1_COMPOSITE_MODE_SOURCE_OVER;
    composite->Sources.push_back(top);
    composite->Sources.push_back(*flood);

    auto factory = m_compositor.CreateEffectFactory(*composite);
    auto brush = factory.CreateBrush();
    brush.SetSourceParameter(L"backdrop", backdrop);
    if (noise_brush) {
        brush.SetSourceParameter(L"NoiseSource", noise_brush);
    }
    return brush;
}

wuxm::Brush MakeBlurBrush(wux::UIElement const& element,
                          const styler::BlurSpec& spec) {
    try {
        if (!element) {
            return nullptr;
        }
        return winrt::make<XamlBlurBrush>(element, spec);
    } catch (winrt::hresult_error const& ex) {
        STYLER_LOG(LogLevel::Error, L"blur brush creation failed 0x%08X",
                   static_cast<unsigned>(ex.code()));
        return nullptr;
    } catch (...) {
        STYLER_LOG(LogLevel::Error, L"blur brush creation failed");
        return nullptr;
    }
}

}  // namespace styler::tap
