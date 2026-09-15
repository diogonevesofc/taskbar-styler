// SPDX-License-Identifier: GPL-3.0-or-later
namespace TaskbarStyler.Tray;

internal static class Program
{
    [STAThread]
    private static void Main()
    {
        using var instance = new Mutex(true, @"Local\TaskbarStyler.Tray", out bool first);
        if (!first)
        {
            uint message = WindowsShell.Native.RegisterWindowMessageW("TaskbarStyler.ShowDiagnostics");
            for (int attempt = 0; attempt < 100; attempt++)
            {
                nint window = WindowsShell.Native.FindWindowW(null, "TaskbarStyler.Commands");
                if (window != 0)
                {
                    if (message != 0) WindowsShell.Native.PostMessageW(window, message, 0, 0);
                    return;
                }
                Thread.Sleep(50); // The first instance may still be starting.
            }
            return;
        }
        try
        {
            Application.SetHighDpiMode(HighDpiMode.PerMonitorV2);
            Application.EnableVisualStyles();
            Application.SetCompatibleTextRenderingDefault(false);
            Application.SetUnhandledExceptionMode(UnhandledExceptionMode.CatchException);
            using var context = new TrayContext();
            Application.ThreadException += (_, error) => context.ReportUnhandled(error.Exception);
            Application.Run(context);
        }
        catch (Exception error)
        {
            TrayLog.Write($"Fatal: {error}");
            Environment.ExitCode = 1;
        }
        finally
        {
            instance.ReleaseMutex();
        }
    }
}
