// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <styler/blur.h>
#include <styler/selector.h>
#include <styler/theme.h>

namespace styler {

// One style after preparation: constants applied, blur rewritten. `value`
// is XAML markup when `is_xaml` (an empty XAML value means "clear the
// property", upstream `Fill:=`); otherwise it is attribute text.
struct PreparedStyle {
    std::wstring property;
    std::wstring visual_state;  // Empty: unconditional.
    std::wstring value;
    bool is_xaml = false;
    // Engaged when `value` came from a `<WindhawkBlur .../>` element. `value`
    // then holds the AcrylicBrush fallback markup (blur_rewrite.h) and stays
    // usable as-is; the TAP prefers `blur` and only parses `value` when the
    // real brush cannot be built.
    std::optional<BlurSpec> blur;
    // True when `value` still contains `{{ ... }}`. The value cannot be
    // resolved at preparation time - it depends on live captured properties -
    // so the engine expands it per element and re-expands it whenever a
    // variable it depends on changes.
    bool dynamic = false;
};

struct PreparedRule {
    std::vector<std::vector<ElementMatcher>> chains;  // Types expanded.
    std::vector<PreparedStyle> styles;
    size_t source_index = 0;  // Index into Theme::rules, for logs.
};

// A theme ready to apply. Immutable once built; shared across threads.
struct ResolvedTheme {
    std::wstring id;
    std::vector<PreparedRule> rules;  // Dead rules dropped, order kept.
    std::map<std::wstring, std::wstring> resource_variables;
    std::vector<std::wstring> diagnostics;  // Theme's own + what was skipped.
    int skipped_captures = 0;     // `Prop=>Var` - Plano 3b.
    int dynamic_values = 0;       // `{{Var}}` values left for the engine - Plano 3b/5.
    // Distinct blur SOURCES parsed into a real BlurSpec: once per constant
    // whose own value is a `<WindhawkBlur>`/`<Blur>` tag, once per inline
    // style that writes the tag itself - not a count of styles using the
    // resulting brush (a constant reused by many rules is still one source).
    int blur_specs = 0;
    int blur_approximations = 0;  // Blur values that only got the AcrylicBrush rewrite.
};

ResolvedTheme PrepareTheme(const Theme& theme);

// What the matcher needs to know about one live element. The TAP adapts a
// FrameworkElement to this; tests use a fake tree. Every method is called
// only while the element is alive on its own UI thread.
class ElementView {
public:
    virtual ~ElementView() = default;
    // Runtime class name (winrt::get_class_name).
    virtual std::wstring TypeName() const = 0;
    // The type the diagnostics reported for this element; accepted as an
    // alternative to TypeName() for the LEAF only (upstream passes the
    // fallback for the matched element and nullptr for its ancestors).
    virtual std::wstring ReportedTypeName() const = 0;
    virtual std::wstring Name() const = 0;
    virtual std::unique_ptr<ElementView> Parent() const = 0;  // null at root.
    virtual int IndexInParent() const = 0;  // 0-based; -1 without a parent.
    // Whether the element's local value of `property` equals `expected` as
    // XAML would parse it. nullopt when it cannot be read or compared - the
    // matcher treats that as "does not match".
    virtual std::optional<bool> PropertyEquals(
        std::wstring_view property, std::wstring_view expected) const = 0;
};

// `@Group` found on a matcher in the chain: the group lives on the element
// `ancestor_depth` parents above the leaf (0 = the leaf itself).
struct VisualStateGroupRef {
    std::wstring name;
    int ancestor_depth = 0;
};

struct RuleMatch {
    const PreparedRule* rule = nullptr;
    std::optional<VisualStateGroupRef> vsg;
};

// `chain` is outermost ancestor first, leaf last (ParseSelector order).
// When `vsg` is non-null it receives the last `@Group` encountered while
// walking, mirroring upstream's single visualStateGroup out-param
// (vendor/upstream/windows-11-taskbar-styler.wh.cpp:15936-16000).
bool MatchesChain(const ElementView& leaf,
                  const std::vector<ElementMatcher>& chain,
                  std::optional<VisualStateGroupRef>* vsg);

// Every rule whose ANY chain matches `leaf`, last theme rule first: that is
// the precedence upstream applies (it walks rules from rbegin and the first
// rule to claim a property keeps it). Consumers dedupe per property in the
// order returned.
std::vector<RuleMatch> FindMatchingRules(const ResolvedTheme& theme,
                                         const ElementView& leaf);

}  // namespace styler
