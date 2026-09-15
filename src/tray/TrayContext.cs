// SPDX-License-Identifier: GPL-3.0-or-later
using System.Diagnostics;
using TaskbarStyler.Tray.Core;

namespace TaskbarStyler.Tray;

internal sealed class TrayContext : ApplicationContext
{
    private readonly WindowsShell _shell = new();
    private readonly TrayStateMachine _state = new();
    private readonly ConfigStore _config = new(Path.Combine(
        Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "TaskbarStyler", "config.json"));
    private readonly NotifyIcon _icon;
    private readonly ContextMenuStrip _menu = new();
    private readonly ToolStripMenuItem _status = new() { Enabled = false };
    private readonly ToolStripMenuItem _themes = new("Temas");
    private readonly ToolStripMenuItem _disable = new("Desativar tema");
    private readonly ToolStripMenuItem _retry = new("Tentar novamente");
    private readonly ToolStripMenuItem _export = new("Exportar árvore visual");
    private readonly ToolStripMenuItem _restart = new("Reiniciar o Explorer");
    private readonly ToolStripMenuItem _exit = new("Desativar e sair");
    private readonly CommandWindow _window;
    private readonly System.Windows.Forms.Timer _poll = new() { Interval = 30_000 };
    private readonly System.Windows.Forms.Timer _diagnosticRefresh = new() { Interval = 2_000 };
    private DiagnosticWindow? _diagnostics;
    private OperationTrigger? _pending;
    private string _selectedTheme = "";
    private string _catalogErrors = "";
    private string _lastAction = "Inicializando.";
    private bool _busy;
    private bool _closing;

    internal TrayContext()
    {
        _menu.Items.AddRange([
            _status, new ToolStripSeparator(), _themes, _disable, _retry,
            new ToolStripSeparator(), _export,
            new ToolStripMenuItem("Diagnóstico", null, (_, _) => ShowDiagnostics()),
            new ToolStripMenuItem("Abrir log", null, (_, _) => OpenLog()),
            _restart, new ToolStripSeparator(), _exit
        ]);
        _icon = new NotifyIcon { Icon = SystemIcons.Information, Text = "TaskbarStyler — Inativo", ContextMenuStrip = _menu };
        _window = new CommandWindow(OnTaskbarCreated, ShowDiagnostics);
        _ = _window.Handle; // Install the WinForms synchronization context before async work.
        _icon.DoubleClick += (_, _) => ShowDiagnostics();
        _menu.Opening += (_, _) => { RefreshThemes(); UpdateUi(); };
        _disable.Click += async (_, _) => await ChangeThemeAsync("");
        _retry.Click += async (_, _) => await ReconcileAsync(OperationTrigger.UserAction);
        _export.Click += async (_, _) => await ExportAsync();
        _restart.Click += async (_, _) => await RestartAsync();
        _exit.Click += async (_, _) => await ExitAsync();
        _poll.Tick += async (_, _) => await ReconcileAsync(OperationTrigger.Poll);
        _diagnosticRefresh.Tick += (_, _) => RefreshDiagnostics();
        _icon.Visible = true;
        _poll.Start();
        _diagnosticRefresh.Start();
        RefreshThemes();
        _window.BeginInvoke((Action)(async () => await ReconcileAsync(OperationTrigger.Startup)));
        TrayLog.Write("Bandeja iniciada.");
    }

    private void OnTaskbarCreated()
    {
        if (_closing) return;
        _icon.Visible = false;
        _icon.Visible = true;
        TrayLog.Write("TaskbarCreated recebido.");
        _ = ReconcileAsync(OperationTrigger.TaskbarCreated);
    }

    private void RefreshThemes()
    {
        try
        {
            var catalog = ThemeCatalog.Load(_shell.ThemesPath);
            _catalogErrors = string.Join(Environment.NewLine, catalog.Errors);
            foreach (ToolStripItem item in _themes.DropDownItems.Cast<ToolStripItem>().ToArray()) item.Dispose();
            _themes.DropDownItems.Clear();
            foreach (var theme in catalog.Themes.Where(item => !item.IsVariant))
            {
                string id = theme.Id;
                var item = new ToolStripMenuItem(theme.Name.Replace("&", "&&", StringComparison.Ordinal))
                {
                    Tag = id, ToolTipText = theme.Author, Checked = string.Equals(id, _selectedTheme, StringComparison.OrdinalIgnoreCase)
                };
                item.Click += async (_, _) => await ChangeThemeAsync(id);
                _themes.DropDownItems.Add(item);
            }
        }
        catch (Exception error) { RecordFailure(error); }
    }

