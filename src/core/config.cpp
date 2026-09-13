// SPDX-License-Identifier: GPL-3.0-or-later
#include <styler/config.h>

#include <nlohmann/json.hpp>
#include <styler/selector.h>
#include <styler/utf.h>

namespace styler {

Config ParseConfigJson(std::string_view utf8) {
    nlohmann::json j = nlohmann::json::parse(utf8, nullptr, false);
    if (j.is_discarded() || !j.is_object()) {
        throw ParseError("config: not a JSON object");
    }
    Config c;
    if (auto it = j.find("theme"); it != j.end()) {
        if (!it->is_string()) {
            throw ParseError("config: \"theme\" must be a string");
        }
        c.theme = Utf8ToWide(it->get<std::string>());
    }
    if (auto it = j.find("logLevel"); it != j.end()) {
        if (!it->is_string()) {
            throw ParseError("config: \"logLevel\" must be a string");
        }
        c.log_level = Utf8ToWide(it->get<std::string>());
    }
    return c;
}

std::string SerializeConfigJson(const Config& config) {
    nlohmann::json j;
    j["theme"] = WideToUtf8(config.theme);
    j["logLevel"] = WideToUtf8(config.log_level);
    return j.dump(2) + "\n";
}

}  // namespace styler
