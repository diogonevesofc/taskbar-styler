// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <map>
#include <optional>
#include <string>
#include <vector>

#include <styler/style_expression.h>

namespace {

// A lookup over a fixed table, with the values the shipped themes actually
// capture (ActualWidth / ActualHeight / Height - all numeric).
struct Vars {
    std::map<std::wstring, styler::StyleVariableValue> table;

    void SetNumber(const wchar_t* name, double v) {
        table[name] = styler::StyleVariableValue{
            styler::FormatDoubleInvariant(v), v, true};
    }
    void SetOpaque(const wchar_t* name, const wchar_t* class_name) {
        // A brush or a thickness: present, but not substitutable.
        table[name] = styler::StyleVariableValue{class_name, std::nullopt, false};
    }
    styler::StyleVariableLookup Lookup() const {
        return [this](std::wstring_view n) -> const styler::StyleVariableValue* {
            auto it = table.find(std::wstring(n));
            return it == table.end() ? nullptr : &it->second;
        };
    }
};

std::optional<std::wstring> Expand(const Vars& vars, const wchar_t* text) {
    std::vector<std::wstring> deps;
    return styler::ExpandStyleVariables(text, vars.Lookup(), &deps);
}

Vars CorpusVars() {
    Vars v;
    v.SetNumber(L"TaskbarHeight", 48);
    v.SetNumber(L"containerGridWidth", 1000);
    v.SetNumber(L"TaskHeight", 40);
    v.SetNumber(L"OverflowHeight", 300);
    v.SetNumber(L"BtnW", 44);
    v.SetNumber(L"ImageIconWidth", 24);
    v.SetNumber(L"LabelWidth", 0);
    v.SetNumber(L"WeatherTempWidth", 30);
    v.SetNumber(L"WeatherCondWidth", 60);
    v.SetNumber(L"WeatherIconWidth", 20);
    return v;
}

}  // namespace

TEST_CASE("text with no substitution passes through") {
    Vars v;
    CHECK(*Expand(v, L"plain text") == L"plain text");
    CHECK(*Expand(v, L"0,0,0,0") == L"0,0,0,0");
}

TEST_CASE("the expressions the shipped themes actually use") {
    Vars v = CorpusVars();
    CHECK(*Expand(v, L"{{TaskbarHeight-(4+6)}}") == L"38");
    CHECK(*Expand(v, L"{{containerGridWidth>0?containerGridWidth:`Infinity`}}") ==
          L"1000");
    CHECK(*Expand(v,
                  L"{{containerGridWidth>0?max(containerGridWidth-250,100):"
                  L"`Infinity`}}") == L"750");
    CHECK(*Expand(v, L"{{(TaskHeight/4)*1.8}}") == L"18");
    CHECK(*Expand(v, L"{{ max(-4, min(-15, OverflowHeight * 0.35)) }}") == L"-4");
    CHECK(*Expand(v, L"{{BtnW-6}}") == L"38");
    CHECK(*Expand(v, L"{{max((ImageIconWidth/2-3),10)}}") == L"10");
    CHECK(*Expand(v, L"{{LabelWidth>0?6:0}}") == L"0");
    CHECK(*Expand(v, L"{{WeatherCondWidth+WeatherTempWidth + WeatherIconWidth + 36}}") ==
          L"146");
    CHECK(*Expand(v, L"{{-ImageIconWidth/2}}") == L"-12");
}

TEST_CASE("substitutions mix with literal text, several per value") {
    Vars v = CorpusVars();
    CHECK(*Expand(v, L"0,0,0,{{TaskHeight - 8}}") == L"0,0,0,32");
    CHECK(*Expand(v, L"{{BtnW}},{{TaskHeight}},{{BtnW}},{{TaskHeight}}") ==
          L"44,40,44,40");
}

TEST_CASE("brace pairs match innermost first") {
    Vars v = CorpusVars();
    CHECK(*Expand(v, L"{{{TaskHeight}}}") == L"{40}");
}

TEST_CASE("an undefined bare reference skips the style") {
    Vars v = CorpusVars();
    CHECK_FALSE(Expand(v, L"{{nope}}").has_value());
    // Pills' sentinel, which reaches here through a $constant.
    CHECK_FALSE(Expand(v, L"{{__unset}}").has_value());
}

TEST_CASE("an undefined variable inside an expression is the empty string") {
    Vars v = CorpusVars();
    CHECK(*Expand(v, L"{{nope == `` ? 80 : nope}}") == L"80");
    // ...but arithmetic on it still fails, so the style is skipped rather
    // than treated as zero.
    CHECK_FALSE(Expand(v, L"{{nope + 1}}").has_value());
}