    private async Task ChangeThemeAsync(string theme)
    {
        if (_busy || _closing) return;
        try
        {
            _config.SaveTheme(theme);
            _selectedTheme = theme;
            await ReconcileAsync(OperationTrigger.UserAction);
        }
        catch (Exception error) { RecordFailure(error); }
        UpdateUi();
    }

    private async Task ReconcileAsync(OperationTrigger trigger)
    {
        if (_closing) return;
        if (_busy)
        {
            if (_pending is null || trigger != OperationTrigger.Poll) _pending = trigger;
            return;
        }
        _busy = true;
        UpdateUi();
        OperationRequest? request = null;
        try
        {
            var config = _config.Read();
            _selectedTheme = config.Theme;
            bool enabled = config.Theme.Length != 0;
            if (enabled && !ThemeCatalog.Load(_shell.ThemesPath).Themes.Any(theme => string.Equals(theme.Id, config.Theme, StringComparison.OrdinalIgnoreCase)))
                throw new InvalidOperationException($"Tema configurado não encontrado no pacote: {config.Theme}.");
            ExplorerIdentity? identity = _shell.FindExplorer();
            bool present = identity is not null && _shell.IsTapPresent(identity.Value, enabled);
            request = _state.BeginOperation(identity, trigger, enabled, present);
            if (request is null) return;
            if (enabled && !WindowsShell.PrerequisiteReady())
                throw new InvalidOperationException("Pré-requisito ausente: execute taskbar-styler setup com autorização antes de aplicar um tema.");
            _lastAction = request.Action switch
            {
                OperationAction.Load => "Carregando o TAP.",
                OperationAction.Reset => "Enviando pedido de desativação.",
                _ => "Enviando pedido de troca de tema."
            };
            UpdateUi();
            await _shell.PerformAsync(request);
            if (_shell.FindExplorer() != request.Explorer)
            {
                _state.BeginOperation(null, OperationTrigger.TaskbarCreated, enabled, false);
                _pending = OperationTrigger.TaskbarCreated;
                return;
            }
            _state.Complete(request.Id, true);
            _lastAction = request.Action == OperationAction.Reset
                ? "Pedido de desativação enviado."
                : "Pedido aceito; falhas de aplicação por regra são registradas no log do TAP.";
            TrayLog.Write($"{request.Action}: pid={request.Explorer.ProcessId}, tema={config.Theme}.");
        }
        catch (Exception error)
        {
            if (request is not null) _state.Complete(request.Id, false, Describe(error));
            else _state.Fail(Describe(error));
            _lastAction = Describe(error);
            TrayLog.Write(error.ToString());
        }
        finally
        {
            _busy = false;
            UpdateUi();
            SchedulePending();
        }
    }

    private void SchedulePending()
    {
        if (_pending is not { } trigger || _closing) return;
        _pending = null;
        _window.BeginInvoke((Action)(async () => await ReconcileAsync(trigger)));
    }

    private async Task ExportAsync()
    {
        if (_busy || _closing) return;
        _busy = true;
        UpdateUi();
        OperationRequest? request = null;
        try
        {
            var identity = _shell.FindExplorer() ?? throw new InvalidOperationException("Aguardando o Explorer.");
            _selectedTheme = _config.Read().Theme;
            request = _state.BeginExport(identity, _selectedTheme.Length != 0);
            if (!_shell.IsTapPresent(identity)) await _shell.LoadAsync(identity);
            DateTime previous = File.GetLastWriteTimeUtc(_shell.TreePath);
            await _shell.SignalAsync(identity, WindowsShell.ExportEvent);
            _lastAction = "Exportação solicitada; aguardando arquivo novo.";
            UpdateUi();
            var limit = Stopwatch.StartNew();
            while (limit.Elapsed < TimeSpan.FromSeconds(15))
            {
                if (_shell.FindExplorer() != identity) throw new InvalidOperationException("O Explorer mudou durante a exportação.");
                var file = new FileInfo(_shell.TreePath);
                if (file.Exists && file.Length != 0 && file.LastWriteTimeUtc != previous)
                {
                    _lastAction = $"Árvore exportada às {DateTime.Now:T}.";
                    OpenFile(_shell.TreePath);
                    _state.Complete(request.Id, true);
                    TrayLog.Write(_lastAction);
                    return;
                }
                await Task.Delay(200);
            }
            throw new TimeoutException("A exportação não produziu arquivo novo em 15 segundos. Consulte o log do TAP.");
        }
        catch (Exception error)
        {
            if (request is not null) _state.Complete(request.Id, false, Describe(error));
            else _state.Fail(Describe(error));
            _lastAction = Describe(error);
            TrayLog.Write(error.ToString());
        }
        finally { _busy = false; UpdateUi(); SchedulePending(); }
    }

