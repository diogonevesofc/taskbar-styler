// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

#include <styler/matcher.h>
#include <tap/element_registry.h>
#include <tap/winrt_common.h>

namespace styler::tap {

// The prepared theme every thread applies. Immutable once set; replacing it
// does not touch elements already styled - callers restore first (see
// theme_session.cpp's reload). Null means "no theme".
void SetTheme(std::shared_ptr<const styler::ResolvedTheme> theme);
std::shared_ptr<const styler::ResolvedTheme> CurrentTheme();

// Called from the standing subscription, on the reporting thread.
void OnElementAdded(ElementId id, wux::FrameworkElement const& element,
                    const wchar_t* reported_type);
void OnElementRemoved(ElementId id);

// Whether this thread's engine holds customization state for `id`.
bool ElementHasState(ElementId id);

// Restores every element this thread customized and forgets them. Must run
// on the owning UI thread, outside any XAML callback.
void RestoreAllOnThisThread();

// Counters for the log and for docs/smoke-test.md, this thread only.
struct EngineStats {
    size_t styled_elements = 0;
    size_t applied_properties = 0;
    size_t failed_styles = 0;
    size_t deferred_visual_state_styles = 0;  // Task 6 turns this to zero.
    size_t blur_brushes = 0;    // Real WindhawkBlur brushes created.
    size_t blur_fallbacks = 0;  // Blurs that fell back to AcrylicBrush.
};
EngineStats StatsForThisThread();

}  // namespace styler::tap
