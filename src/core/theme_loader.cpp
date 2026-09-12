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
        rule.selector = ParseSelector(rule.target);

        auto styles_it = entry.find("styles");
        if (styles_it == entry.end() || !styles_it->is_array()) {
            throw ParseError("Missing or non-array field: styles");
        }
        for (const auto& s : *styles_it) {
            if (!s.is_string()) {
                throw ParseError("Style entry is not a string");
            }
            rule.styles.push_back(
                ParseStyleRule(Utf8ToWide(s.get<std::string>())));
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
