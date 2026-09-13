// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <styler/type_name.h>

using styler::AdjustTypeName;

TEST_CASE("bare control names get the Controls namespace") {
    CHECK(AdjustTypeName(L"Grid") == L"Windows.UI.Xaml.Controls.Grid");
    CHECK(AdjustTypeName(L"Border") == L"Windows.UI.Xaml.Controls.Border");
}

TEST_CASE("Rectangle is a Shape, not a Control") {
    CHECK(AdjustTypeName(L"Rectangle") == L"Windows.UI.Xaml.Shapes.Rectangle");
}

TEST_CASE("dotted names pass through unchanged") {
    CHECK(AdjustTypeName(L"Taskbar.TaskbarFrame") == L"Taskbar.TaskbarFrame");
    CHECK(AdjustTypeName(L"Windows.UI.Xaml.Controls.Grid") ==
          L"Windows.UI.Xaml.Controls.Grid");
}

TEST_CASE("xml-style prefixes expand to their namespaces") {
    CHECK(AdjustTypeName(L"taskbar:TaskListButton") == L"Taskbar.TaskListButton");
    CHECK(AdjustTypeName(L"systemtray:Foo") == L"SystemTray.Foo");
    CHECK(AdjustTypeName(L"udk:Bar") == L"WindowsUdk.UI.Shell.Bar");
    CHECK(AdjustTypeName(L"muxc:ItemsRepeater") ==
          L"Microsoft.UI.Xaml.Controls.ItemsRepeater");
}

TEST_CASE("an unknown prefix passes through unchanged") {
    CHECK(AdjustTypeName(L"foo:Bar") == L"foo:Bar");
}
