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
        // ParseSelectorGroups already drops any individual comma-separated
        // chain that hits the one tolerated selector error
        // (AmbiguousMatcherError - see selector.h/.cpp), keeping sibling
        // chains alive. What's left here is deciding what an empty (or
        // shortened) result means for the RULE: LiquidGlass2's
        // SnapLayoutControl/LayoutBorder target (two matchers glued by a
        // space instead of '>', a real upstream authoring bug) is a single
        // chain, so dropping its one bad chain leaves nothing - the rule
        // matches nothing and is dead. Upstream's own
        // ElementMatcherFromString (vendor/upstream/...:18628) throws the
        // identical "more than one name" error on it, and
        // AddElementCustomizationRules (vendor/upstream/...:18956) catches
        // that per target and never registers it - already dead at runtime.
        // Unlike upstream, we say so instead of staying silent.
        {
            auto chain_count = SplitTargetString(rule.target).size();
            rule.selector = ParseSelectorGroups(rule.target);
            if (rule.selector.size() < chain_count) {
                // |=, not =: the styles loop below can already have set
                // `dead` (an empty style entry). Using = here would silently
                // un-kill that rule if this selector block ever ran after
                // it - harmless today only because of the fixed order of
                // the two blocks.
                rule.dead |= rule.selector.empty();
                theme.diagnostics.push_back(
                    L"theme " + theme.id + L": target '" + rule.target +
                    L"' - " +
                    (rule.dead
                         ? L"every selector chain is unparseable (glued "
                           L"matchers, e.g. two '#Name's with no '>' "
                           L"between them); rule matches nothing"
                         : L"one or more comma-separated selector chains "
                           L"are unparseable and were dropped; the "
                           L"remaining chain(s) still apply"));
            }
        }

        auto styles_it = entry.find("styles");
        if (styles_it == entry.end() || !styles_it->is_array()) {
            throw ParseError("Missing or non-array field: styles");
        }
        // Validate every entry's type up front, before processing any of
        // them: the loop below can `break` early once it hits an empty
        // style string (see the comment there), which must not let a
        // malformed entry AFTER the break - e.g. {"styles": ["", 42]} -
        // load silently instead of failing the theme closed.
        for (const auto& s : *styles_it) {
            if (!s.is_string()) {
                throw ParseError("Style entry is not a string");
            }
        }
        for (const auto& s : *styles_it) {
            auto raw = s.get<std::string>();
            // A handful of shipped rules (e.g. LiquidGlass2's #DisplayName
            // and #Iconlmage targets) carry a literal empty style string in
            // the upstream C++ source (`L""`). Upstream's own ParseRule
            // (vendor/upstream/...:18727) throws "'=' is missing" on it, and
            // AddElementCustomizationRules (vendor/upstream/...:18956)
            // catches that per TARGET, discarding everything already parsed
            // for it - not just the bad entry. Mirror that: throw away the
            // whole styles list built so far for this rule (today that list
            // is empty anyway - all four shipped occurrences are the only
            // style on their rule - but a rule with good styles ahead of a
            // bad one must lose them too, faithfully). Stop looking at the
            // rest of this rule's style strings, same as upstream aborting
            // the target; any other malformed entry (missing '=', etc.)
            // still throws via ParseStyleRule below and fails the theme
            // closed, unchanged.
            if (raw.empty()) {
                rule.styles.clear();
                rule.dead = true;
                theme.diagnostics.push_back(
                    L"theme " + theme.id + L": target '" + rule.target +
                    L"' - has an unparseable (empty) style entry; "
                    L"discarding all styles for this rule");
                break;
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
