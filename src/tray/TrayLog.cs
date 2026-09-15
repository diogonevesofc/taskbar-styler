// SPDX-License-Identifier: GPL-3.0-or-later
namespace TaskbarStyler.Tray;

internal static class TrayLog
{
    internal static string DirectoryPath => Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "TaskbarStyler");
    internal static string PathName => Path.Combine(DirectoryPath, "tray.log");
    private static readonly object Gate = new();

    internal static void Write(string message)
    {
        try
        {
            lock (Gate)
            {
                Directory.CreateDirectory(DirectoryPath);
                if (File.Exists(PathName) && new FileInfo(PathName).Length > 1_048_576)
                    File.Move(PathName, PathName + ".1", true);
                File.AppendAllText(PathName, $"[{DateTimeOffset.Now:O}] {message}{Environment.NewLine}");
            }
        }
        catch (Exception error) when (error is IOException or UnauthorizedAccessException)
        {
            System.Diagnostics.Debug.WriteLine(error);
        }
    }
}
