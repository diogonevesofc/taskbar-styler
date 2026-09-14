// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <tap/tree_export.h>

using styler::tap::AssignSiblingIndices;
using styler::tap::BuildForest;
using styler::tap::FormatTree;
using styler::tap::Reported;
using styler::tap::TreeNode;

TEST_CASE("formats a single node") {
    TreeNode root;
    root.type = L"Taskbar.TaskbarFrame";
    CHECK(FormatTree(root) == L"Taskbar.TaskbarFrame\n");
}

TEST_CASE("appends #Name when the element has one") {
    TreeNode root;
    root.type = L"Grid";
    root.name = L"RootGrid";
    CHECK(FormatTree(root) == L"Grid#RootGrid\n");
}

TEST_CASE("indents two spaces per level") {
    TreeNode root;
    root.type = L"A";
    TreeNode b;
    b.type = L"B";
    TreeNode c;
    c.type = L"C";
    c.name = L"Deep";
    b.children.push_back(c);
    root.children.push_back(b);

    CHECK(FormatTree(root) ==
          L"A\n"
          L"  B\n"
          L"    C#Deep\n");
}

TEST_CASE("indexes only the sibling types that repeat") {
    TreeNode root;
    root.type = L"Grid";
    TreeNode r1;
    r1.type = L"Rectangle";
    TreeNode r2;
    r2.type = L"Rectangle";
    TreeNode border;
    border.type = L"Border";
    root.children = {r1, r2, border};

    AssignSiblingIndices(root);

    CHECK(FormatTree(root) ==
          L"Grid\n"
          L"  Rectangle[1]\n"
          L"  Rectangle[2]\n"
          L"  Border\n");
}

TEST_CASE("combines #Name and [N] on the same node") {
    TreeNode root;
    root.type = L"Rectangle";
    root.name = L"BackgroundFill";
    root.one_based_index = 1;
    CHECK(FormatTree(root) == L"Rectangle#BackgroundFill[1]\n");
}

TEST_CASE("indexes non-adjacent repeats among siblings") {
    TreeNode root;
    root.type = L"Grid";
    TreeNode r1;
    r1.type = L"Rect";
    TreeNode border;
    border.type = L"Border";
    TreeNode r2;
    r2.type = L"Rect";
    root.children = {r1, border, r2};

    AssignSiblingIndices(root);

    CHECK(FormatTree(root) ==
          L"Grid\n"
          L"  Rect[1]\n"
          L"  Border\n"
          L"  Rect[2]\n");
}

TEST_CASE("AssignSiblingIndices recurses past the first level") {
    TreeNode root;
    root.type = L"Root";
    TreeNode branch;
    branch.type = L"Branch";
    TreeNode leaf1;
    leaf1.type = L"Leaf";
    TreeNode other;
    other.type = L"Other";
    TreeNode leaf2;
    leaf2.type = L"Leaf";
    branch.children = {leaf1, other, leaf2};
    root.children.push_back(branch);

    AssignSiblingIndices(root);

    CHECK(FormatTree(root) ==
          L"Root\n"
          L"  Branch\n"
          L"    Leaf[1]\n"
          L"    Other\n"
          L"    Leaf[2]\n");
}

TEST_CASE("BuildForest keeps the first report on a duplicate handle") {
    Reported first;
    first.handle = 1;
    first.type = L"A";
    Reported child;
    child.handle = 2;
    child.parent = 1;
    child.type = L"Child";
    Reported duplicate;
    duplicate.handle = 1;
    duplicate.type = L"Dup";

    auto forest = BuildForest({first, child, duplicate});

    REQUIRE(forest.size() == 1);
    CHECK(forest[0].type == L"A");
    REQUIRE(forest[0].children.size() == 1);
    CHECK(forest[0].children[0].type == L"Child");
}

TEST_CASE("BuildForest makes an element with an unreported parent a root") {
    Reported orphan;
    orphan.handle = 1;
    orphan.parent = 99;  // Never itself reported.
    orphan.type = L"Orphan";

    auto forest = BuildForest({orphan});

    REQUIRE(forest.size() == 1);
    CHECK(forest[0].type == L"Orphan");
}

TEST_CASE("BuildForest orders siblings by ChildIndex regardless of arrival order") {
    Reported root;
    root.handle = 1;
    root.type = L"Root";
    Reported c2;
    c2.handle = 2;
    c2.parent = 1;
    c2.child_index = 2;
    c2.type = L"C";
    Reported c0;
    c0.handle = 3;
    c0.parent = 1;
    c0.child_index = 0;
    c0.type = L"A";
    Reported c1;
    c1.handle = 4;
    c1.parent = 1;
    c1.child_index = 1;
    c1.type = L"B";

    // Arrival order deliberately scrambled: C, A, B.
    auto forest = BuildForest({root, c2, c0, c1});

    REQUIRE(forest.size() == 1);
    REQUIRE(forest[0].children.size() == 3);
    CHECK(forest[0].children[0].type == L"A");
    CHECK(forest[0].children[1].type == L"B");
    CHECK(forest[0].children[2].type == L"C");
}

TEST_CASE("BuildForest yields an empty forest for a cycle") {
    Reported a;
    a.handle = 1;
    a.parent = 2;
    a.type = L"A";
    Reported b;
    b.handle = 2;
    b.parent = 1;
    b.type = L"B";

    CHECK(BuildForest({a, b}).empty());
}

TEST_CASE("DescribeIncompleteTree flags a truncated batch") {
    using styler::tap::Reported;
    std::vector<Reported> reported{
        {1, 0, 0, 3, L"Taskbar.TaskbarFrame", L""},
        {2, 1, 0, 0, L"Grid", L"RootGrid"},
    };
    auto lines = styler::tap::DescribeIncompleteTree(reported);
    REQUIRE(lines.size() == 1);
    CHECK(lines[0] == L"Taskbar.TaskbarFrame: 1 of 3 children reported");
}

TEST_CASE("DescribeIncompleteTree is quiet on a complete tree") {
    using styler::tap::Reported;
    std::vector<Reported> reported{
        {1, 0, 0, 2, L"Taskbar.TaskbarFrame", L""},
        {2, 1, 0, 0, L"Grid", L"RootGrid"},
        {3, 1, 1, 0, L"Rectangle", L"BackgroundFill"},
    };
    CHECK(styler::tap::DescribeIncompleteTree(reported).empty());
}
