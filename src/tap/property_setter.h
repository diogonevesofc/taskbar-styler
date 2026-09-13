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

}  // namespace styler::tap
