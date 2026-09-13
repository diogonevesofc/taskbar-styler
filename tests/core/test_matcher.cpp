// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <map>
#include <memory>
#include <string>
#include <vector>

#include <styler/matcher.h>
#include <styler/selector.h>
#include <styler/theme.h>

using namespace styler;

namespace {

struct Node {
    std::wstring type;
    std::wstring name;
    std::wstring reported;  // what diagnostics reported; empty = same as type
    std::map<std::wstring, std::wstring> props;
    std::vector<std::shared_ptr<Node>> children;
    Node* parent = nullptr;

    std::shared_ptr<Node> Add(std::wstring t, std::wstring n = L"") {
        auto c = std::make_shared<Node>();
        c->type = std::move(t);
        c->name = std::move(n);
        c->parent = this;
        children.push_back(c);
        return c;
    }
};

class NodeView : public ElementView {
public:
    explicit NodeView(const Node* n) : n_(n) {}
    std::wstring TypeName() const override { return n_->type; }
    std::wstring ReportedTypeName() const override {
        return n_->reported.empty() ? n_->type : n_->reported;
    }
    std::wstring Name() const override { return n_->name; }
    std::unique_ptr<ElementView> Parent() const override {
        return n_->parent ? std::make_unique<NodeView>(n_->parent) : nullptr;
    }
    int IndexInParent() const override {
        if (!n_->parent) return -1;
        const auto& sib = n_->parent->children;
        for (size_t i = 0; i < sib.size(); ++i) {
            if (sib[i].get() == n_) return static_cast<int>(i);
        }
        return -1;
    }
    std::optional<bool> PropertyEquals(std::wstring_view property,
                                       std::wstring_view expected) const override {
        auto it = n_->props.find(std::wstring(property));
        if (it == n_->props.end()) return std::nullopt;
        return it->second == expected;
    }

private:
    const Node* n_;
};

// Frame > Grid#RootGrid > (Background > Grid > Rectangle#BackgroundFill,
//                          Border#Stroke, Border#Stroke)
struct Tree {
    std::shared_ptr<Node> frame, root, bg, grid, fill, b1, b2;
    Tree() {
        frame = std::make_shared<Node>();
        frame->type = L"Taskbar.TaskbarFrame";
        frame->name = L"TaskbarFrame";
        root = frame->Add(L"Windows.UI.Xaml.Controls.Grid", L"RootGrid");
        bg = root->Add(L"Taskbar.TaskbarBackground", L"BackgroundControl");
        grid = bg->Add(L"Windows.UI.Xaml.Controls.Grid");
        fill = grid->Add(L"Windows.UI.Xaml.Shapes.Rectangle", L"BackgroundFill");
        b1 = root->Add(L"Windows.UI.Xaml.Controls.Border", L"Stroke");
        b2 = root->Add(L"Windows.UI.Xaml.Controls.Border", L"Stroke");
        fill->props[L"Visibility"] = L"Visible";
    }
};

std::vector<ElementMatcher> Chain(std::wstring_view s) {
    return ParseSelector(s);
}

}  // namespace

TEST_CASE("a bare type matches by runtime class name") {
    Tree t;
    CHECK(MatchesChain(NodeView(t.fill.get()),
                       Chain(L"Windows.UI.Xaml.Shapes.Rectangle"), nullptr));
    CHECK_FALSE(MatchesChain(NodeView(t.fill.get()),
                             Chain(L"Windows.UI.Xaml.Controls.Grid"), nullptr));
}

TEST_CASE("the leaf may match the reported type, ancestors may not") {
    Tree t;
    t.fill->reported = L"Custom.Rect";
    t.grid->reported = L"Custom.Grid";
    CHECK(MatchesChain(NodeView(t.fill.get()), Chain(L"Custom.Rect"), nullptr));
    CHECK_FALSE(MatchesChain(NodeView(t.fill.get()),
                             Chain(L"Custom.Grid > Custom.Rect"), nullptr));
}

TEST_CASE("a full ancestor chain matches outermost first") {
    Tree t;
    CHECK(MatchesChain(
        NodeView(t.fill.get()),
        Chain(L"Taskbar.TaskbarFrame > Windows.UI.Xaml.Controls.Grid#RootGrid > "
              L"Taskbar.TaskbarBackground > Windows.UI.Xaml.Controls.Grid > "
              L"Windows.UI.Xaml.Shapes.Rectangle#BackgroundFill"),
        nullptr));
    CHECK_FALSE(MatchesChain(
        NodeView(t.fill.get()),
        Chain(L"Windows.UI.Xaml.Controls.Grid#RootGrid > "
              L"Windows.UI.Xaml.Shapes.Rectangle#BackgroundFill"),
        nullptr));
}

TEST_CASE("the wildcard skips any number of ancestors and backtracks") {
    Tree t;
    // Grid ... Rectangle: the nearest Grid ancestor is the anonymous one; the
    // wildcard must also be able to reach RootGrid for the #RootGrid name.
    CHECK(MatchesChain(
        NodeView(t.fill.get()),
        Chain(L"Windows.UI.Xaml.Controls.Grid#RootGrid > * > "
              L"Windows.UI.Xaml.Shapes.Rectangle#BackgroundFill"),
        nullptr));
    CHECK(MatchesChain(
        NodeView(t.fill.get()),
        Chain(L"Taskbar.TaskbarFrame > * > Windows.UI.Xaml.Controls.Grid > "
              L"Windows.UI.Xaml.Shapes.Rectangle"),
        nullptr));
    CHECK_FALSE(MatchesChain(
        NodeView(t.fill.get()),
        Chain(L"Windows.UI.Xaml.Controls.Border > * > "
              L"Windows.UI.Xaml.Shapes.Rectangle"),
        nullptr));
}

