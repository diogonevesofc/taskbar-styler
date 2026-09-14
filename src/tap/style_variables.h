// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <styler/style_expression.h>
#include <tap/element_registry.h>
#include <tap/winrt_common.h>

namespace styler::tap {

// The identity of one live element's position in the visual tree: every
// ancestor's raw pointer, root first, the element itself last. Used only to
// score "which capturing element is closest to this consumer", by comparing
// how long a common prefix two chains share - upstream keeps a cached node
// graph for the same answer (ElementTreeLcaDepth, vendor:11728-11750). The
// pointers are never dereferenced; they are identity tokens, and a chain is
// recomputed on every capture change rather than cached, so a reparented
// element cannot leave a stale spine behind.
std::vector<void*> AncestorChain(wux::FrameworkElement const& element);

// Registers `element` as the source of `name`, reading `property`. Seeds the
// variable with the property's current value and subscribes to changes:
// SizeChanged for the layout-driven ActualWidth / ActualHeight (which never
// raise a property-changed callback, vendor:17199-17206), the property
// callback for everything else. Re-registering the same (element, property)
// is a no-op that logs.
void RegisterCapture(ElementId id, wux::FrameworkElement const& element,
                     wux::DependencyProperty const& property,
                     const std::wstring& name);

// Records that (`id`, `property`) used `deps` the last time its value was
// expanded, replacing whatever it used before. An empty `deps` unregisters it.
void RegisterConsumer(ElementId id, wux::DependencyProperty const& property,
                      const std::vector<std::wstring>& deps);

// The value of `name` as seen from a consumer whose ancestor chain is
// `consumer_chain`: of all the elements currently capturing `name`, the one
// sharing the longest ancestor prefix with the consumer wins; between equally
// close ones, the most recently registered. Null when nothing captures it.
const styler::StyleVariableValue* LookupForConsumer(
    std::wstring_view name, const std::vector<void*>& consumer_chain);

// A lookup bound to one consumer's chain, ready for ExpandStyleVariables.
styler::StyleVariableLookup LookupFor(const std::vector<void*>& consumer_chain);

// Drops every capture and every consumer registration this element owns, and
// propagates the loss of any variable it was the last source of.
void ForgetElementVariables(ElementId id);

// Drops everything on this thread. Called when the theme changes or the
// session ends, before the engine restores elements.
void ClearStyleVariablesOnThisThread();

// Installed once by the style engine: re-expands and re-applies one property
// of one element. style_variables.cpp calls it for every consumer of a
// variable whose value changed; keeping it a callback is what stops
// style_variables.cpp and style_engine.cpp from including each other.
using ReapplyPropertyFn =
    std::function<void(ElementId, wux::DependencyProperty const&)>;
void SetReapplyPropertyCallback(ReapplyPropertyFn fn);

// How many variables are currently defined on this thread, for the log.
size_t DefinedVariableCount();

}  // namespace styler::tap