    private async Task RestartAsync()
    {
        if (_busy || _closing) return;
        _busy = true;
        _lastAction = "Reiniciando o Explorer por solicitação do menu.";
        _state.BeginOperation(null, OperationTrigger.UserAction, _selectedTheme.Length != 0, false);
        UpdateUi();
        try
        {
            await _shell.RestartExplorerAsync();
            // Shell_TrayWnd can exist well before XAML is ready. The real
            // TaskbarCreated broadcast queues recovery, with polling as backup.
        }
        catch (Exception error) { RecordFailure(error); }
        finally { _busy = false; UpdateUi(); SchedulePending(); }
    }

    private async Task ExitAsync()
    {
        if (_busy || _closing) return;
        await ChangeThemeAsync("");
        if (_state.State == TrayState.Failed) { ShowDiagnostics(); return; }
        _closing = true;
        _poll.Stop();
        _icon.Visible = false;
        TrayLog.Write("Pedido de reset enviado; bandeja encerrando.");
        ExitThread();
    }

    private void RecordFailure(Exception error)
    {
        _state.Fail(Describe(error));
        _lastAction = Describe(error);
        TrayLog.Write(error.ToString());
    }

    internal void ReportUnhandled(Exception error)
    {
        TrayLog.Write(error.ToString());
        if (_closing) return;
        _state.Fail(Describe(error));
        _lastAction = Describe(error);
        try { UpdateUi(); }
        catch (Exception refreshError) { TrayLog.Write(refreshError.ToString()); }
    }

    private static string Describe(Exception error) => $"0x{error.HResult:X8}: {error.Message}";

    private string StateName => _state.State switch
    {
        TrayState.Active => "Ativo",
        TrayState.Failed => "Falhou",
        TrayState.Waiting => "Aguardando",
        _ => "Inativo"
    };

    private void UpdateUi()
    {
        string state = _busy ? "Aguardando" : StateName;
        _status.Text = _selectedTheme.Length == 0 ? state : $"{state} · {_selectedTheme}";
        string tooltip = $"TaskbarStyler — {state}";
        if (_state.State == TrayState.Failed && _state.LastError is { } error) tooltip += $": {error}";
        _icon.Text = tooltip.Length > 63 ? tooltip[..63] : tooltip;
        _icon.Icon = _busy || _state.State == TrayState.Waiting ? SystemIcons.Warning
            : _state.State == TrayState.Failed ? SystemIcons.Error
            : _state.State == TrayState.Active ? SystemIcons.Shield : SystemIcons.Information;
        foreach (var item in new[] { _themes, _disable, _retry, _export, _restart, _exit }) item.Enabled = !_busy && !_closing;
        foreach (ToolStripMenuItem item in _themes.DropDownItems)
            item.Checked = string.Equals((string?)item.Tag, _selectedTheme, StringComparison.OrdinalIgnoreCase);
        RefreshDiagnostics();
    }

    private void ShowDiagnostics()
    {
        if (_diagnostics is null || _diagnostics.IsDisposed)
            _diagnostics = new DiagnosticWindow(OpenLog, RefreshDiagnostics,
                () => _menu.Show(_diagnostics!, new Point(16, 40)));
        RefreshDiagnostics();
        if (_diagnostics.WindowState == FormWindowState.Minimized)
            _diagnostics.WindowState = FormWindowState.Normal;
        _diagnostics.Show();
        // An explicit open must work even when the tray process was launched
        // with STARTF_USESHOWWINDOW/SW_HIDE (for example by a background launcher).
        WindowsShell.Native.ShowWindow(_diagnostics.Handle, 5); // SW_SHOW
        _diagnostics.Activate();
    }

