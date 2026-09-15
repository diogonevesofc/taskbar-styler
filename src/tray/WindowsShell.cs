// SPDX-License-Identifier: GPL-3.0-or-later
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using Microsoft.Win32;
using TaskbarStyler.Tray.Core;

namespace TaskbarStyler.Tray;

internal sealed class WindowsShell
{
    internal const string ReloadEvent = @"Local\TaskbarStyler.Reload";
    internal const string ExportEvent = @"Local\TaskbarStyler.ExportTree";
    internal string TapPath { get; } = Path.Combine(AppContext.BaseDirectory, "TaskbarStyler.Tap.dll");
    internal string ThemesPath { get; } = Path.Combine(AppContext.BaseDirectory, "themes");
    internal string TreePath { get; } = Path.Combine(TrayLog.DirectoryPath, "visual-tree.txt");
    private static readonly Guid TapClass = new("B3A1F27C-6E45-4D8A-9F31-0C7E5A2D91B4");

    internal ExplorerIdentity? FindExplorer()
    {
        nint window = Native.FindWindowW("Shell_TrayWnd", null);
        if (window == 0) return null;
        Native.GetWindowThreadProcessId(window, out uint pid);
        if (pid == 0) return null;
        try
        {
            using var process = Process.GetProcessById(checked((int)pid));
            return new ExplorerIdentity(process.Id, process.StartTime.ToUniversalTime().ToFileTimeUtc());
        }
        catch (ArgumentException) { return null; }
        catch (InvalidOperationException) { return null; }
    }

    internal bool IsTapPresent(ExplorerIdentity identity, bool requireCurrentPackage = true)
    {
        if (FindExplorer() != identity) return false;
        using var process = Process.GetProcessById(identity.ProcessId);
        bool found = false;
        foreach (ProcessModule module in process.Modules)
        {
            if (!string.Equals(module.ModuleName, "TaskbarStyler.Tap.dll", StringComparison.OrdinalIgnoreCase))
                continue;
            if (requireCurrentPackage && !string.Equals(Path.GetFullPath(module.FileName), TapPath, StringComparison.OrdinalIgnoreCase))
                throw new InvalidOperationException("TAP de outra pasta carregado. Use Reiniciar o Explorer para carregar esta versão.");
            found = true;
        }
        return found && EventExists(ReloadEvent);
    }

