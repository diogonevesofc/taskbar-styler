// SPDX-License-Identifier: GPL-3.0-or-later
using System.Globalization;
using System.Text;
using System.Text.RegularExpressions;

namespace TaskbarStyler.Tray.Core;

public sealed record DiagnosticObservation(ulong Observed, ulong Residual,
    bool Incomplete, string LogTime, DateTimeOffset ObservedAt,
    DateTimeOffset ReadAt, string RawLine);

public static partial class DiagnosticLogReader
{
    private const long MaximumFileBytes = 2 * 1024 * 1024;

    public static DiagnosticObservation? ReadLatest(string logPath, ExplorerIdentity identity)
    {
        DiagnosticObservation? latest = null;
        var readAt = DateTimeOffset.UtcNow;
        foreach (var path in new[] { logPath + ".1", logPath })
        {
            try
            {
                using var stream = new FileStream(path, FileMode.Open, FileAccess.Read,
                    FileShare.ReadWrite | FileShare.Delete);
                // Log rotation bounds the normal input at 1 MiB. A replaced or
                // corrupt file must not allocate arbitrarily on the tray UI.
                if (stream.Length > MaximumFileBytes)
                    continue;
                var bytes = new byte[checked((int)MaximumFileBytes + 1)];
                int count = stream.ReadAtLeast(bytes, bytes.Length, throwOnEndOfStream: false);
                if (count > MaximumFileBytes)
                    continue;
                // Discard the unfinished record before decoding: an append may
                // end halfway through a multibyte UTF-8 character.
                int lastNewline = bytes.AsSpan(0, count).LastIndexOf((byte)'\n');
                if (lastNewline < 0)
                    continue;
                count = lastNewline + 1;
                int offset = bytes.AsSpan(0, count).StartsWith(new byte[] { 0xef, 0xbb, 0xbf }) ? 3 : 0;
                var contents = new UTF8Encoding(false, true).GetString(bytes, offset, count - offset);
                var candidate = ParseLatest(contents, identity, readAt);
                if (candidate is not null && (latest is null || candidate.ObservedAt >= latest.ObservedAt))
                    latest = candidate;
            }
            catch (Exception ex) when (ex is IOException or UnauthorizedAccessException or DecoderFallbackException)
            {
                // Unavailable diagnostics do not imply zero outstanding records.
            }
        }
        return latest;
    }

    public static DiagnosticObservation? ParseLatest(string content,
        ExplorerIdentity identity, DateTimeOffset readAt)
    {
        DiagnosticObservation? latest = null;
        int start = 0;
        while (start < content.Length)
        {
            int end = content.IndexOf('\n', start);
            if (end < 0)
                break;
            var line = content[start..end].TrimEnd('\r');
            start = end + 1;
            var match = MetricLine().Match(line);
            if (!match.Success ||
                !int.TryParse(match.Groups["prefixPid"].Value, NumberStyles.None, CultureInfo.InvariantCulture, out var prefixPid) ||
                !int.TryParse(match.Groups["pid"].Value, NumberStyles.None, CultureInfo.InvariantCulture, out var pid) ||
                prefixPid != identity.ProcessId || pid != identity.ProcessId ||
                !long.TryParse(match.Groups["created"].Value, NumberStyles.None, CultureInfo.InvariantCulture, out var created) ||
                created != identity.CreationTimeUtcFileTime || created <= 0 ||
                !ulong.TryParse(match.Groups["observed"].Value, NumberStyles.None, CultureInfo.InvariantCulture, out var observed) ||
                !ulong.TryParse(match.Groups["residual"].Value, NumberStyles.None, CultureInfo.InvariantCulture, out var residual) ||
                residual > observed ||
                !long.TryParse(match.Groups["utc"].Value, NumberStyles.None, CultureInfo.InvariantCulture, out var utc) || utc < created ||
                !TimeOnly.TryParseExact(match.Groups["time"].Value, "HH:mm:ss.fff", CultureInfo.InvariantCulture, DateTimeStyles.None, out _))
                continue;

            DateTimeOffset observedAt;
            try { observedAt = DateTimeOffset.FromFileTime(utc).ToUniversalTime(); }
            catch (ArgumentOutOfRangeException) { continue; }
            if (observedAt > readAt.AddSeconds(5))
                continue;
            var candidate = new DiagnosticObservation(observed, residual,
                match.Groups["incomplete"].Value == "1", match.Groups["time"].Value,
                observedAt, readAt, line);
            if (latest is null || observedAt >= latest.ObservedAt)
                latest = candidate;
        }
        return latest;
    }

    [GeneratedRegex(@"^\[(?<time>\d{2}:\d{2}:\d{2}\.\d{3}) (?<prefixPid>\d+)/\d+\] INFO diagnostics handles: pid=(?<pid>\d+) created=(?<created>\d+) observed=(?<observed>\d+) incomplete=(?<incomplete>[01]) residual=(?<residual>\d+) utc=(?<utc>\d+)$", RegexOptions.CultureInvariant | RegexOptions.NonBacktracking)]
    private static partial Regex MetricLine();
}
