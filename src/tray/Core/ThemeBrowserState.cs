// SPDX-License-Identifier: GPL-3.0-or-later
namespace TaskbarStyler.Tray.Core;

// Preview selection is deliberately independent of persisted configuration.
// Only the caller's explicit apply action may write config or contact the TAP.
public sealed class ThemeBrowserState
{
    private IReadOnlyList<ThemeInfo> catalog;

    public ThemeBrowserState(IReadOnlyList<ThemeInfo> themes, string? configuredTheme = null)
    {
        catalog = Snapshot(themes);
        ConfiguredThemeId = configuredTheme ?? "";
        RebuildVisibleThemes(ConfiguredThemeId);
    }

    public IReadOnlyList<ThemeInfo> VisibleThemes { get; private set; } = Array.Empty<ThemeInfo>();
    public ThemeInfo? SelectedTheme { get; private set; }
    public string? SelectedId => SelectedTheme?.Id;
    public string ConfiguredThemeId { get; private set; }
    public string Filter { get; private set; } = "";

    public void SetFilter(string? filter)
    {
        Filter = filter?.Trim() ?? "";
        RebuildVisibleThemes(SelectedId);
    }

    public bool Select(string? id)
    {
        var theme = VisibleThemes.FirstOrDefault(theme =>
            string.Equals(theme.Id, id, StringComparison.OrdinalIgnoreCase));
        if (theme is null)
            return false;
        SelectedTheme = theme;
        return true;
    }

    public void UpdateCatalog(IReadOnlyList<ThemeInfo> themes)
    {
        catalog = Snapshot(themes);
        RebuildVisibleThemes(SelectedId);
    }

    public void UpdateConfiguredTheme(string? configuredTheme)
    {
        ConfiguredThemeId = configuredTheme ?? "";
    }

    public static string HumanizeName(string name) => string.Join(' ',
        name.Replace("_variant_", " · ", StringComparison.OrdinalIgnoreCase)
            .Replace('_', ' ').Split((char[]?)null, StringSplitOptions.RemoveEmptyEntries));

    private static IReadOnlyList<ThemeInfo> Snapshot(IReadOnlyList<ThemeInfo> themes)
    {
        ArgumentNullException.ThrowIfNull(themes);
        return Array.AsReadOnly(themes.Where(theme => !theme.IsVariant).ToArray());
    }

    private void RebuildVisibleThemes(string? selectedId)
    {
        VisibleThemes = Array.AsReadOnly(catalog.Where(MatchesFilter).ToArray());
        SelectedTheme = VisibleThemes.FirstOrDefault(theme =>
            string.Equals(theme.Id, selectedId, StringComparison.OrdinalIgnoreCase))
            ?? VisibleThemes.FirstOrDefault();
    }

    private bool MatchesFilter(ThemeInfo theme) => Filter.Length == 0 ||
        theme.Name.Contains(Filter, StringComparison.OrdinalIgnoreCase) ||
        theme.Id.Contains(Filter, StringComparison.OrdinalIgnoreCase) ||
        theme.Author.Contains(Filter, StringComparison.OrdinalIgnoreCase) ||
        HumanizeName(theme.Name).Contains(Filter, StringComparison.OrdinalIgnoreCase) ||
        HumanizeName(theme.Id).Contains(Filter, StringComparison.OrdinalIgnoreCase);
}