TEST_CASE("an opaque capture is not substitutable") {
    Vars v = CorpusVars();
    v.SetOpaque(L"Brushy", L"Windows.UI.Xaml.Media.SolidColorBrush");
    CHECK_FALSE(Expand(v, L"{{Brushy}}").has_value());
    // But it can still be compared, which is how a theme can branch on it.
    CHECK(*Expand(v, L"{{Brushy == `Windows.UI.Xaml.Media.SolidColorBrush` ? 1 : 0}}") ==
          L"1");
}

TEST_CASE("strings, comparisons and the conditional") {
    Vars v = CorpusVars();
    CHECK(*Expand(v, L"{{TaskHeight == 40 ? `Auto` : `*`}}") == L"Auto");
    CHECK(*Expand(v, L"{{TaskHeight != 40 ? `Auto` : `*`}}") == L"*");
    CHECK(*Expand(v, L"{{TaskHeight >= 40}}") == L"1");
    CHECK(*Expand(v, L"{{TaskHeight < 40}}") == L"0");
    CHECK(*Expand(v, L"{{`a``b`}}") == L"a`b");
}

TEST_CASE("a malformed expression skips the style, it never throws") {
    Vars v = CorpusVars();
    CHECK_FALSE(Expand(v, L"{{1/0}}").has_value());
    CHECK_FALSE(Expand(v, L"{{TaskHeight +}}").has_value());
    CHECK_FALSE(Expand(v, L"a}}b").has_value());
    CHECK_FALSE(Expand(v, L"{{min(1)}}").has_value());
    CHECK_FALSE(Expand(v, L"{{nosuchfn(1,2)}}").has_value());
}

TEST_CASE("every referenced name is reported once, in use order") {
    Vars v = CorpusVars();
    std::vector<std::wstring> deps;
    styler::ExpandStyleVariables(L"{{BtnW - ImageIconWidth + nope}}", v.Lookup(),
                                 &deps);
    REQUIRE(deps.size() == 3);
    CHECK(deps[0] == L"BtnW");
    CHECK(deps[1] == L"ImageIconWidth");
    CHECK(deps[2] == L"nope");
}

TEST_CASE("the ternary short-circuits: the untaken branch is not evaluated") {
    Vars v = CorpusVars();
    // Fix round 1: matches upstream (vendor:16330-16357) - the untaken branch
    // is parsed (to advance the position and enforce syntax) but not
    // evaluated, so a division by zero, an undefined variable used
    // arithmetically, or an unknown function call in it must not skip the
    // style. This is the guard idiom `{{x == 0 ? 0 : 100/x}}`.
    auto guard_taken = Expand(v, L"{{TaskHeight == 40 ? 0 : 1/0}}");
    REQUIRE(guard_taken.has_value());
    CHECK(*guard_taken == L"0");

    auto guard_else = Expand(v, L"{{TaskHeight != 40 ? 1/0 : 0}}");
    REQUIRE(guard_else.has_value());
    CHECK(*guard_else == L"0");

    auto undefined_in_dead_branch =
        Expand(v, L"{{TaskHeight == 40 ? 0 : nope + 1}}");
    REQUIRE(undefined_in_dead_branch.has_value());
    CHECK(*undefined_in_dead_branch == L"0");

    auto unknown_fn_in_dead_branch =
        Expand(v, L"{{TaskHeight == 40 ? 0 : nosuchfn(1, 2)}}");
    REQUIRE(unknown_fn_in_dead_branch.has_value());
    CHECK(*unknown_fn_in_dead_branch == L"0");

    // The taken branch still fails closed as usual.
    CHECK_FALSE(Expand(v, L"{{TaskHeight != 40 ? 0 : 1/0}}").has_value());
}

TEST_CASE("a variable used only in the untaken ternary branch is not a dependency") {
    Vars v = CorpusVars();
    std::vector<std::wstring> deps;
    auto out = styler::ExpandStyleVariables(
        L"{{TaskHeight == 40 ? BtnW : ImageIconWidth}}", v.Lookup(), &deps);
    REQUIRE(out.has_value());
    CHECK(*out == L"44");
    // TaskHeight (the condition) and BtnW (the taken branch) are real
    // dependencies; ImageIconWidth, used only in the untaken branch, must
    // not be - Task 6 must not recompute this style when it alone changes.
    REQUIRE(deps.size() == 2);
    CHECK(deps[0] == L"TaskHeight");
    CHECK(deps[1] == L"BtnW");
}

TEST_CASE("FormatDoubleInvariant round-trips without trailing zeros") {
    CHECK(styler::FormatDoubleInvariant(38.0) == L"38");
    CHECK(styler::FormatDoubleInvariant(19.5) == L"19.5");
    CHECK(styler::FormatDoubleInvariant(-4.0) == L"-4");
}
