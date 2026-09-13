// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <tap/tree_export.h>

using styler::tap::AssignSiblingIndices;
using styler::tap::FormatTree;
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