    private void RefreshDiagnostics()
    {
        if (_diagnostics is null || _diagnostics.IsDisposed) return;
        try
        {
            var identity = _shell.FindExplorer();
            string handles = "Indisponível para este processo. Ausência de observação não significa zero.";
            if (identity is not null)
            {
                var observation = DiagnosticLogReader.ReadLatest(Path.Combine(TrayLog.DirectoryPath, "log.txt"), identity.Value);
                if (observation is not null)
                {
                    TimeSpan age = DateTimeOffset.UtcNow - observation.ObservedAt;
                    handles = $"{observation.Observed} registros observados sem liberação confirmada\r\n" +
                        $"Residuais de sessões retiradas: {observation.Residual}\r\n" +
                        $"Contagem incompleta: {(observation.Incomplete ? "sim" : "não")}\r\n" +
                        $"Última observação: {observation.ObservedAt.ToLocalTime():G} (há {Math.Max(0, (int)age.TotalSeconds)} s)" +
                        (age > TimeSpan.FromMinutes(1) ? " — desatualizada" : "");
                }
            }
            _diagnostics.Content = $"Estado: {StateName}\r\nTema configurado: {(_selectedTheme.Length == 0 ? "nenhum" : _selectedTheme)}\r\n" +
                $"Explorer: {(identity is null ? "aguardando" : identity.Value.ProcessId.ToString())}\r\n\r\n" +
                $"{_lastAction}\r\n\r\n{handles}\r\n\r\n" +
                "Ativo indica pedido aceito. A aplicação visual e as falhas por regra ficam no log do TAP.\r\n" +
                $"\r\nPacote: {AppContext.BaseDirectory}\r\nLog do TAP: {Path.Combine(TrayLog.DirectoryPath, "log.txt")}\r\n" +
                $"Log da bandeja: {TrayLog.PathName}" +
                (_catalogErrors.Length == 0 ? "" : $"\r\n\r\nTemas ignorados:\r\n{_catalogErrors}");
        }
        catch (Exception error) { _diagnostics.Content = Describe(error); }
    }

    private void OpenLog()
    {
        string path = Path.Combine(TrayLog.DirectoryPath, "log.txt");
        try { OpenFile(File.Exists(path) ? path : TrayLog.PathName); }
        catch (Exception error) { RecordFailure(error); UpdateUi(); }
    }

    private static void OpenFile(string path)
    {
        Process.Start(new ProcessStartInfo(path)
        {
            UseShellExecute = true
        })?.Dispose();
    }

    protected override void Dispose(bool disposing)
    {
        if (disposing)
        {
            _closing = true;
            _poll.Dispose();
            _diagnosticRefresh.Dispose();
            _diagnostics?.Dispose();
            _icon.Dispose();
            _menu.Dispose();
            _window.Dispose();
        }
        base.Dispose(disposing);
    }

    private sealed class CommandWindow : Form
    {
        private readonly uint _taskbarCreated = WindowsShell.Native.RegisterWindowMessageW("TaskbarCreated");
        private readonly uint _showDiagnostics = WindowsShell.Native.RegisterWindowMessageW("TaskbarStyler.ShowDiagnostics");
        private readonly Action _created;
        private readonly Action _show;
        internal CommandWindow(Action created, Action show)
        {
            _created = created;
            _show = show;
            ShowInTaskbar = false;
            Text = "TaskbarStyler.Commands";
            if (_taskbarCreated == 0 || _showDiagnostics == 0) throw new System.ComponentModel.Win32Exception(System.Runtime.InteropServices.Marshal.GetLastWin32Error());
        }
        protected override void WndProc(ref Message message)
        {
            if ((uint)message.Msg == _taskbarCreated) _created();
            if ((uint)message.Msg == _showDiagnostics) _show();
            base.WndProc(ref message);
        }
    }
}

internal sealed class DiagnosticWindow : Form
{
    private readonly TextBox _content = new()
    {
        Multiline = true, ReadOnly = true, ScrollBars = ScrollBars.Both,
        WordWrap = false, Dock = DockStyle.Fill, Font = new Font("Consolas", 10)
    };
    [System.ComponentModel.DesignerSerializationVisibility(System.ComponentModel.DesignerSerializationVisibility.Hidden)]
    internal string Content { set { if (_content.Text != value) _content.Text = value; } }
    internal DiagnosticWindow(Action openLog, Action refresh, Action showMenu)
    {
        Text = "TaskbarStyler — Diagnóstico";
        Size = new Size(850, 520);
        StartPosition = FormStartPosition.CenterScreen;
        var buttons = new FlowLayoutPanel { Dock = DockStyle.Bottom, Height = 45, Padding = new Padding(8) };
        var log = new Button { Text = "Abrir log", AutoSize = true };
        var menu = new Button { Text = "Abrir menu", AutoSize = true };
        var update = new Button { Text = "Atualizar", AutoSize = true };
        var close = new Button { Text = "Fechar", AutoSize = true };
        log.Click += (_, _) => openLog();
        menu.Click += (_, _) => showMenu();
        update.Click += (_, _) => refresh();
        close.Click += (_, _) => Close();
        buttons.Controls.AddRange([menu, log, update, close]);
        Controls.Add(_content);
        Controls.Add(buttons);
    }
}
