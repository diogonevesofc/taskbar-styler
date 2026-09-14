// SPDX-License-Identifier: GPL-3.0-or-later
#include <styler/resource_variables.h>

namespace styler {
namespace {

std::wstring Trim(std::wstring_view s) {
    while (!s.empty() && (s.front() == L' ' || s.front() == L'\t')) s.remove_prefix(1);
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\t')) s.remove_suffix(1);
    return std::wstring(s);
}

}  // namespace

std::vector<ResourceVariable> ParseResourceVariables(
    const std::map<std::wstring, std::wstring>& variables,
    std::vector<std::wstring>* diagnostics) {
    std::vector<ResourceVariable> out;
    for (const auto& [raw_key, raw_value] : variables) {
        ResourceVariable v;
        std::wstring key = Trim(raw_key);
        v.value = Trim(raw_value);

        if (!key.empty() && key.back() == L':') {
            v.type = ResourceValueType::Xaml;
            key = Trim(key.substr(0, key.size() - 1));
        } else if (v.value.starts_with(L"{ThemeResource ") && v.value.ends_with(L"}")) {
            v.type = ResourceValueType::ThemeResourceReference;
            v.value = Trim(std::wstring_view(v.value).substr(
                15, v.value.size() - 16));  // strlen("{ThemeResource ") == 15
        }

        if (auto at = key.find(L'@'); at != std::wstring::npos) {
            std::wstring theme = Trim(std::wstring_view(key).substr(at + 1));
            key = Trim(std::wstring_view(key).substr(0, at));
            if (theme == L"Dark") {
                v.theme = ResourceTheme::Dark;
            } else if (theme == L"Light") {
                v.theme = ResourceTheme::Light;
            } else {
                if (diagnostics) {
                    diagnostics->push_back(L"resource variable '" + raw_key +
                                           L"': unknown theme '" + theme +
                                           L"', expected Dark or Light");
                }
                continue;
            }
        }
        if (key.empty()) {
            if (diagnostics) {
                diagnostics->push_back(L"resource variable with empty name skipped");
            }
            continue;
        }
        v.key = std::move(key);
        out.push_back(std::move(v));
    }
    return out;
}

}  // namespace styler
