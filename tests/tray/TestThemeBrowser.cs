// SPDX-License-Identifier: GPL-3.0-or-later
using TaskbarStyler.Tray.Core;

internal static class TestThemeBrowser
{
    private static readonly ThemeInfo[] Catalog =
    [
        new("Command_Center", "Command_Center", "Alice"),
        new("Pills", "Pills", "Bruno"),
        new("Glass", "Clear Glass", "CAROL"),
        new("Glass_Variant", "Glass internal variant", "Carol", true)
    ];

    public static (string Name, Action Run)[] Cases =>
    [
        ("browser initially previews configured theme ignoring case", () =>
        {
            var state = new ThemeBrowserState(Catalog, "pIlLs");
            Check.Equal("Pills", state.SelectedId);
            Check.Equal("pIlLs", state.ConfiguredThemeId);
        }),
        ("browser selection changes preview without applying or configuring", () =>
        {
            var state = new ThemeBrowserState(Catalog, "Pills");
            Check.True(state.Select("gLaSs"));
            Check.Equal("Glass", state.SelectedId);
            Check.Equal("Pills", state.ConfiguredThemeId);
            Check.False(state.Select("unknown"));
            Check.Equal("Glass", state.SelectedId);
        }),
        ("configuration updates preserve the user's preview choice", () =>
        {
            var state = new ThemeBrowserState(Catalog, "Pills");
            state.Select("Glass");
            state.UpdateConfiguredTheme("Command_Center");
            Check.Equal("Command_Center", state.ConfiguredThemeId);
            Check.Equal("Glass", state.SelectedId);
            state.UpdateConfiguredTheme("");
            Check.Equal("", state.ConfiguredThemeId);
            Check.Equal("Glass", state.SelectedId);
        }),
        ("browser filters names, IDs and authors without case sensitivity", () =>
        {
            var state = new ThemeBrowserState(Catalog);
            foreach (var pair in new[]
            {
                ("clear GLASS", "Glass"), ("COMMAND_CENTER", "Command_Center"),
                ("brUnO", "Pills"), ("command center", "Command_Center")
            })
            {
                state.SetFilter(pair.Item1);
                Check.Equal(1, state.VisibleThemes.Count);
                Check.Equal(pair.Item2, state.SelectedId);
            }
        }),
        ("browser preserves selection when it remains in filtered results", () =>
        {
            var state = new ThemeBrowserState(Catalog, "Pills");
            state.Select("Glass");
            state.SetFilter("l");
            Check.Equal(3, state.VisibleThemes.Count);
            Check.Equal("Glass", state.SelectedId);
            state.SetFilter("  ");
            Check.Equal("", state.Filter);
            Check.Equal(3, state.VisibleThemes.Count);
            Check.Equal("Glass", state.SelectedId);
        }),
        ("no search results means no invisible selected theme", () =>
        {
            var state = new ThemeBrowserState(Catalog, "Pills");
            state.SetFilter("unmatched");
            Check.Equal(0, state.VisibleThemes.Count);
            Check.Null(state.SelectedId);
            Check.Null(state.SelectedTheme);
            Check.False(state.Select("Pills"));
            state.UpdateConfiguredTheme("Glass");
            Check.Null(state.SelectedId);
            state.SetFilter(null);
            Check.Equal(3, state.VisibleThemes.Count);
            Check.Equal("Command_Center", state.SelectedId);
        }),
        ("internal variants are never browser results or selectable previews", () =>
        {
            var state = new ThemeBrowserState(Catalog, "Glass_Variant");
            Check.Equal(3, state.VisibleThemes.Count);
            Check.Equal("Command_Center", state.SelectedId);
            Check.Equal("Glass_Variant", state.ConfiguredThemeId);
            Check.False(state.Select("Glass_Variant"));
            state.SetFilter("internal variant");
            Check.Equal(0, state.VisibleThemes.Count);
            Check.Null(state.SelectedId);
        }),
        ("catalog refresh preserves selection and replaces preview metadata", () =>
        {
            var state = new ThemeBrowserState(Catalog, "Pills");
            state.Select("Glass");
            state.UpdateCatalog([new("gLass", "Updated Glass", "Dana"), Catalog[1]]);
            Check.Equal("gLass", state.SelectedId);
            Check.Equal("Updated Glass", state.SelectedTheme!.Name);
            Check.Equal("Pills", state.ConfiguredThemeId);
        }),
        ("removing the selected theme chooses an available visible result", () =>
        {
            var state = new ThemeBrowserState(Catalog, "Pills");
            state.Select("Glass");
            state.UpdateCatalog([Catalog[0], Catalog[1]]);
            Check.Equal("Command_Center", state.SelectedId);
            Check.Equal("Pills", state.ConfiguredThemeId);
            state.UpdateCatalog([]);
            Check.Null(state.SelectedId);
            Check.Null(state.SelectedTheme);
            Check.Equal(0, state.VisibleThemes.Count);
        }),
        ("catalog refresh retains search and never selects a hidden result", () =>
        {
            var state = new ThemeBrowserState(Catalog);
            state.SetFilter("bruno");
            state.UpdateCatalog([Catalog[0], Catalog[2]]);
            Check.Equal("bruno", state.Filter);
            Check.Null(state.SelectedId);
            Check.Equal(0, state.VisibleThemes.Count);
        }),
        ("empty or variant-only catalogs have no selected preview", () =>
        {
            foreach (var catalog in new IReadOnlyList<ThemeInfo>[] { [], [Catalog[3]] })
            {
                var state = new ThemeBrowserState(catalog, "Pills");
                Check.Null(state.SelectedId);
                Check.Null(state.SelectedTheme);
                Check.Equal("Pills", state.ConfiguredThemeId);
                Check.Equal(0, state.VisibleThemes.Count);
            }
        }),
        ("browser snapshots its input until explicit catalog refresh", () =>
        {
            var catalog = new List<ThemeInfo> { Catalog[0], Catalog[1] };
            var state = new ThemeBrowserState(catalog);
            catalog.Clear();
            state.SetFilter("");
            Check.Equal(2, state.VisibleThemes.Count);
            state.UpdateCatalog(catalog);
            Check.Equal(0, state.VisibleThemes.Count);
        }),
        ("humanized names preserve authored wording and ampersands", () =>
        {
            Check.Equal("Command Center", ThemeBrowserState.HumanizeName("Command_Center"));
            Check.Equal("A & B", ThemeBrowserState.HumanizeName("  A__&__B  "));
            Check.Equal("TranslucentTaskbar", ThemeBrowserState.HumanizeName("TranslucentTaskbar"));
            Check.Equal("WinXP · Zune", ThemeBrowserState.HumanizeName("WinXP_variant_Zune"));
        })
    ];
}
