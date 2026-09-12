// SPDX-License-Identifier: GPL-3.0-or-later
#include <styler/style_rule.h>

#include "detail/text.h"

namespace styler {

bool IsValidStyleVariableIdentifier(std::wstring_view name) {
    if (name.empty()) {
        return false;
    }
    if (name.front() >= L'0' && name.front() <= L'9') {
        return false;
    }
    for (wchar_t c : name) {
        bool ok = (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') ||
                  (c >= L'0' && c <= L'9') || c == L'_';
        if (!ok) {
            return false;
        }
    }
    return true;
}

StyleRule ParseStyleRule(std::wstring_view str) {
    auto eq = str.find(L'=');
    if (eq == std::wstring_view::npos) {
        throw ParseError("Bad style syntax, '=' is missing");
    }

    auto name = str.substr(0, eq);
    auto value = str.substr(eq + 1);

    if (!value.empty() && value.front() == L'>') {
        value = value.substr(1);

        if (!name.empty() && name.back() == L':') {
            throw ParseError(
                "Bad style syntax, ':=>' is not valid (':=' XAML value cannot "
                "be combined with '=>' capture)");
        }
        if (name.find(L'@') != std::wstring_view::npos) {
            throw ParseError(
                "Bad style syntax, '@VisualState' not allowed on a capture "
                "rule");
        }

        auto property_name = detail::Trim(name);
        if (property_name.empty()) {
            throw ParseError("Bad style syntax, empty name");
        }

        auto var_name = detail::Trim(value);
        if (var_name.empty()) {
            throw ParseError("Bad style syntax, empty capture variable name");
        }
        if (!IsValidStyleVariableIdentifier(var_name)) {
            throw ParseError("Bad style syntax, invalid capture variable name");
        }

        return CaptureRule{std::wstring(property_name),
                           std::wstring(var_name)};
    }

    ValueRule result;
    result.value = detail::Trim(value);

    if (!name.empty() && name.back() == L':') {
        result.is_xaml_value = true;
        name = name.substr(0, name.size() - 1);
    }

    auto at = name.find(L'@');
    if (at != std::wstring_view::npos) {
        result.visual_state = detail::Trim(name.substr(at + 1));
        name = name.substr(0, at);
    }

    result.property_name = detail::Trim(name);
    if (result.property_name.empty()) {
        throw ParseError("Bad style syntax, empty name");
    }

    return result;
}

}  // namespace styler