    internal static bool PrerequisiteReady()
    {
        using var machine = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64);
        using var key = machine.OpenSubKey(@"Software\Microsoft\XAML\Debug");
        return key?.GetValue("DisableCompositionDiag") is int value && value == 1;
    }

    internal Task PerformAsync(OperationRequest request) => request.Action switch
    {
        OperationAction.Load => LoadAsync(request.Explorer),
        OperationAction.Reload or OperationAction.Reset => SignalAsync(request.Explorer, ReloadEvent),
        _ => throw new ArgumentOutOfRangeException(nameof(request))
    };

    internal Task SignalAsync(ExplorerIdentity identity, string eventName)
    {
        if (FindExplorer() != identity)
            throw new InvalidOperationException("O Explorer mudou antes de receber o pedido.");
        nint handle = Native.OpenEventW(0x0002, false, eventName); // EVENT_MODIFY_STATE
        if (handle == 0) throw new Win32Exception(Marshal.GetLastWin32Error());
        try
        {
            if (!Native.SetEvent(handle)) throw new Win32Exception(Marshal.GetLastWin32Error());
        }
        finally { Native.CloseHandle(handle); }
        return Task.CompletedTask;
    }

    private static bool EventExists(string name)
    {
        nint handle = Native.OpenEventW(0x0002, false, name);
        if (handle != 0)
        {
            Native.CloseHandle(handle);
            return true;
        }
        int error = Marshal.GetLastWin32Error();
        if (error == 2) return false; // ERROR_FILE_NOT_FOUND
        throw new Win32Exception(error);
    }

    internal Task LoadAsync(ExplorerIdentity identity) => OnStaThread(() =>
    {
        using var process = Process.GetProcessById(identity.ProcessId);
        _ = process.SafeHandle;
        if (process.StartTime.ToUniversalTime().ToFileTimeUtc() != identity.CreationTimeUtcFileTime)
            throw new InvalidOperationException("O Explorer mudou antes da carga.");
        if (FindExplorer() != identity)
            throw new InvalidOperationException("O Explorer mudou antes da carga.");
        if (!File.Exists(TapPath)) throw new FileNotFoundException("TaskbarStyler.Tap.dll ausente do pacote.", TapPath);
        if (!Directory.Exists(ThemesPath)) throw new DirectoryNotFoundException("A pasta themes está ausente do pacote.");
        int apartment = Native.CoInitializeEx(0, 0x2); // COINIT_APARTMENTTHREADED
        Marshal.ThrowExceptionForHR(apartment);
        try
        {
            nint library = Native.LoadLibraryExW("Windows.UI.Xaml.dll", 0, 0x00000800); // SYSTEM32
            if (library == 0) throw new Win32Exception(Marshal.GetLastWin32Error());
            try
            {
                nint address = Native.GetProcAddress(library, "InitializeXamlDiagnosticsEx");
                if (address == 0) throw new Win32Exception(Marshal.GetLastWin32Error());
                var initialize = Marshal.GetDelegateForFunctionPointer<InitializeXamlDiagnostics>(address);
                const int notFound = unchecked((int)0x80070490);
                var readiness = Stopwatch.StartNew();
                bool waitingLogged = false;
                while (true)
                {
                    for (int slot = 1; slot <= 10_000; slot++)
                    {
                        if (process.HasExited || FindExplorer() != identity)
                            throw new InvalidOperationException("O Explorer mudou durante a carga.");
                        if (readiness.Elapsed >= TimeSpan.FromSeconds(5))
                            Marshal.ThrowExceptionForHR(notFound);
                        int result = initialize($"VisualDiagConnection{slot}", checked((uint)identity.ProcessId),
                            "", TapPath, TapClass, ThemesPath);
                        if (result == notFound) continue;
                        Marshal.ThrowExceptionForHR(result);
                        TrayLog.Write($"Carga aceita: pid={identity.ProcessId}, conexão={slot}.");
                        return;
                    }
                    // TaskbarCreated can precede the XAML diagnostics endpoint.
                    // Bound readiness within this operation; completed failures
                    // remain blocked from retries by the periodic poll.
                    if (readiness.Elapsed >= TimeSpan.FromSeconds(5))
                        Marshal.ThrowExceptionForHR(notFound);
                    if (!waitingLogged)
                    {
                        TrayLog.Write($"Aguardando conexão XAML: pid={identity.ProcessId}, limite=5s.");
                        waitingLogged = true;
                    }
                    Thread.Sleep(100);
                }
            }
            finally { Native.FreeLibrary(library); }
        }
        finally { Native.CoUninitialize(); }
    });

    private static Task OnStaThread(Action operation)
    {
        var result = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var thread = new Thread(() =>
        {
            try { operation(); result.SetResult(); }
            catch (Exception error) { result.SetException(error); }
        }) { IsBackground = true, Name = "Taskbar Styler XAML loader" };
        thread.SetApartmentState(ApartmentState.STA);
        thread.Start();
        return result.Task;
    }

    internal async Task RestartExplorerAsync()
    {
        ExplorerIdentity? identity = FindExplorer();
        if (identity is not null)
        {
            using var process = Process.GetProcessById(identity.Value.ProcessId);
            _ = process.SafeHandle;
            if (process.StartTime.ToUniversalTime().ToFileTimeUtc() != identity.Value.CreationTimeUtcFileTime)
                throw new InvalidOperationException("O Explorer mudou antes do reinício.");
            TrayLog.Write($"Reinício solicitado pelo menu: pid={process.Id}.");
            process.Kill();
            await process.WaitForExitAsync().WaitAsync(TimeSpan.FromSeconds(10));
        }
        for (int attempt = 0; attempt < 30; attempt++)
        {
            if (FindExplorer() is not null) return;
            await Task.Delay(100);
        }
        Process.Start(new ProcessStartInfo(Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.Windows), "explorer.exe"))
        {
            UseShellExecute = true,
            WindowStyle = ProcessWindowStyle.Hidden
        })?.Dispose();
    }

    [UnmanagedFunctionPointer(CallingConvention.StdCall, CharSet = CharSet.Unicode)]
    private delegate int InitializeXamlDiagnostics(
        [MarshalAs(UnmanagedType.LPWStr)] string connection, uint processId,
        [MarshalAs(UnmanagedType.LPWStr)] string xamlPath,
        [MarshalAs(UnmanagedType.LPWStr)] string tapPath, Guid classId,
        [MarshalAs(UnmanagedType.LPWStr)] string themesPath);

    internal static class Native
    {
        [DllImport("user32.dll", CharSet = CharSet.Unicode, ExactSpelling = true)]
        internal static extern nint FindWindowW(string? className, string? name);
        [DllImport("user32.dll", ExactSpelling = true)]
        internal static extern uint GetWindowThreadProcessId(nint window, out uint processId);
        [DllImport("user32.dll", CharSet = CharSet.Unicode, SetLastError = true, ExactSpelling = true)]
        internal static extern uint RegisterWindowMessageW(string message);
        [DllImport("user32.dll", SetLastError = true, ExactSpelling = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool PostMessageW(nint window, uint message, nuint wParam, nint lParam);
        [DllImport("user32.dll", ExactSpelling = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool ShowWindow(nint window, int command);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true, ExactSpelling = true)]
        internal static extern nint OpenEventW(uint access, [MarshalAs(UnmanagedType.Bool)] bool inherit, string name);
        [DllImport("kernel32.dll", SetLastError = true, ExactSpelling = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool SetEvent(nint handle);
        [DllImport("kernel32.dll", SetLastError = true, ExactSpelling = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool CloseHandle(nint handle);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true, ExactSpelling = true)]
        internal static extern nint LoadLibraryExW(string fileName, nint file, uint flags);
        [DllImport("kernel32.dll", CharSet = CharSet.Ansi, SetLastError = true, ExactSpelling = true)]
        internal static extern nint GetProcAddress(nint module, string name);
        [DllImport("kernel32.dll", ExactSpelling = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        internal static extern bool FreeLibrary(nint module);
        [DllImport("ole32.dll", ExactSpelling = true)]
        internal static extern int CoInitializeEx(nint reserved, uint apartment);
        [DllImport("ole32.dll", ExactSpelling = true)]
        internal static extern void CoUninitialize();
    }
}
