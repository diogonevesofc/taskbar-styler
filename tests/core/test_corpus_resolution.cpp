// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <variant>
#include <vector>

#include <nlohmann/json.hpp>
#include <styler/constants.h>
#include <styler/matcher.h>
#include <styler/style_expression.h>
#include <styler/theme_loader.h>
#include <styler/utf.h>

namespace {

// Upstream's algorithm, written independently of ResolveConstants: expand
// each constant against the ones declared BEFORE it, in declaration order,
// then keep the list sorted longest name first (vendor:18570-18600).
styler::ResolvedConstants UpstreamOrder(
    const nlohmann::ordered_json& constants) {
    styler::ResolvedConstants out;
    for (auto it = constants.begin(); it != constants.end(); ++it) {
        std::wstring name = styler::Utf8ToWide(it.key());
        std::wstring value =
            styler::Utf8ToWide(it.value().get<std::string>());
        value = styler::ApplyStyleConstants(value, out);  // earlier ones only
        auto pos = std::lower_bound(
            out.begin(), out.end(), name,
            [](const auto& e, const std::wstring& n) {
                return e.first.size() > n.size();
            });
        out.insert(pos, {name, value});
    }
    return out;
}

bool IsTheme(const std::filesystem::directory_entry& e) {
    return e.path().extension() == ".json" &&
           e.path().filename() != "credits.json";
}

}  // namespace

TEST_CASE("every shipped theme resolves constants exactly as upstream would") {
    int themes = 0;
    for (const auto& entry :
         std::filesystem::directory_iterator(STYLER_THEMES_DIR)) {
        if (!IsTheme(entry)) {
            continue;
        }
        ++themes;
        std::ifstream in(entry.path());
        auto ordered = nlohmann::ordered_json::parse(in);
        auto theme = styler::LoadThemeFromFile(entry.path());

        auto ours = styler::ResolveConstants(theme.constants);
        auto upstream = UpstreamOrder(
            ordered.value("constants", nlohmann::ordered_json::object()));

        std::map<std::wstring, std::wstring> a(ours.begin(), ours.end());
        std::map<std::wstring, std::wstring> b(upstream.begin(), upstream.end());
        INFO("theme " << entry.path().filename().string());
        CHECK(a == b);

        // And every style text expands identically under both.
        for (const auto& rule : ordered["rules"]) {
            for (const auto& style : rule["styles"]) {
                std::wstring s = styler::Utf8ToWide(style.get<std::string>());
                CHECK(styler::ApplyStyleConstants(s, ours) ==
                      styler::ApplyStyleConstants(s, upstream));
            }
        }
    }
    CHECK(themes == 55);
}

TEST_CASE("only Fluid keeps an unresolved dollar") {
    // Spec section 7.6's tolerance for an unmatched `$` is what Fluid
    // relies on. Luminosity_variant_Dock and Luminosity_variant_Compact
    // reference $WidgetGap57, which upstream's longest-prefix rule resolves
    // as WidgetGap + a literal "57" (vendor LoadStyleConstants,
    // ApplyStyleConstants) - so they are not in this set.
    std::set<std::wstring> with_unresolved;
    for (const auto& entry :
         std::filesystem::directory_iterator(STYLER_THEMES_DIR)) {
        if (!IsTheme(entry)) {
            continue;
        }
        auto theme = styler::LoadThemeFromFile(entry.path());
        auto constants = styler::ResolveConstants(theme.constants);
        for (const auto& rule : theme.rules) {
            for (const auto& style : rule.styles) {
                const auto* v = std::get_if<styler::ValueRule>(&style);
                if (v && styler::ApplyStyleConstants(v->value, constants)
                                 .find(L'$') != std::wstring::npos) {
                    with_unresolved.insert(theme.id);
                }
            }
        }
    }
    CHECK(with_unresolved == std::set<std::wstring>{L"Fluid"});
}

TEST_CASE("every dynamic value in the corpus parses with its variables bound") {
    namespace fs = std::filesystem;
    // Bind every name any theme captures to a plausible number, then expand
    // every dynamic value in the corpus. A value that comes back nullopt is
    // a grammar gap, not a theme bug: with all its variables defined and
    // numeric, every shipped expression must evaluate.
    std::map<std::wstring, styler::StyleVariableValue> table;
    std::vector<std::pair<std::wstring, std::wstring>> failures;
    int dynamic_total = 0;
    int expanded = 0;
    int expected_skips = 0;

    for (int pass = 0; pass < 2; ++pass) {
        for (const auto& entry : fs::directory_iterator(STYLER_THEMES_DIR)) {
            if (entry.path().extension() != ".json" ||
                entry.path().filename() == "credits.json") {
                continue;
            }
            styler::Theme theme = styler::LoadThemeFromFile(entry.path().wstring());
            for (const auto& rule : theme.rules) {
                for (const auto& style : rule.styles) {
                    if (const auto* c = std::get_if<styler::CaptureRule>(&style)) {
                        if (pass == 0) {
                            table[c->var_name] = styler::StyleVariableValue{
                                L"64", 64.0, true};
                        }
                        continue;
                    }
                }
            }
            if (pass == 0) {
                continue;
            }
            styler::ResolvedTheme resolved = styler::PrepareTheme(theme);
            auto lookup = [&table](std::wstring_view n)
                -> const styler::StyleVariableValue* {
                auto it = table.find(std::wstring(n));
                return it == table.end() ? nullptr : &it->second;
            };
            for (const auto& prepared_rule : resolved.rules) {
                for (const auto& style : prepared_rule.styles) {
                    if (!style.dynamic) {
                        continue;
                    }
                    ++dynamic_total;
                    std::vector<std::wstring> deps;
                    auto out = styler::ExpandStyleVariables(style.value, lookup,
                                                            &deps);
                    if (out) {
                        ++expanded;
                    }
                    // Two expected skips, both bare references to a name no
                    // theme in the corpus ever captures - so upstream's own
                    // evaluator skips them too, same as ours:
                    //   * Pills' `{{__unset}}`, a deliberate "leave this
                    //     alone" sentinel reached through a $constant;
                    //   * LiquidGlass2's `Visibility={{clickThroughTaskbar}}`
                    //     (measured: the only occurrence of the name in the
                    //     entire corpus, and it is never the target of a
                    //     `Prop=>clickThroughTaskbar` capture rule anywhere -
                    //     grepped themes/*.json - nor is it seeded any other
                    //     way; `clickThroughTaskbar` is only otherwise a
                    //     Windhawk MOD SETTING (vendor:598,
                    //     g_settings.clickThroughTaskbar), not a style
                    //     variable, so this is not a grammar gap).
                    if (!out) {
                        if (style.value.find(L"__unset") != std::wstring::npos ||
                            style.value.find(L"clickThroughTaskbar") !=
                                std::wstring::npos) {
                            ++expected_skips;
                        } else {
                            failures.emplace_back(resolved.id, style.value);
                        }
                    }
                }
            }
        }
    }
    for (const auto& [theme_id, value] : failures) {
        MESSAGE("unevaluable dynamic value in ", styler::WideToUtf8(theme_id),
                ": ", styler::WideToUtf8(value));
    }
    MESSAGE(dynamic_total, " dynamic values in the corpus: ", expanded,
            " expanded, ", expected_skips, " expected skips (sentinels)");
    CHECK(failures.empty());
}
