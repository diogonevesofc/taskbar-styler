// SPDX-License-Identifier: GPL-3.0-or-later
#include <styler/matcher.h>

#include <variant>

#include <styler/blur_rewrite.h>
#include <styler/constants.h>
#include <styler/style_rule.h>
#include <styler/type_name.h>

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
    // The per-style rewrite further down stays: it still catches blur XAML
    // written directly in a style value with no constant involved, and
    // re-rewriting an already-rewritten AcrylicBrush value is a no-op, so
    // `blur_approximations` counts each distinct rewrite exactly once.
    for (auto& [name, value] : constants) {
        bool rewritten = false;
        value = RewriteWindhawkBlur(value, &rewritten);
        if (rewritten) {
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
            if (std::holds_alternative<CaptureRule>(style)) {
                ++out.skipped_captures;
                out.diagnostics.push_back(theme.id + L": " + src.target +
                                          L": capture rule skipped (Plano 3b)");
                continue;
            }
            const ValueRule& v = std::get<ValueRule>(style);
            if (v.IsDynamic()) {
                ++out.skipped_dynamic;
                out.diagnostics.push_back(theme.id + L": " + src.target + L": " +
                                          v.property_name +
                                          L": dynamic value skipped (Plano 3b)");
                continue;
            }
            PreparedStyle p;
            p.property = v.property_name;
            p.visual_state = v.visual_state;
            p.is_xaml = v.is_xaml_value;
            p.value = ApplyStyleConstants(v.value, constants);
            if (p.is_xaml) {
                bool rewritten = false;
                p.value = RewriteWindhawkBlur(p.value, &rewritten);
                if (rewritten) {
                    ++out.blur_approximations;
                }
            }
            rule.styles.push_back(std::move(p));
        }
        if (rule.styles.empty()) {
            continue;  // Nothing left to apply (e.g. only captures).
        }
        out.rules.push_back(std::move(rule));
    }
    return out;
}

}  // namespace styler
