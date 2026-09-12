// SPDX-License-Identifier: GPL-3.0-or-later
#include <styler/theme_loader.h>

#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include <styler/utf.h>

namespace styler {
namespace {

using json = nlohmann::json;

std::wstring RequiredString(const json& obj, const char* key) {
    auto it = obj.find(key);
    if (it == obj.end() || !it->is_string()) {
        throw ParseError(std::string("Missing or non-string field: ") + key);
    }
    auto value = Utf8ToWide(it->get<std::string>());
    if (value.empty()) {
        throw ParseError(std::string("Empty field: ") + key);
    }
    return value;
}

std::map<std::wstring, std::wstring> OptionalMap(const json& obj,
                                                 const char* key) {
    std::map<std::wstring, std::wstring> out;
    auto it = obj.find(key);
    if (it == obj.end() || it->is_null()) {
        return out;
    }
    if (!it->is_object()) {
        throw ParseError(std::string("Field must be an object: ") + key);
    }
    for (auto& [k, v] : it->items()) {
        if (!v.is_string()) {
            throw ParseError(std::string("Non-string value in: ") + key);
        }
        out.emplace(Utf8ToWide(k), Utf8ToWide(v.get<std::string>()));
    }
    return out;
}

}  // namespace

Theme LoadThemeFromJson(std::string_view utf8) {
    json doc = json::parse(utf8, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        throw ParseError("Theme is not a JSON object");
    }

    Theme theme;
    theme.id = RequiredString(doc, "id");
    theme.name = RequiredString(doc, "name");

    if (auto it = doc.find("author"); it != doc.end() && it->is_string()) {
        theme.author = Utf8ToWide(it->get<std::string>());
    }

    theme.constants = OptionalMap(doc, "constants");
    theme.resource_variables = OptionalMap(doc, "resourceVariables");

    auto rules_it = doc.find("rules");
    if (rules_it == doc.end() || !rules_it->is_array()) {
        throw ParseError("Missing or non-array field: rules");
    }

    for (const auto& entry : *rules_it) {
        if (!entry.is_object()) {
            throw ParseError("Rule entry is not an object");
        }

        ThemeRule rule;
        rule.target = RequiredString(entry, "target");
        // One shipped target (LiquidGlass2's SnapLayoutControl /
        // LayoutBorder rule) glues two segments with a plain space instead
        // of '>' - a real authoring bug in the upstream C++ source, not a
        // syntax this loader should learn to accept. Upstream's own
        // ElementMatcherFromString (vendor/upstream/...:18628) throws the
        // identical "more than one name" error on it, and
        // AddElementCustomizationRules (vendor/upstream/...:18956) catches
        // that per target and logs it, so the target is never registered -
        // it is already dead at runtime. Only THIS narrow error class is
        // tolerated (see AmbiguousMatcherError); leaving `selector` empty
        // preserves upstream's dead-target outcome (an empty selector
        // matches nothing) while keeping the rule itself in the theme, so
        // the rule count some tools rely on (see test_corpus.cpp) still
        // reflects one entry per shipped ThemeTargetStyles block. Every
        // other selector error (empty type, unmatched bracket, ...) still
        // fails the whole theme closed, unchanged.
        try {
            rule.selector = ParseSelectorGroups(rule.target);
        } catch (const AmbiguousMatcherError&) {
            rule.selector.clear();
        }

        auto styles_it = entry.find("styles");
        if (styles_it == entry.end() || !styles_it->is_array()) {
            throw ParseError("Missing or non-array field: styles");
        }
        for (const auto& s : *styles_it) {
            if (!s.is_string()) {
                throw ParseError("Style entry is not a string");
            }
            auto raw = s.get<std::string>();
            // A handful of shipped rules (e.g. LiquidGlass2's #DisplayName
            // and #Iconlmage targets) carry a literal empty style string in
            // the upstream C++ source (`L""`). Upstream's own ParseRule
            // (vendor/upstream/...:18727) throws "'=' is missing" on it, and
            // AddElementCustomizationRules (vendor/upstream/...:18956)
            // catches that per target and logs it - discarding the whole
            // target's customization, but never the theme. It is already
            // inert at runtime, so skip it here rather than fail the theme;
            // any other malformed entry (missing '=', etc.) still throws via
            // ParseStyleRule below, unchanged.
            if (raw.empty()) {
                continue;
            }
            rule.styles.push_back(ParseStyleRule(Utf8ToWide(raw)));
        }

        theme.rules.push_back(std::move(rule));
    }

    if (auto it = doc.find("osFeatureVariant");
        it != doc.end() && !it->is_null()) {
        if (!it->is_object()) {
            throw ParseError("osFeatureVariant must be an object");
        }
        OsFeatureVariant variant;
        auto fid = it->find("featureId");
        if (fid == it->end() || !fid->is_number_unsigned()) {
            throw ParseError("osFeatureVariant.featureId must be a number");
        }
        variant.feature_id = fid->get<std::uint32_t>();
        variant.theme_id = RequiredString(*it, "themeId");
        theme.os_feature_variant = std::move(variant);
    }

    return theme;
}

Theme LoadThemeFromFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        throw ParseError("Cannot open theme file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    return LoadThemeFromJson(buffer.str());
}

}  // namespace styler
