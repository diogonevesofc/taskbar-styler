// SPDX-License-Identifier: GPL-3.0-or-later
#include <styler/matcher.h>

#include <variant>

#include <styler/blur_rewrite.h>
#include <styler/constants.h>
#include <styler/style_rule.h>
#include <styler/type_name.h>
#include <styler/utf.h>

#include "detail/text.h"

namespace styler {
namespace {

// TestElementMatcher (vendor:15855-15922) over the abstract view. `depth`
// is how many parents above the leaf `element` sits, for the VSG ref.
bool TestMatcher(const ElementView& element, const ElementMatcher& m,
                 bool allow_reported_type, int depth,
                 std::optional<VisualStateGroupRef>* vsg) {
    if (!m.type.empty()) {
        if (m.type != element.TypeName() &&
            !(allow_reported_type && m.type == element.ReportedTypeName())) {
            return false;
        }
    }
    if (!m.name.empty() && m.name != element.Name()) {
        return false;
    }
    if (m.one_based_index) {
        int index = element.IndexInParent();
        if (index < 0 || index + 1 != m.one_based_index) {
            return false;
        }
    }
    for (const auto& [property, expected] : m.property_filters) {
        std::optional<bool> eq = element.PropertyEquals(property, expected);
        if (!eq.has_value() || !*eq) {
            return false;
        }
    }
    if (m.visual_state_group && vsg) {
        *vsg = VisualStateGroupRef{*m.visual_state_group, depth};
    }
    return true;
}

// `parents` is nearest ancestor first (the chain reversed, leaf removed).
// Recursive so that '*' can backtrack: when a candidate for the wildcard's
// next matcher fails further up, retry with a farther ancestor
// (vendor:15947-15995).
bool MatchParents(const ElementView& iter, int depth,
                  const std::vector<const ElementMatcher*>& parents, size_t mi,
                  std::optional<VisualStateGroupRef>* vsg) {
    if (mi >= parents.size()) {
        return true;
    }
    const ElementMatcher& m = *parents[mi];

    if (m.kind == ElementMatcher::Kind::Root) {
        if (iter.Parent()) {
            return false;
        }
        return MatchParents(iter, depth, parents, mi + 1, vsg);
    }

    if (m.kind == ElementMatcher::Kind::Wildcard) {
        // Fail closed on a '*' that is last or not followed by an Element
        // matcher: upstream validates that at parse time; ours does not yet
        // (Plano 1 deferred it), so the chain simply never matches.
        if (mi + 1 >= parents.size() ||
            parents[mi + 1]->kind != ElementMatcher::Kind::Element) {
            return false;
        }
        const ElementMatcher& next = *parents[mi + 1];
        std::unique_ptr<ElementView> cur = iter.Parent();
        int cur_depth = depth + 1;
        while (cur) {
            if (TestMatcher(*cur, next, false, cur_depth, vsg) &&
                MatchParents(*cur, cur_depth, parents, mi + 2, vsg)) {
                return true;
            }
            cur = cur->Parent();
            ++cur_depth;
        }
        return false;
    }

    std::unique_ptr<ElementView> parent = iter.Parent();
    if (!parent) {
        return false;
    }
    if (!TestMatcher(*parent, m, false, depth + 1, vsg)) {
        return false;
    }
    return MatchParents(*parent, depth + 1, parents, mi + 1, vsg);
}

}  // namespace

bool MatchesChain(const ElementView& leaf,
                  const std::vector<ElementMatcher>& chain,
                  std::optional<VisualStateGroupRef>* vsg) {
    if (chain.empty() || chain.back().kind != ElementMatcher::Kind::Element) {
        return false;
    }
    std::optional<VisualStateGroupRef> found;
    if (!TestMatcher(leaf, chain.back(), true, 0, &found)) {
        return false;
    }
    std::vector<const ElementMatcher*> parents;
    parents.reserve(chain.size() - 1);
    for (size_t i = chain.size() - 1; i-- > 0;) {
        parents.push_back(&chain[i]);
    }
    if (!MatchParents(leaf, 0, parents, 0, &found)) {
        return false;
    }
    if (vsg) {
        *vsg = found;
    }
    return true;
}

std::vector<RuleMatch> FindMatchingRules(const ResolvedTheme& theme,
                                         const ElementView& leaf) {
    std::vector<RuleMatch> out;
    for (size_t i = theme.rules.size(); i-- > 0;) {
        const PreparedRule& rule = theme.rules[i];
        for (const auto& chain : rule.chains) {
            std::optional<VisualStateGroupRef> vsg;
            if (MatchesChain(leaf, chain, &vsg)) {
                out.push_back(RuleMatch{&rule, vsg});
                break;  // Chains are alternatives: one match is enough.
            }
        }
    }
    return out;
}

ResolvedTheme PrepareTheme(const Theme& theme) {
    ResolvedTheme out;
    out.id = theme.id;
    out.diagnostics = theme.diagnostics;

    ResolvedConstants constants = ResolveConstants(theme.constants);
    // A copy taken before the AcrylicBrush rewrite below, so the style loop
    // can see a blur constant in its original `<WindhawkBlur .../>` form and
    // parse it into a BlurSpec. `constants` itself keeps the rewritten form
    // because resource variables (merged into a XAML ResourceDictionary) have
    // no BlurSpec path and need real markup.
    const ResolvedConstants raw_constants = constants;

    // A constant whose value is itself a whole <WindhawkBlur>/<Blur> element
    // (e.g. upstream's `Glass=<WindhawkBlur .../>` pattern) is rewritten
    // once here, so every substitution site sees the AcrylicBrush form -
    // this covers resource variables just below, which have no `is_xaml`
    // flag of their own to gate a rewrite on (they are always merged into a
    // XAML ResourceDictionary, see src/tap/resource_variables.h). Deviation
    // from the brief's Step 4 listing, which only rewrote inside the rule
    // styles loop below and left resource variables on the raw
    // WindhawkBlur text - that fails this file's own
    // "PrepareTheme ... rewrites blur" test (resource_variables.at("Accent")
    // expects the AcrylicBrush form). RewriteWindhawkBlur no-ops on anything
    // that isn't the blur tag, so this is safe for the overwhelming
    // majority of constants that hold plain colors/numbers/XAML snippets.
    //
    // This also decides, ONCE per distinct constant, whether it counts as a
    // real BlurSpec or as a plain approximation - a second deviation from
    // the brief's Step 5 listing, which only counted in the styles loop
    // below. Counting there too double-counts a constant reused by several
    // rules: measured against the corpus, that inflated `blur_specs` to 439
    // (expected 272) and left `blur_approximations` above 0 for 17 themes
    // whose one blur constant parses just fine - contradicting both this
    // file's own corpus test (test_blur.cpp) and the brief's own
    // TranslucentTaskbar smoke-test line ("0 blur approximations"). The
    // styles loop below still parses each use (every style needs its own
    // `PreparedStyle::blur`), it just skips re-counting a value that came
    // from a constant (see the "written directly ... IN THE STYLE ITSELF"
    // check there) since it is counted here instead - restoring the
    // invariant the brief documents: blur_approximations counts only blurs
    // that never became a BlurSpec anywhere.
    for (auto& [name, value] : constants) {
        std::optional<BlurSpec> spec;
        try {
            spec = ParseWindhawkBlur(value);
        } catch (const ParseError& ex) {
            out.diagnostics.push_back(theme.id + L": constant $" + name +
                                      L": " + Utf8ToWide(ex.what()) +
                                      L" (blur)");
        }
        bool rewritten = false;
        value = RewriteWindhawkBlur(value, &rewritten);
        if (spec) {
            ++out.blur_specs;
        } else if (rewritten) {
            ++out.blur_approximations;
        }
    }

    for (const auto& [name, value] : theme.resource_variables) {
        out.resource_variables[name] = ApplyStyleConstants(value, constants);
    }

    for (size_t i = 0; i < theme.rules.size(); ++i) {
        const ThemeRule& src = theme.rules[i];
        if (src.dead) {
            continue;  // Spec section 7.6: consumers MUST check `dead`.
        }
        PreparedRule rule;
        rule.source_index = i;
        rule.chains = src.selector;
        for (auto& chain : rule.chains) {
            for (auto& m : chain) {
                if (m.kind == ElementMatcher::Kind::Element && !m.type.empty()) {
                    m.type = AdjustTypeName(m.type);
                }
            }
        }
        for (const StyleRule& style : src.styles) {
            if (const auto* capture = std::get_if<CaptureRule>(&style)) {
                rule.captures.push_back(
                    PreparedCapture{capture->property_name, capture->var_name});
                ++out.captures;
                continue;
            }
            const ValueRule& v = std::get<ValueRule>(style);
            PreparedStyle p;
            p.property = v.property_name;
            p.visual_state = v.visual_state;
            p.is_xaml = v.is_xaml_value;
            // Substitute constants BEFORE checking for a dynamic marker, not
            // after (found in review): a theme can hide `{{...}}` inside a
            // $constant - Pills' `taskbarFill` resolves to the literal
            // `{{__unset}}` - and ValueRule::IsDynamic() on the raw,
            // pre-substitution text never sees it, so the style used to
            // reach ResolveSetter and fail there instead of being skipped
            // here. Checking the resolved text for "{{" is a strict
            // superset of the old raw-text check: substitution never
            // introduces a "{{" that was not already produced by a
            // constant's own value, and never removes one already in the
            // style's own literal text either (it only rewrites `$Name`
            // tokens). Mirrors upstream: a `{{Var}}` nobody defines means
            // "leave this alone" (vendor:400-404).
            p.value = ApplyStyleConstants(v.value, constants);
            if (p.value.find(L"{{") != std::wstring::npos) {
                // Left for the engine: the value depends on live captured
                // properties, so it is expanded per element (Task 6) and
                // re-expanded on every change. Constants are substituted
                // FIRST (found in the Plano 3 review): Pills hides
                // `{{__unset}}` inside a $constant, and checking the raw text
                // would miss it.
                p.dynamic = true;
                ++out.dynamic_values;
            }
            // A dynamic value is never blur-parsed here: a `<WindhawkBlur>`
            // with `{{...}}` inside does not occur in the corpus, and
            // ResolveSetter's cached-by-PreparedStyle* blur/value would be
            // the wrong one once the engine (Task 6) starts expanding it
            // per element - p.blur must stay unset for it.
            if (p.is_xaml && !p.dynamic) {
                try {
                    // The constants pass above already rewrote any blur that
                    // came in through a $Constant, so parse the ORIGINAL text
                    // of this style as well as the substituted one: a blur
                    // written inline reaches here untouched, a blur that
                    // arrived via a constant reaches here already as
                    // AcrylicBrush. Trying the raw value first recovers the
                    // second case without undoing the rewrite that resource
                    // variables still need.
                    std::wstring raw = ApplyStyleConstants(v.value, raw_constants);
                    std::optional<BlurSpec> spec = ParseWindhawkBlur(raw);
                    if (spec) {
                        p.blur = std::move(spec);
                        bool rewritten = false;
                        p.value = RewriteWindhawkBlur(raw, &rewritten);
                        // A value written directly as `<WindhawkBlur .../>`
                        // (or `<Blur .../>`) IN THE STYLE ITSELF is counted
                        // here, once per style - even when one attribute is
                        // itself `$Parameterized` (Aeris' and Windows7's
                        // `BlurAmount="$taskbarBlurIncreace"` /
                        // `TintColor="$aeroColor"`: `raw` differs from
                        // `v.value` there too, so comparing the two texts
                        // is not the right test). A bare `$Name` reference
                        // whose OWN value is the whole tag was already
                        // counted once, above, at the constant - counting it
                        // again for every rule that reuses the constant
                        // would inflate blur_specs by however many rules do.
                        if (detail::Trim(v.value).starts_with(L"<")) {
                            ++out.blur_specs;
                        }
                    } else {
                        bool rewritten = false;
                        p.value = RewriteWindhawkBlur(p.value, &rewritten);
                        if (rewritten) {
                            ++out.blur_approximations;
                        }
                    }
                } catch (const ParseError& ex) {
                    // Fails closed per spec section 7.6: a malformed
                    // <WindhawkBlur> does not take the whole theme down, only
                    // this one style - which still gets the AcrylicBrush
                    // fallback below, same as any other unparseable blur.
                    out.diagnostics.push_back(theme.id + L": " + src.target +
                                              L": " + Utf8ToWide(ex.what()) +
                                              L" (blur)");
                    bool rewritten = false;
                    p.value = RewriteWindhawkBlur(p.value, &rewritten);
                    if (rewritten) {
                        ++out.blur_approximations;
                    }
                }
            }
            rule.styles.push_back(std::move(p));
        }
        if (rule.styles.empty() && rule.captures.empty()) {
            continue;  // Nothing to apply and nothing to capture.
        }
        out.rules.push_back(std::move(rule));
    }
    return out;
}

}  // namespace styler
