// SPDX-License-Identifier: GPL-3.0-or-later
using System.Globalization;
using System.Text.Json;
using System.Text.RegularExpressions;

namespace TaskbarStyler.Tray.Core;

public enum PreviewLayout { FullWidth, Dock, Split, Pills }

// A drawing hint, never a XAML interpreter or a promise of the applied result.
public sealed record ThemePreviewProfile(PreviewLayout Layout, uint? BackgroundArgb,
    float Opacity, float CornerRadius, bool Blur, string Summary)
{
    public bool IsAvailable { get; init; } = true;

    private static readonly Regex Constant = new(@"\$([A-Za-z_][A-Za-z0-9_]*)", RegexOptions.CultureInvariant);
    private static readonly ThemePreviewProfile Unavailable = new(PreviewLayout.FullWidth,
        null, .65f, 8, false, "Prévia ilustrativa indisponível; tema ausente ou inválido.") { IsAvailable = false };

    public static ThemePreviewProfile Load(string themesDirectory, string themeId)
    {
        if (!ThemeCatalog.IsValidId(themeId)) return Unavailable;
        try
        {
            using var file = File.OpenRead(Path.Combine(themesDirectory, themeId + ".json"));
            if (file.Length is <= 0 or > 2 * 1024 * 1024) return Unavailable;
            var bytes = new byte[(int)file.Length];
            file.ReadExactly(bytes);
            var content = bytes.AsMemory();
            if (content.Span.StartsWith(new byte[] { 0xef, 0xbb, 0xbf })) content = content[3..];
            using var document = JsonDocument.Parse(content, new JsonDocumentOptions { MaxDepth = 32 });
            return Read(document.RootElement, themeId);
        }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or
            ArgumentException or NotSupportedException or JsonException or InvalidOperationException or System.Security.SecurityException)
        {
            // JsonElement.GetString can reject malformed UTF-8 or an unpaired
            // JSON surrogate after JsonDocument.Parse itself has succeeded.
            return Unavailable;
        }
    }

    private static ThemePreviewProfile Read(JsonElement root, string themeId)
    {
        if (root.ValueKind != JsonValueKind.Object ||
            !root.TryGetProperty("rules", out var source) || source.ValueKind != JsonValueKind.Array)
            return Unavailable;
        var constants = new Dictionary<string, string>(StringComparer.Ordinal);
        if (root.TryGetProperty("constants", out var values))
        {
            if (values.ValueKind != JsonValueKind.Object) return Unavailable;
            foreach (var value in values.EnumerateObject())
            {
                if (value.Value.ValueKind != JsonValueKind.String) return Unavailable;
                constants[value.Name] = value.Value.GetString()!;
            }
        }
        var rules = new Dictionary<string, Dictionary<string, string>>(StringComparer.Ordinal);
        foreach (var rule in source.EnumerateArray())
        {
            if (rule.ValueKind != JsonValueKind.Object ||
                !rule.TryGetProperty("target", out var target) || target.ValueKind != JsonValueKind.String ||
                !rule.TryGetProperty("styles", out var styles) || styles.ValueKind != JsonValueKind.Array)
                return Unavailable;
            string selector = target.GetString()!;
            if (!rules.TryGetValue(selector, out var properties)) rules[selector] = properties = new(StringComparer.Ordinal);
            foreach (var style in styles.EnumerateArray())
            {
                if (style.ValueKind != JsonValueKind.String) return Unavailable;
                string text = style.GetString()!;
                int equal = text.IndexOf('=');
                if (equal <= 0) continue;
                string property = text[..equal].Trim().TrimEnd(':').TrimEnd();
                properties[property] = Resolve(text[(equal + 1)..].Trim(), constants);
            }
        }

        var candidates = rules.Where(rule => BackgroundScore(rule.Key) > 0 &&
                Get(rule.Value, "Visibility") is not ("Collapsed" or "1"))
            .Select(rule => (Rule: rule, Brush: Get(rule.Value, "Background") ?? Get(rule.Value, "Fill")))
            .Where(item => item.Brush is not null)
            // An invisible fill can expose a real brush on its parent grid.
            .OrderByDescending(item => !IsTransparent(item.Brush!))
            .ThenByDescending(item => BackgroundScore(item.Rule.Key)).ToList();

        var frame = rules.FirstOrDefault(rule => rule.Key.Trim().EndsWith("Taskbar.TaskbarFrame", StringComparison.Ordinal)).Value;
        var grid = rules.FirstOrDefault(rule => IsRootGrid(rule.Key)).Value;
        var tray = rules.FirstOrDefault(rule => IsTrayGrid(rule.Key)).Value;
        bool narrow = Get(frame, "Width")?.Equals("Auto", StringComparison.OrdinalIgnoreCase) == true ||
            HasSideMargins(Get(frame, "Margin"));
        PreviewLayout layout = narrow ? PreviewLayout.Dock : PreviewLayout.FullWidth;
        if (narrow && HasInnerCorners(Get(grid, "CornerRadius")) &&
            Get(tray, "Background") is { } trayBrush && !IsTransparent(trayBrush)) layout = PreviewLayout.Split;
        // This shipped variant spans the background behind both regions and
        // deliberately makes the independent tray background transparent.
        if (themeId.Equals("WindowGlass_variant_FullLength", StringComparison.OrdinalIgnoreCase) &&
            Get(grid, "Background") is not null && IsTransparent(Get(tray, "Background") ?? ""))
            layout = PreviewLayout.FullWidth;
        bool pills = themeId.Equals("Pills", StringComparison.OrdinalIgnoreCase) &&
            rules.Any(rule => rule.Key.Contains("#BackgroundElement", StringComparison.Ordinal) &&
                Get(rule.Value, "Background@NoRunningIndicator") is not null &&
                Radius(Get(rule.Value, "CornerRadius@NoRunningIndicator")) is > 0);
        if (pills) layout = PreviewLayout.Pills;

        string? brush = candidates.FirstOrDefault().Brush;
        var background = candidates.FirstOrDefault().Rule.Value;
        float radius = Radius(Get(background, "CornerRadius")) ?? Radius(Get(background, "RadiusX")) ??
            Radius(Get(grid, "CornerRadius")) ?? 0;
        // Pills leaves the bar background unchanged and draws individual items.
        if (pills)
        {
            var button = rules.First(rule => Get(rule.Value, "Background@NoRunningIndicator") is not null).Value;
            brush = Get(button, "Background@NoRunningIndicator");
            radius = Radius(Get(button, "CornerRadius@NoRunningIndicator")) ?? 7;
        }
        uint? color = Color(brush);
        bool blur = brush?.Contains("<WindhawkBlur", StringComparison.Ordinal) == true ||
            brush?.Contains("<AcrylicBrush", StringComparison.Ordinal) == true;
        float opacity = Number(Get(background, "Opacity")) ?? 1;
        opacity *= Number(Attribute(brush, "TintOpacity")) ?? Number(Attribute(brush, "Opacity")) ?? 1;
        string description = layout switch
        {
            PreviewLayout.Dock => "Barra compacta",
            PreviewLayout.Split => "Blocos separados",
            PreviewLayout.Pills => "Botões em cápsulas",
            _ => "Barra horizontal"
        };
        string palette = color is null ? "paleta neutra para valores dinâmicos ou ausentes" : "cor extraída do tema";
        return new(layout, color, Math.Clamp(opacity, 0, 1), Math.Clamp(radius, 0, 80), blur,
            $"Prévia ilustrativa · {description}, {palette}. Efeitos e geometria simplificados.");
    }

    private static string Resolve(string value, Dictionary<string, string> constants)
    {
        for (int i = 0; i < 8 && value.Length <= 32768; i++)
        {
            int expandedLength = 0;
            bool overflow = false;
            string next = Constant.Replace(value, match =>
            {
                string replacement = constants.GetValueOrDefault(match.Groups[1].Value, match.Value);
                if (replacement.Length > 32768 - expandedLength) { overflow = true; return ""; }
                expandedLength += replacement.Length;
                return replacement;
            });
            if (overflow) return "";
            if (next == value) break;
            value = next;
        }
        return value.Length <= 32768 ? value : "";
    }

    private static string? Get(Dictionary<string, string>? values, string key) => values?.GetValueOrDefault(key);
    private static bool IsRootGrid(string target) => target.Contains("Taskbar.TaskbarFrame", StringComparison.Ordinal) &&
        target.Trim().EndsWith("Grid#RootGrid", StringComparison.Ordinal);
    private static bool IsTrayGrid(string target) => target.Split(',').All(part => part.Trim().EndsWith("#SystemTrayFrameGrid", StringComparison.Ordinal));
    private static int BackgroundScore(string target)
    {
        if (target.Contains("HoverFlyout", StringComparison.Ordinal) || target.Contains("Switcher.", StringComparison.Ordinal)) return 0;
        if (target.Contains("TaskbarBackground", StringComparison.Ordinal) &&
            (target.EndsWith("#BackgroundFill", StringComparison.Ordinal) || target.EndsWith("> Grid", StringComparison.Ordinal))) return 100;
        if (IsRootGrid(target)) return 90;
        if (target is "Rectangle#BackgroundFill" or "Rectangle#BackgroundStroke") return 80;
        return 0;
    }
    private static bool HasSideMargins(string? value)
    {
        string[] parts = value?.Split(',') ?? [];
        return parts.Length == 4 && Number(parts[0]) is > 0 && Number(parts[2]) is > 0;
    }
    private static bool HasInnerCorners(string? value)
    {
        string[] parts = value?.Split(',') ?? [];
        return parts.Length == 4 && Number(parts[1]) is > 0 && Number(parts[2]) is > 0;
    }
    private static float? Number(string? text) => float.TryParse(text?.Trim(), NumberStyles.Float,
        CultureInfo.InvariantCulture, out float value) && float.IsFinite(value) ? value : null;
    private static float? Radius(string? text)
    {
        if (text is null) return null;
        var values = text.Split(',').Select(Number).ToArray();
        return values.Length is 1 or 4 && values.All(value => value is >= 0) ? values.Max() : null;
    }
    private static bool IsTransparent(string value) => value.Equals("Transparent", StringComparison.OrdinalIgnoreCase) ||
        Color(value) is uint color && (color >> 24) == 0 || value.Contains("{{__unset}}", StringComparison.Ordinal);
    private static string? Attribute(string? text, string name)
    {
        if (text is null) return null;
        var match = Regex.Match(text, @"\b" + name + "\\s*=\\s*[\"']([^\"']*)[\"']", RegexOptions.CultureInvariant);
        return match.Success ? match.Groups[1].Value : null;
    }
    private static uint? Color(string? brush)
    {
        if (brush is null) return null;
        string value = (Attribute(brush, "TintColor") ?? Attribute(brush, "Color") ?? brush).Trim();
        if (value.StartsWith('#'))
        {
            string hex = value[1..];
            if (hex.Length is 3 or 4) hex = string.Concat(hex.Select(c => new string(c, 2)));
            if (hex.Length == 6) hex = "FF" + hex;
            return hex.Length == 8 && uint.TryParse(hex, NumberStyles.HexNumber, CultureInfo.InvariantCulture, out uint color) ? color : null;
        }
        return value.ToLowerInvariant() switch
        {
            "transparent" => 0, "black" => 0xff000000, "white" => 0xffffffff,
            "gray" or "grey" => 0xff808080, "red" => 0xffff0000, "blue" => 0xff0000ff,
            _ => null
        };
    }
}