TEST_CASE(":root asserts the element has no parent") {
    Tree t;
    CHECK(MatchesChain(NodeView(t.root.get()),
                       Chain(L":root > Taskbar.TaskbarFrame > "
                             L"Windows.UI.Xaml.Controls.Grid"),
                       nullptr));
    CHECK_FALSE(MatchesChain(NodeView(t.grid.get()),
                             Chain(L":root > Taskbar.TaskbarBackground > "
                                   L"Windows.UI.Xaml.Controls.Grid"),
                             nullptr));
}

TEST_CASE("a one-based index selects among all siblings") {
    Tree t;
    CHECK(MatchesChain(NodeView(t.b2.get()),
                       Chain(L"Windows.UI.Xaml.Controls.Border#Stroke[3]"),
                       nullptr));
    CHECK_FALSE(MatchesChain(NodeView(t.b1.get()),
                             Chain(L"Windows.UI.Xaml.Controls.Border#Stroke[3]"),
                             nullptr));
}

TEST_CASE("a property filter delegates the comparison to the view") {
    Tree t;
    CHECK(MatchesChain(NodeView(t.fill.get()),
                       Chain(L"Windows.UI.Xaml.Shapes.Rectangle[Visibility=Visible]"),
                       nullptr));
    CHECK_FALSE(MatchesChain(
        NodeView(t.fill.get()),
        Chain(L"Windows.UI.Xaml.Shapes.Rectangle[Visibility=Collapsed]"),
        nullptr));
    // Unreadable property: does not match, does not throw.
    CHECK_FALSE(MatchesChain(NodeView(t.fill.get()),
                             Chain(L"Windows.UI.Xaml.Shapes.Rectangle[Nope=1]"),
                             nullptr));
}

TEST_CASE("a visual state group on the leaf or an ancestor is reported with its depth") {
    Tree t;
    std::optional<VisualStateGroupRef> vsg;
    REQUIRE(MatchesChain(NodeView(t.fill.get()),
                         Chain(L"Windows.UI.Xaml.Shapes.Rectangle@CommonStates"),
                         &vsg));
    REQUIRE(vsg.has_value());
    CHECK(vsg->name == L"CommonStates");
    CHECK(vsg->ancestor_depth == 0);

    vsg.reset();
    REQUIRE(MatchesChain(NodeView(t.fill.get()),
                         Chain(L"Taskbar.TaskbarBackground@CommonStates > "
                               L"Windows.UI.Xaml.Controls.Grid > "
                               L"Windows.UI.Xaml.Shapes.Rectangle"),
                         &vsg));
    REQUIRE(vsg.has_value());
    CHECK(vsg->ancestor_depth == 2);
}

TEST_CASE("PrepareTheme expands types, applies constants, rewrites blur, skips the unsupported") {
    Theme theme;
    theme.id = L"T";
    theme.constants = {{L"Bg", L"<WindhawkBlur BlurAmount=\"18\" TintColor=\"#25323232\"/>"}};
    theme.resource_variables = {{L"Accent", L"$Bg"}};

    ThemeRule r1;
    r1.target = L"Grid#RootGrid > Rectangle";
    r1.selector = ParseSelectorGroups(r1.target);
    r1.styles = {ParseStyleRule(L"Fill:=$Bg"), ParseStyleRule(L"Visibility=Collapsed"),
                 ParseStyleRule(L"Width=>W"), ParseStyleRule(L"Height={{W}}")};
    ThemeRule dead;
    dead.target = L"Nope";
    dead.dead = true;
    theme.rules = {r1, dead};

    auto prepared = PrepareTheme(theme);
    REQUIRE(prepared.rules.size() == 1);
    const auto& rule = prepared.rules[0];
    CHECK(rule.source_index == 0);
    REQUIRE(rule.chains.size() == 1);
    CHECK(rule.chains[0][0].type == L"Windows.UI.Xaml.Controls.Grid");
    CHECK(rule.chains[0][1].type == L"Windows.UI.Xaml.Shapes.Rectangle");
    REQUIRE(rule.styles.size() == 2);
    CHECK(rule.styles[0].property == L"Fill");
    CHECK(rule.styles[0].is_xaml);
    CHECK(rule.styles[0].value == L"<AcrylicBrush TintColor=\"#25323232\"/>");
    CHECK(rule.styles[1].property == L"Visibility");
    CHECK_FALSE(rule.styles[1].is_xaml);
    CHECK(prepared.skipped_captures == 1);
    CHECK(prepared.skipped_dynamic == 1);
    CHECK(prepared.blur_approximations == 1);
    CHECK(prepared.resource_variables.at(L"Accent") ==
          L"<AcrylicBrush TintColor=\"#25323232\"/>");
    CHECK(prepared.diagnostics.size() == 2);
}

TEST_CASE("FindMatchingRules returns the last matching rule first") {
    Tree t;
    Theme theme;
    theme.id = L"T";
    for (auto target : {L"Rectangle", L"Grid > Rectangle", L"Border"}) {
        ThemeRule r;
        r.target = target;
        r.selector = ParseSelectorGroups(target);
        r.styles = {ParseStyleRule(L"Opacity=1")};
        theme.rules.push_back(r);
    }
    auto prepared = PrepareTheme(theme);
    auto matches = FindMatchingRules(prepared, NodeView(t.fill.get()));
    REQUIRE(matches.size() == 2);
    CHECK(matches[0].rule->source_index == 1);
    CHECK(matches[1].rule->source_index == 0);
}
