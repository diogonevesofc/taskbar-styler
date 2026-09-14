// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>

#include <styler/matcher.h>
#include <tap/winrt_common.h>

namespace styler::tap {

std::wstring EscapeXmlAttribute(std::wstring_view text);

struct ResolvedSetter {
    wux::DependencyProperty property{nullptr};
    wf::IInspectable value;  // Null when `clear` is set.
    bool clear = false;      // `Prop:=` with an empty value clears the property.
    // Non-null when the style's value was a `<WindhawkBlur .../>`. `value`
    // then holds the shared AcrylicBrush fallback, which IS safe to share
    // across elements; the real brush is NOT (it captures the element's
    // Compositor and parks a proxy in its Resources), so the engine builds
    // one per element from this spec instead of caching it here. Points into
    // the PreparedStyle inside the ResolvedTheme, which the setter cache
    // already keeps alive through its own shared_ptr.
    const styler::BlurSpec* blur = nullptr;
};

// Turns a property name into a DependencyProperty - and the style's text
// into a value XAML parsed - by loading a <Style TargetType="type"> with one
// <Setter> through XamlReader and reading it back (upstream
// vendor:15394-15440, :15595-15632). This is also how attached properties
// (Canvas.ZIndex, Grid.Column) and `:=` markup values resolve. On XAML's
// 0x802B000A ("cannot create a System.Type from the text") the load is
// retried with `fallback_type`, then with "FrameworkElement". Throws
// winrt::hresult_error when every attempt fails.
ResolvedSetter ResolveSetter(std::wstring_view type,
                             std::wstring_view fallback_type,
                             const styler::PreparedStyle& style);

// ReadLocalValue, except that a BindingExpression(Base) - observed for
// properties declared as {TemplateBinding ...} - is replaced by
// GetAnimationBaseValue, since SetValue with a binding expression fails and
// the original could never be restored (upstream vendor:12377-12400).
wf::IInspectable ReadLocalValueWithWorkaround(
    wux::DependencyObject const& object, wux::DependencyProperty const& property);

// SetValue, or ClearValue when `value` is DependencyProperty::UnsetValue().
// `initial_apply` enables the one deferral upstream needs: setting
// Rectangle#BackgroundFill.Fill before TaskbarBackground's OnApplyTemplate
// can crash, so that first set is posted to the element's dispatcher at High
// priority instead (vendor:14940-14990).
void SetOrClearValue(wux::DependencyObject const& object,
                     wux::DependencyProperty const& property,
                     wf::IInspectable const& value, bool initial_apply);

// Whether the current thread is inside a write this engine made itself, so
// that write's own PropertyChanged callback does not react to it as if the
// shell had changed the value. Every direct SetValue/ClearValue on a
// property this engine tracks - including SetOrClearValue's own deferred
// BackgroundFill.Fill set, which runs later, on the dispatcher, outside
// whatever scope its caller held - must be wrapped in a ModifyingGuard
// while it runs (found in review: the deferred set used to run unguarded,
// so its own PropertyChanged notification looked external and overwrote
// the tracked `original` with the engine's own value - restore then
// "restored" to that, never to the shell's).
bool IsModifying();

// RAII for the flag IsModifying() reads: true for the guard's lifetime,
// restoring the PREVIOUS value on scope exit - including via an exception,
// which a hand-paired `t_modifying = true; ...; t_modifying = false;` cannot
// guarantee (found in review: ApplyProperty's own call had no try/catch at
// all, so a throw left the flag stuck true for the rest of the thread's
// life). Restoring the previous value rather than hard-clearing to false
// also makes nested guards unwind correctly: a CurrentStateChanged handler
// can fire synchronously while an outer SetValue is still in flight (Task
// 6), so its own inner guard must not clear a flag an outer guard still
// needs set.
class ModifyingGuard {
public:
    ModifyingGuard();
    ~ModifyingGuard();
    ModifyingGuard(const ModifyingGuard&) = delete;
    ModifyingGuard& operator=(const ModifyingGuard&) = delete;

private:
    bool prev_;
};

}  // namespace styler::tap
