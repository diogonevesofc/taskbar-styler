// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <styler/style_expression.h>
#include <tap/dynamic_styles.h>

namespace {

using styler::tap::DynamicStyleDependencies;
using styler::tap::DynamicStyleStates;
using styler::tap::SelectDynamicStyle;

styler::PreparedStyle Style(const wchar_t* state, const wchar_t* value,
                            bool dynamic = true) {
    styler::PreparedStyle style;
    style.property = L"Width";
    style.visual_state = state;
    style.value = value;
    style.dynamic = dynamic;
    return style;
}

struct Variables {
    std::map<std::wstring, styler::StyleVariableValue> values;

    void Set(const wchar_t* name, double value) {
        values[name] = {styler::FormatDoubleInvariant(value), value, true};
    }

    std::optional<std::wstring> Expand(DynamicStyleStates& states,
                                       const wchar_t* name) const {
        auto& dynamic = states.at(name);
        dynamic.dependencies.clear();
        return styler::ExpandStyleVariables(
            dynamic.style->value,
            [this](std::wstring_view variable) -> const styler::StyleVariableValue* {
                auto it = values.find(std::wstring(variable));
                return it == values.end() ? nullptr : &it->second;
            },
            &dynamic.dependencies);
    }
};

}  // namespace

TEST_CASE("dynamic property retains dependencies of every visual state once") {
    auto normal = Style(L"", L"{{A + Shared}}");
    auto hover = Style(L"PointerOver", L"{{B + Shared}}");
    DynamicStyleStates states;
    SelectDynamicStyle(states, normal);
    SelectDynamicStyle(states, hover);
    Variables variables;
    variables.Set(L"A", 40);
    variables.Set(L"B", 80);
    variables.Set(L"Shared", 2);

    CHECK(variables.Expand(states, L"") == std::optional<std::wstring>{L"42"});
    CHECK(variables.Expand(states, L"PointerOver") ==
          std::optional<std::wstring>{L"82"});
    CHECK(DynamicStyleDependencies(states) ==
          std::vector<std::wstring>{L"A", L"B", L"Shared"});

    variables.Set(L"A", 60);
    CHECK(variables.Expand(states, L"") == std::optional<std::wstring>{L"62"});
    CHECK(DynamicStyleDependencies(states) ==
          std::vector<std::wstring>{L"A", L"B", L"Shared"});
}

TEST_CASE("conditional expansion replaces only its state's dependencies") {
    auto normal = Style(L"", L"{{Switch ? A : C}}");
    auto hover = Style(L"PointerOver", L"{{B}}");
    DynamicStyleStates states;
    SelectDynamicStyle(states, normal);
    SelectDynamicStyle(states, hover);
    Variables variables;
    variables.Set(L"Switch", 1);
    variables.Set(L"A", 40);
    variables.Set(L"B", 80);
    variables.Set(L"C", 100);

    REQUIRE(variables.Expand(states, L"").has_value());
    REQUIRE(variables.Expand(states, L"PointerOver").has_value());
    CHECK(DynamicStyleDependencies(states) ==
          std::vector<std::wstring>{L"A", L"B", L"Switch"});

    variables.Set(L"Switch", 0);
    CHECK(variables.Expand(states, L"") == std::optional<std::wstring>{L"100"});
    CHECK(DynamicStyleDependencies(states) ==
          std::vector<std::wstring>{L"B", L"C", L"Switch"});
}

TEST_CASE("static override removes the old dynamic template and dependencies") {
    auto normal = Style(L"", L"{{A}}");
    auto hover = Style(L"PointerOver", L"{{B}}");
    auto replacement = Style(L"", L"100", false);
    DynamicStyleStates states;
    SelectDynamicStyle(states, normal);
    SelectDynamicStyle(states, hover);
    Variables variables;
    variables.Set(L"A", 40);
    variables.Set(L"B", 80);
    REQUIRE(variables.Expand(states, L"").has_value());
    REQUIRE(variables.Expand(states, L"PointerOver").has_value());

    SelectDynamicStyle(states, replacement);
    CHECK_FALSE(states.contains(L""));
    CHECK(DynamicStyleDependencies(states) == std::vector<std::wstring>{L"B"});

    auto dynamic_replacement = Style(L"PointerOver", L"{{C}}");
    SelectDynamicStyle(states, dynamic_replacement);
    variables.Set(L"C", 120);
    CHECK(variables.Expand(states, L"PointerOver") ==
          std::optional<std::wstring>{L"120"});
    CHECK(DynamicStyleDependencies(states) == std::vector<std::wstring>{L"C"});
}

TEST_CASE("unresolved state stays subscribed alongside a resolved state") {
    auto normal = Style(L"", L"{{Missing}}");
    auto hover = Style(L"PointerOver", L"{{B}}");
    DynamicStyleStates states;
    SelectDynamicStyle(states, normal);
    SelectDynamicStyle(states, hover);
    Variables variables;
    variables.Set(L"B", 80);

    CHECK_FALSE(variables.Expand(states, L"").has_value());
    REQUIRE(variables.Expand(states, L"PointerOver").has_value());
    CHECK(DynamicStyleDependencies(states) ==
          std::vector<std::wstring>{L"B", L"Missing"});

    variables.Set(L"Missing", 100);
    CHECK(variables.Expand(states, L"") == std::optional<std::wstring>{L"100"});
    CHECK(DynamicStyleDependencies(states) ==
          std::vector<std::wstring>{L"B", L"Missing"});
}
