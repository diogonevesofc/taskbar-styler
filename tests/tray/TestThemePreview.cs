// SPDX-License-Identifier: GPL-3.0-or-later
using System.Text;
using System.Text.Json;
using TaskbarStyler.Tray.Core;

static class ThemePreviewTests
{
    public static readonly (string Name, Action Run)[] All =
    [
        ("preview never reads traversal or invalid identifiers", () => TempDirectory.Run(dir =>
        {
            foreach (string id in new[] { "../Pills", "..\\Pills", "C:\\Pills", "a/b", "", "a\0b" })
                Check.False(ThemePreviewProfile.Load(dir, id).IsAvailable);
        })),
        ("missing malformed and oversized themes produce a neutral preview", () => TempDirectory.Run(dir =>
        {
            foreach (string data in new[] { "{", "[]", "{}", "{\"rules\":null}", "{\"rules\":[{}]}",
                "{\"constants\":[],\"rules\":[]}", new string(' ', 2 * 1024 * 1024 + 1) })
            {
                File.WriteAllText(Path.Combine(dir, "Broken.json"), data);
                var preview = ThemePreviewProfile.Load(dir, "Broken");
                Check.Null(preview.BackgroundArgb);
                Check.False(preview.IsAvailable);
                Check.True(preview.Summary.Contains("indisponível"));
            }
            Check.False(ThemePreviewProfile.Load(dir, "Missing").IsAvailable);
        })),
        ("preview availability is independent of its display summary", () => TempDirectory.Run(dir =>
        {
            var illustration = new ThemePreviewProfile(PreviewLayout.Dock, null, 1, 8, false, "");
            Check.True(illustration.IsAvailable);
            var missing = ThemePreviewProfile.Load(dir, "Missing") with { Summary = "Different wording" };
            Check.False(missing.IsAvailable);
        })),
        ("unpaired JSON surrogate in a style produces an unavailable preview", () => TempDirectory.Run(dir =>
        {
            File.WriteAllText(Path.Combine(dir, "Broken.json"), InvalidStyleTemplate.Replace("VALUE", "\\uD800", StringComparison.Ordinal));
            // Metadata and the rules array remain valid for catalog listing.
            Check.Equal(1, ThemeCatalog.Load(dir).Themes.Count);
            Check.False(ThemePreviewProfile.Load(dir, "Broken").IsAvailable);
        })),
        ("invalid UTF8 inside a style produces an unavailable preview", () => TempDirectory.Run(dir =>
        {
            string[] parts = InvalidStyleTemplate.Split("VALUE", StringSplitOptions.None);
            File.WriteAllBytes(Path.Combine(dir, "Broken.json"),
                [.. Encoding.UTF8.GetBytes(parts[0]), 0xff, .. Encoding.UTF8.GetBytes(parts[1])]);
            Check.False(ThemePreviewProfile.Load(dir, "Broken").IsAvailable);
        })),
        ("preview resolves nested constants and preserves color alpha", () => TempDirectory.Run(dir =>
        {
            Write(dir, "Glass", new Dictionary<string, string>
            {
                ["tone"] = "#80446688", ["brush"] = "<WindhawkBlur TintColor=\"$tone\" TintOpacity=\"0.5\"/>",
                ["radius"] = "16"
            }, ("Taskbar.TaskbarFrame > Grid#RootGrid", ["Background:=$brush", "CornerRadius=$radius", "Opacity=0.8"]));
            var preview = ThemePreviewProfile.Load(dir, "Glass");
            Check.True(preview.IsAvailable);
            Check.Equal((uint?)0x80446688, preview.BackgroundArgb);
            Check.Equal(.4f, preview.Opacity);
            Check.Equal(16f, preview.CornerRadius);
            Check.True(preview.Blur);
        })),
        ("taskbar preview ignores unrelated flyout and button colors", () => TempDirectory.Run(dir =>
        {
            Write(dir, "Bar", [],
                ("Taskbar.TaskbarBackground#HoverFlyoutBackgroundControl > Grid > Rectangle#BackgroundFill", ["Fill=#FF0000"]),
                ("Taskbar.TaskListButton > Border#BackgroundElement", ["Background=#00FF00"]),
                ("Taskbar.TaskbarFrame > Grid#RootGrid > Taskbar.TaskbarBackground > Grid > Rectangle#BackgroundFill", ["Fill=#123456"]));
            Check.Equal((uint?)0xff123456, ThemePreviewProfile.Load(dir, "Bar").BackgroundArgb);
        })),
        ("transparent or collapsed child exposes the parent preview brush", () => TempDirectory.Run(dir =>
        {
            foreach (string child in new[] { "Fill=Transparent", "Visibility=Collapsed" })
            {
                Write(dir, "Bar", [],
                    ("Taskbar.TaskbarFrame > Grid#RootGrid", ["Background=#123456", "CornerRadius=12"]),
                    ("Taskbar.TaskbarBackground > Grid > Rectangle#BackgroundFill", [child]));
                Check.Equal((uint?)0xff123456, ThemePreviewProfile.Load(dir, "Bar").BackgroundArgb);
            }
        })),
        ("dynamic cyclic and expanding values remain bounded neutral hints", () => TempDirectory.Run(dir =>
        {
            Write(dir, "Dynamic", new Dictionary<string, string> { ["a"] = "$b", ["b"] = "$a" },
                ("Taskbar.TaskbarFrame > Grid#RootGrid", ["Background=$a", "CornerRadius=NaN", "Opacity=Infinity"]));
            var preview = ThemePreviewProfile.Load(dir, "Dynamic");
            Check.Null(preview.BackgroundArgb);
            Check.True(float.IsFinite(preview.Opacity));
            Check.True(float.IsFinite(preview.CornerRadius));
            Check.True(preview.Summary.Contains("neutra"));
            Write(dir, "Dynamic", new Dictionary<string, string> { ["a"] = new string('x', 32768) },
                ("Taskbar.TaskbarFrame > Grid#RootGrid", ["Background=" + string.Concat(Enumerable.Repeat("$a", 1000))]));
            Check.Null(ThemePreviewProfile.Load(dir, "Dynamic").BackgroundArgb);
        })),
        ("preview accepts UTF8 BOM and clamps drawing dimensions", () => TempDirectory.Run(dir =>
        {
            Write(dir, "Bar", [], ("Taskbar.TaskbarFrame > Grid#RootGrid", ["Background=#abc", "CornerRadius=900", "Opacity=-1"]));
            var path = Path.Combine(dir, "Bar.json");
            File.WriteAllText(path, File.ReadAllText(path), new UTF8Encoding(true));
            var preview = ThemePreviewProfile.Load(dir, "Bar");
            Check.Equal((uint?)0xffaabbcc, preview.BackgroundArgb);
            Check.Equal(0f, preview.Opacity);
            Check.Equal(80f, preview.CornerRadius);
        })),
        ("shipped principal themes select their evidenced preview layouts", () =>
        {
            string themes = ThemesDirectory();
            foreach (var (id, layout) in new (string, PreviewLayout)[]
            {
                ("TranslucentTaskbar", PreviewLayout.FullWidth), ("DockLike", PreviewLayout.Dock),
                ("Pills", PreviewLayout.Pills), ("WindowGlass_variant_Split", PreviewLayout.Split),
                ("WindowGlass_variant_FullLength", PreviewLayout.FullWidth),
                ("OS26_Liquid_Glass_variant_ClearMacDock", PreviewLayout.Dock),
                ("One_UI_8_5_variant_Taskbar", PreviewLayout.FullWidth),
                ("Luminosity_variant_Dock", PreviewLayout.Dock)
            }) Check.Equal(layout, ThemePreviewProfile.Load(themes, id).Layout);
            Check.Equal((uint?)0x25323232, ThemePreviewProfile.Load(themes, "TranslucentTaskbar").BackgroundArgb);
            Check.True(ThemePreviewProfile.Load(themes, "Pills").Blur);
        }),
        ("every shipped theme produces finite explicitly illustrative output", () =>
        {
            string directory = ThemesDirectory();
            foreach (var theme in ThemeCatalog.Load(directory).Themes)
            {
                var preview = ThemePreviewProfile.Load(directory, theme.Id);
                Check.True(preview.IsAvailable);
                Check.True(preview.Summary.Contains("ilustrativa"));
                Check.True(float.IsFinite(preview.Opacity) && preview.Opacity is >= 0 and <= 1);
                Check.True(float.IsFinite(preview.CornerRadius) && preview.CornerRadius is >= 0 and <= 80);
            }
        })
    ];

    private const string InvalidStyleTemplate = """
        {"id":"Broken","name":"Broken","rules":[{"target":"Taskbar.TaskbarFrame > Grid#RootGrid","styles":["Background=VALUE"]}]}
        """;

    private static void Write(string directory, string id, Dictionary<string, string> constants,
        params (string Target, string[] Styles)[] rules) => File.WriteAllText(Path.Combine(directory, id + ".json"),
        JsonSerializer.Serialize(new { id, name = id, constants,
            rules = rules.Select(rule => new { target = rule.Target, styles = rule.Styles }) }));

    private static string ThemesDirectory()
    {
        for (var directory = new DirectoryInfo(AppContext.BaseDirectory); directory is not null; directory = directory.Parent)
            if (File.Exists(Path.Combine(directory.FullName, "themes", "Pills.json")))
                return Path.Combine(directory.FullName, "themes");
        throw new DirectoryNotFoundException("The theme corpus is required for preview integration tests.");
    }
}
