// SPDX-License-Identifier: GPL-3.0-or-later
using System.Text;
using System.Text.Json;

namespace TaskbarStyler.Tray.Core;

public sealed record ThemeInfo(string Id, string Name, string Author, bool IsVariant = false);
public sealed record ThemeCatalogResult(IReadOnlyList<ThemeInfo> Themes, IReadOnlyList<string> Errors);

public static class ThemeCatalog
{
    public static bool IsValidId(string? id) => id is { Length: > 0 and <= 128 } &&
        !id.Contains("..", StringComparison.Ordinal) && id.All(c =>
            c is >= '0' and <= '9' or >= 'A' and <= 'Z' or >= 'a' and <= 'z' or '_' or '&' or '-' or '.');

    public static ThemeCatalogResult Load(string directory)
    {
        var themes = new List<ThemeInfo>();
        var errors = new List<string>();
        string[] files;
        try { files = Directory.GetFiles(directory, "*.json"); }
        catch (Exception ex) when (ex is IOException or UnauthorizedAccessException)
        {
            errors.Add($"{directory}: {ex.Message}");
            return new(themes.AsReadOnly(), errors.AsReadOnly());
        }

        var seen = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        var variantTargets = new HashSet<string>(StringComparer.OrdinalIgnoreCase);
        foreach (var path in files.Order(StringComparer.OrdinalIgnoreCase))
        {
            var stem = Path.GetFileNameWithoutExtension(path);
            if (stem.Equals("credits", StringComparison.OrdinalIgnoreCase))
                continue;
            try
            {
                if (!IsValidId(stem))
                    throw new JsonException("Filename is not a valid theme identifier.");
                using var document = JsonDocument.Parse(Utf8File.ReadAllText(path));
                var root = document.RootElement;
                if (root.ValueKind != JsonValueKind.Object)
                    throw new JsonException("Theme must be an object.");
                var id = RequiredString(root, "id");
                var name = RequiredString(root, "name");
                if (!IsValidId(id))
                    throw new JsonException("Theme metadata identifier is invalid.");
                string author = "";
                if (root.TryGetProperty("author", out var value) && value.ValueKind != JsonValueKind.Null)
                {
                    if (value.ValueKind != JsonValueKind.String)
                        throw new JsonException("Theme author must be a string.");
                    author = value.GetString()!;
                }
                if (!root.TryGetProperty("rules", out var rules) || rules.ValueKind != JsonValueKind.Array)
                    throw new JsonException("Theme rules must be an array.");
                string? variantId = null;
                if (root.TryGetProperty("osFeatureVariant", out var variant) && variant.ValueKind != JsonValueKind.Null)
                {
                    if (variant.ValueKind != JsonValueKind.Object ||
                        !variant.TryGetProperty("featureId", out var featureId) ||
                        featureId.ValueKind != JsonValueKind.Number || !featureId.TryGetUInt32(out _))
                        throw new JsonException("Theme variant must contain an unsigned 32-bit featureId.");
                    variantId = RequiredString(variant, "themeId");
                    if (!IsValidId(variantId))
                        throw new JsonException("Theme variant identifier is invalid.");
                }
                if (!seen.Add(stem))
                    throw new JsonException("Duplicate theme identifier.");
                // Native loading uses the filename stem. The converter retains
                // upstream display IDs, which can differ from that stem.
                themes.Add(new ThemeInfo(stem, name, author));
                if (variantId is not null && !variantId.Equals(stem, StringComparison.OrdinalIgnoreCase))
                    variantTargets.Add(variantId);
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or JsonException or DecoderFallbackException)
            {
                errors.Add($"{Path.GetFileName(path)}: {ex.Message}");
            }
        }
        for (int i = 0; i < themes.Count; i++)
            if (variantTargets.Contains(themes[i].Id))
                themes[i] = themes[i] with { IsVariant = true };
        return new(themes.AsReadOnly(), errors.AsReadOnly());
    }

    private static string RequiredString(JsonElement root, string field)
    {
        if (!root.TryGetProperty(field, out var value) || value.ValueKind != JsonValueKind.String ||
            string.IsNullOrEmpty(value.GetString()))
            throw new JsonException($"Theme field '{field}' must be a non-empty string.");
        return value.GetString()!;
    }
}
