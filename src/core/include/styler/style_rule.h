// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <string>
#include <string_view>
#include <variant>

#include <styler/selector.h>  // for ParseError

namespace styler {

// `Property[@VisualState][:]=value` — sets a property on a matched element.
struct ValueRule {
    std::wstring property_name;
    std::wstring visual_state;
    std::wstring value;
    bool is_xaml_value = false;

    // A `{{...}}` placeholder means the value is re-resolved on every apply.
    bool IsDynamic() const {
        return value.find(L"{{") != std::wstring::npos;
    }
};

// `Property=>VarName` — reads a property into a global style variable.
struct CaptureRule {
    std::wstring property_name;
    std::wstring var_name;
};

using StyleRule = std::variant<ValueRule, CaptureRule>;

bool IsValidStyleVariableIdentifier(std::wstring_view name);

StyleRule ParseStyleRule(std::wstring_view str);

}  // namespace styler
