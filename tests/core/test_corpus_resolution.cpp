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

TEST_CASE("only the three known themes keep an unresolved dollar") {
    // Spec section 7.6: Luminosity_variant_Dock, Luminosity_variant_Compact
    // and Fluid reference names no constant defines; upstream lets those
    // through as literals.
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
    CHECK(with_unresolved ==
          std::set<std::wstring>{L"Luminosity_variant_Dock",
                                 L"Luminosity_variant_Compact", L"Fluid"});
}
