// SPDX-License-Identifier: GPL-3.0-or-later
using System.Text;
using System.Text.Json;
using System.Text.Json.Nodes;

namespace TaskbarStyler.Tray.Core;

public sealed record ConfigDocument(string Theme, string LogLevel);

public sealed class ConfigStore(string path)
{
    private readonly string fullPath = Path.GetFullPath(path);

    public ConfigDocument Read()
    {
        var document = ReadObject();
        return new ConfigDocument(ReadString(document, "theme"), ReadString(document, "logLevel"));
    }

    public void SaveTheme(string theme)
    {
        ArgumentNullException.ThrowIfNull(theme);
        if (theme.Length != 0 && !ThemeCatalog.IsValidId(theme))
            throw new ArgumentException("Theme identifier is invalid.", nameof(theme));

        // Read immediately before changing one field: retain user-written and
        // future fields. A malformed file is never treated as a new config.
        var document = ReadObject();
        document["theme"] = theme;
        var directory = Path.GetDirectoryName(fullPath)!;
        Directory.CreateDirectory(directory);
        var temporary = Path.Combine(directory, ".config-" + Guid.NewGuid().ToString("N") + ".tmp");
        try
        {
            using (var stream = new FileStream(temporary, FileMode.CreateNew,
                FileAccess.Write, FileShare.None))
            {
                using (var writer = new Utf8JsonWriter(stream, new JsonWriterOptions { Indented = true }))
                {
                    document.WriteTo(writer);
                    writer.Flush();
                }
                stream.WriteByte((byte)'\n');
                stream.Flush(flushToDisk: true);
            }
            if (File.Exists(fullPath))
                File.Replace(temporary, fullPath, null);
            else
                File.Move(temporary, fullPath);
        }
        finally
        {
            // A failed write/replace leaves the original untouched. Cleanup is
            // best-effort and must not mask the useful write exception.
            try { File.Delete(temporary); }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }
    }

    private JsonObject ReadObject()
    {
        string contents;
        try { contents = Utf8File.ReadAllText(fullPath); }
        catch (FileNotFoundException) { return []; }
        catch (DirectoryNotFoundException) { return []; }

        var document = JsonNode.Parse(contents) as JsonObject
            ?? throw new JsonException("Configuration must be a JSON object.");
        _ = ReadString(document, "theme");
        _ = ReadString(document, "logLevel");
        return document;
    }

    private static string ReadString(JsonObject document, string field)
    {
        if (!document.TryGetPropertyValue(field, out var value))
            return "";
        if (value is not JsonValue json || !json.TryGetValue<string>(out var text))
            throw new JsonException($"Configuration field '{field}' must be a string.");
        return text;
    }
}

internal static class Utf8File
{
    private static readonly UTF8Encoding StrictUtf8 = new(false, true);
    internal static string ReadAllText(string path)
    {
        var bytes = File.ReadAllBytes(path);
        int offset = bytes.AsSpan().StartsWith(new byte[] { 0xef, 0xbb, 0xbf }) ? 3 : 0;
        return StrictUtf8.GetString(bytes, offset, bytes.Length - offset);
    }
}
