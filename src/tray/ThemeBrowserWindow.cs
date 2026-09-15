// SPDX-License-Identifier: GPL-3.0-or-later
using TaskbarStyler.Tray.Core;

namespace TaskbarStyler.Tray;

internal sealed class ThemeBrowserWindow : Form
{
    private readonly ThemeBrowserState _browser;
    private readonly string _themesPath;
    private readonly Func<string, Task> _applyTheme;
    private readonly Func<Task> _resetTheme;
    private readonly TextBox _search;
    private readonly ThemeListBox _list;
    private readonly Label _count;
    private readonly Label _title;
    private readonly Label _author;
    private readonly Label _description;
    private readonly Label _current;
    private readonly Label _feedback;
    private readonly Label _empty;
    private readonly TaskbarPreviewControl _preview;
    private readonly StudioButton _apply;
    private readonly StudioButton _reset;
    private readonly StudioButton _clear;
    private readonly StudioButton _light;
    private readonly StudioButton _dark;
    private readonly Dictionary<string, ThemePreviewProfile> _profiles = new(StringComparer.OrdinalIgnoreCase);
    private readonly ToolTip _tips = new();
    private bool _rebuilding;
    private bool _busy;
    private bool _failed;
    private bool _lightBackground;

    internal ThemeBrowserWindow(string themesPath, IReadOnlyList<ThemeInfo> themes, string configuredTheme,
        Func<string, Task> applyTheme, Func<Task> resetTheme, Action diagnostics)
    {
        _themesPath = themesPath;
        _browser = new ThemeBrowserState(themes, configuredTheme);
        if (configuredTheme.Length == 0) _browser.Select("TranslucentTaskbar");
        _applyTheme = applyTheme;
        _resetTheme = resetTheme;
        Text = "Taskbar Styler";
        Font = new Font("Segoe UI", 10);
        BackColor = StudioPalette.Background;
        ForeColor = StudioPalette.Ink;
        AutoScaleMode = AutoScaleMode.Dpi;
        AutoScaleDimensions = new SizeF(96, 96);
        ClientSize = new Size(1120, 740);
        MinimumSize = new Size(940, 700);
        StartPosition = FormStartPosition.CenterScreen;
        KeyPreview = true;
        DoubleBuffered = true;

        var page = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 1, RowCount = 3, Margin = Padding.Empty };
        page.RowStyles.Add(new RowStyle(SizeType.Absolute, 88));
        page.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        page.RowStyles.Add(new RowStyle(SizeType.Absolute, 100));
        Controls.Add(page);

        var header = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(28, 16, 28, 14), ColumnCount = 3, BackColor = StudioPalette.Surface, Margin = Padding.Empty };
        header.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        header.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 52));
        header.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        header.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 150));
        var mark = new Label { Text = "\uE790", Font = new Font("Segoe Fluent Icons", 22), ForeColor = StudioPalette.Accent, Dock = DockStyle.Fill, TextAlign = ContentAlignment.MiddleLeft, AccessibleName = "Taskbar Styler" };
        var heading = new TableLayoutPanel { Dock = DockStyle.Fill, RowCount = 2, Margin = Padding.Empty };
        heading.RowStyles.Add(new RowStyle(SizeType.Absolute, 30));
        heading.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        heading.Controls.Add(Label("Taskbar Styler", 18, true), 0, 0);
        heading.Controls.Add(Label("Personalizar sua barra de tarefas", 9, false, StudioPalette.Muted), 0, 1);
        var diagnostic = new StudioButton("Diagnóstico") { Anchor = AnchorStyles.Right, Size = new Size(146, 40), Margin = Padding.Empty, TabIndex = 10 };
        diagnostic.Click += (_, _) => diagnostics();
        header.Controls.Add(mark, 0, 0);
        header.Controls.Add(heading, 1, 0);
        header.Controls.Add(diagnostic, 2, 0);
        page.Controls.Add(header, 0, 0);

        var columns = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, Margin = Padding.Empty };
        columns.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 282));
        columns.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        columns.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        page.Controls.Add(columns, 0, 1);
        var catalog = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(22, 24, 16, 8), RowCount = 4, ColumnCount = 1, Margin = Padding.Empty, BackColor = StudioPalette.Surface };
        catalog.RowStyles.Add(new RowStyle(SizeType.Absolute, 30));
        catalog.RowStyles.Add(new RowStyle(SizeType.Absolute, 44));
        catalog.RowStyles.Add(new RowStyle(SizeType.Absolute, 32));
        catalog.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        columns.Controls.Add(catalog, 0, 0);
        _count = Label("TEMAS", 10, true);
        catalog.Controls.Add(_count, 0, 0);
        var searchFrame = new Panel { Dock = DockStyle.Fill, BackColor = StudioPalette.Background, Padding = new Padding(12, 8, 8, 8), Margin = new Padding(0, 0, 0, 6) };
        _search = new TextBox { Dock = DockStyle.Fill, BorderStyle = BorderStyle.None, BackColor = StudioPalette.Background, ForeColor = StudioPalette.Ink, Font = new Font("Segoe UI", 11), PlaceholderText = "Buscar nome ou autor", AccessibleName = "Buscar temas", TabIndex = 0 };
        searchFrame.Controls.Add(_search);
        catalog.Controls.Add(searchFrame, 0, 1);
        var searchHint = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 2, Margin = Padding.Empty };
        searchHint.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        searchHint.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 65));
        searchHint.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        searchHint.Controls.Add(Label("Ctrl + F para buscar", 8, false, StudioPalette.Muted));
        _clear = new StudioButton("Limpar") { Dock = DockStyle.Fill, Margin = Padding.Empty, Font = new Font("Segoe UI", 8), Visible = false, TabIndex = 1 };
        _clear.Click += (_, _) => { _search.Clear(); _search.Focus(); };
        searchHint.Controls.Add(_clear, 1, 0);
        catalog.Controls.Add(searchHint, 0, 2);
        _list = new ThemeListBox { Dock = DockStyle.Fill, Margin = new Padding(0, 6, 0, 0), TabIndex = 2 };
        catalog.Controls.Add(_list, 0, 3);

        var detail = new TableLayoutPanel { Dock = DockStyle.Fill, Padding = new Padding(30, 24, 30, 18), ColumnCount = 1, RowCount = 6, Margin = Padding.Empty };
        detail.RowStyles.Add(new RowStyle(SizeType.Absolute, 25));
        detail.RowStyles.Add(new RowStyle(SizeType.Absolute, 47));
        detail.RowStyles.Add(new RowStyle(SizeType.Absolute, 34));
        detail.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        detail.RowStyles.Add(new RowStyle(SizeType.Absolute, 46));
        detail.RowStyles.Add(new RowStyle(SizeType.Absolute, 53));
        columns.Controls.Add(detail, 1, 0);
        detail.Controls.Add(Label("PERSONALIZE SEU ESPAÇO", 9, true, StudioPalette.Accent), 0, 0);
        _title = Label("", 27, true);
        _title.AutoEllipsis = true;
        _tips.SetToolTip(_title, "");
        detail.Controls.Add(_title, 0, 1);
        _author = Label("", 10, false, StudioPalette.Muted);
        detail.Controls.Add(_author, 0, 2);
        var previewFrame = new Panel { Dock = DockStyle.Fill, Margin = Padding.Empty, BackColor = Color.FromArgb(24, 43, 48) };
        _preview = new TaskbarPreviewControl { Dock = DockStyle.Fill, AccessibleName = "Prévia ilustrativa da barra de tarefas", TabStop = false };
        _empty = Label("Nenhum tema encontrado.\nTente outro nome ou limpe a busca.", 14, false, StudioPalette.Muted);
        _empty.BackColor = StudioPalette.Background;
        _empty.TextAlign = ContentAlignment.MiddleCenter;
        previewFrame.Controls.Add(_preview);
        previewFrame.Controls.Add(_empty);
        detail.Controls.Add(previewFrame, 0, 3);

        var previewToolbar = new TableLayoutPanel { Dock = DockStyle.Fill, ColumnCount = 4, Margin = Padding.Empty };
        previewToolbar.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        previewToolbar.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 52));
        previewToolbar.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 70));
        previewToolbar.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 76));
        previewToolbar.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        previewToolbar.Controls.Add(Label("Prévia ilustrativa", 9, true, StudioPalette.Muted), 0, 0);
        previewToolbar.Controls.Add(Label("Fundo", 9, false, StudioPalette.Muted), 1, 0);
        _light = new StudioButton("Claro") { Dock = DockStyle.Fill, Margin = new Padding(0, 8, 4, 5), Font = new Font("Segoe UI", 9), TabIndex = 3 };
        _dark = new StudioButton("Escuro") { Dock = DockStyle.Fill, Margin = new Padding(0, 8, 0, 5), Font = new Font("Segoe UI", 9), TabIndex = 4 };
        _light.Click += (_, _) => { _lightBackground = true; RenderSelection(); };
        _dark.Click += (_, _) => { _lightBackground = false; RenderSelection(); };
        previewToolbar.Controls.Add(_light, 2, 0);
        previewToolbar.Controls.Add(_dark, 3, 0);
        detail.Controls.Add(previewToolbar, 0, 4);
        _description = Label("", 9, false, StudioPalette.Muted);
        _description.TextAlign = ContentAlignment.TopLeft;
        detail.Controls.Add(_description, 0, 5);

        var footer = new TableLayoutPanel { Dock = DockStyle.Fill, BackColor = StudioPalette.Surface, Padding = new Padding(28, 16, 28, 12), ColumnCount = 3, Margin = Padding.Empty };
        footer.ColumnStyles.Add(new ColumnStyle(SizeType.Percent, 100));
        footer.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 176));
        footer.ColumnStyles.Add(new ColumnStyle(SizeType.Absolute, 190));
        footer.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        var status = new TableLayoutPanel { Dock = DockStyle.Fill, RowCount = 2, Margin = Padding.Empty };
        status.RowStyles.Add(new RowStyle(SizeType.Absolute, 24));
        status.RowStyles.Add(new RowStyle(SizeType.Percent, 100));
        _current = Label("Padrão do Windows", 10, true);
        _feedback = Label("Selecionar um tema não altera sua barra.", 9, false, StudioPalette.Muted);
        _feedback.AutoEllipsis = true;
        _feedback.TextAlign = ContentAlignment.TopLeft;
        status.Controls.Add(_current, 0, 0);
        status.Controls.Add(_feedback, 0, 1);
        footer.Controls.Add(status, 0, 0);
        _reset = new StudioButton("Restaurar padrão") { Anchor = AnchorStyles.Right, Size = new Size(164, 44), Margin = new Padding(0, 0, 12, 0), TabIndex = 5 };
        _apply = new StudioButton("Aplicar tema", true) { Anchor = AnchorStyles.Right, Size = new Size(190, 44), Margin = Padding.Empty, TabIndex = 6 };
        _apply.Click += async (_, _) => { if (_browser.SelectedId is { } id && !_busy) await _applyTheme(id); };
        _reset.Click += async (_, _) => { if (!_busy) await _resetTheme(); };
        footer.Controls.Add(_reset, 1, 0);
        footer.Controls.Add(_apply, 2, 0);
        page.Controls.Add(footer, 0, 2);
        _tips.SetToolTip(_apply, "Aplica o tema selecionado à barra de tarefas do Windows.");
        _tips.SetToolTip(diagnostic, "Estado detalhado, logs e ferramentas de diagnóstico.");
        _tips.SetToolTip(_light, "Altera apenas o fundo da prévia.");
        _tips.SetToolTip(_dark, "Altera apenas o fundo da prévia.");

        _search.TextChanged += (_, _) => { _browser.SetFilter(_search.Text); RebuildList(); };
        _list.SelectedIndexChanged += (_, _) =>
        {
            if (!_rebuilding && _list.SelectedItem is ThemeInfo theme) { _browser.Select(theme.Id); RenderSelection(); }
        };
        KeyDown += (_, e) =>
        {
            if (e.Control && e.KeyCode == Keys.F) { _search.Focus(); _search.SelectAll(); e.SuppressKeyPress = true; }
            if (e.KeyCode == Keys.Escape) { Hide(); e.SuppressKeyPress = true; }
        };
        FormClosing += (_, e) =>
        {
            if (e.CloseReason == CloseReason.UserClosing) { e.Cancel = true; Hide(); }
        };
        RebuildList();
    }

    internal void UpdateCatalog(IReadOnlyList<ThemeInfo> themes)
    {
        _profiles.Clear();
        _browser.UpdateCatalog(themes);
        RebuildList();
    }

    internal void SelectPreview(string themeId)
    {
        _search.Clear();
        _browser.Select(themeId);
        RebuildList();
    }

    internal void UpdateStatus(string configuredTheme, TrayState state, bool busy, string? failure, string lastAction)
    {
        _browser.UpdateConfiguredTheme(configuredTheme);
        _busy = busy;
        _failed = state == TrayState.Failed;
        _list.ConfiguredTheme = configuredTheme;
        _list.Invalidate();
        string display = _browser.VisibleThemes.FirstOrDefault(t => t.Id.Equals(configuredTheme, StringComparison.OrdinalIgnoreCase))?.Name ?? configuredTheme;
        _current.Text = busy ? "Atualizando a barra de tarefas…" : _failed ? "Não foi possível concluir a operação"
            : state == TrayState.Waiting ? "Aguardando o Explorer…"
            : configuredTheme.Length == 0 ? "Padrão do Windows" : $"Configurado: {ThemeBrowserState.HumanizeName(display)}";
        _feedback.Text = _failed ? (failure ?? "Abra o diagnóstico para ver o erro.")
            : busy ? "Aguarde a operação terminar. Você pode continuar explorando os temas."
            : state == TrayState.Waiting ? "A recuperação será feita quando a barra estiver disponível."
            : lastAction.Contains("Pedido aceito", StringComparison.Ordinal) ? "Pedido enviado. Se o visual não mudar, consulte o diagnóstico."
            : "Fechar esta janela mantém o aplicativo na bandeja.";
        _feedback.ForeColor = _failed ? Color.FromArgb(161, 48, 38) : StudioPalette.Muted;
        _tips.SetToolTip(_feedback, _feedback.Text);
        UpdateActions();
    }

    private void RebuildList()
    {
        _rebuilding = true;
        _list.BeginUpdate();
        try
        {
            _list.Items.Clear();
            foreach (var theme in _browser.VisibleThemes) _list.Items.Add(theme);
            for (int i = 0; i < _list.Items.Count; i++)
                if (((ThemeInfo)_list.Items[i]).Id == _browser.SelectedId) { _list.SelectedIndex = i; break; }
        }
        finally { _list.EndUpdate(); _rebuilding = false; }
        _count.Text = $"TEMAS  /  {_browser.VisibleThemes.Count:00}";
        _clear.Visible = _search.Text.Length != 0;
        RenderSelection();
    }

    private void RenderSelection()
    {
        var theme = _browser.SelectedTheme;
        _empty.Visible = theme is null;
        _empty.Text = _browser.Filter.Length == 0 ? "Nenhum tema disponível.\nConfira o pacote no Diagnóstico."
            : "Nenhum tema encontrado.\nTente outro nome ou limpe a busca.";
        _preview.Visible = theme is not null;
        _title.Text = theme is null ? "Encontre seu estilo" : ThemeBrowserState.HumanizeName(theme.Name);
        _tips.SetToolTip(_title, _title.Text);
        _author.Text = theme is null ? "Busque entre os temas disponíveis neste pacote."
            : theme.Author.Length == 0 ? "Tema da comunidade" : $"Criado por {theme.Author}";
        if (theme is not null)
        {
            if (!_profiles.TryGetValue(theme.Id, out var profile))
                _profiles[theme.Id] = profile = ThemePreviewProfile.Load(_themesPath, theme.Id);
            _empty.Visible = !profile.IsAvailable;
            _empty.Text = "Prévia indisponível para este tema.\nConfira os detalhes no Diagnóstico.";
            _preview.Visible = profile.IsAvailable;
            _preview.SetPreview(profile, _lightBackground);
            _description.Text = "Explore o visual antes de aplicar. A prévia simplifica os efeitos e o layout.\nO resultado depende do Windows, do papel de parede e dos ícones.";
            _tips.SetToolTip(_preview, profile.Summary);
        }
        else _description.Text = "A busca considera o nome do tema e o autor. Nenhuma alteração foi aplicada.";
        _light.BackColor = _lightBackground ? StudioPalette.Selected : StudioPalette.Surface;
        _dark.BackColor = !_lightBackground ? StudioPalette.Selected : StudioPalette.Surface;
        _light.Invalidate();
        _dark.Invalidate();
        UpdateActions();
    }

    private void UpdateActions()
    {
        bool selectedIsConfigured = _browser.SelectedId is { } id && id.Equals(_browser.ConfiguredThemeId, StringComparison.OrdinalIgnoreCase);
        _apply.Text = _busy ? "Aguarde…" : selectedIsConfigured && !_failed ? "Tema configurado" : _failed ? "Tentar aplicar" : "Aplicar tema";
        _apply.Enabled = !_busy && _browser.SelectedId is not null && (!selectedIsConfigured || _failed);
        _reset.Enabled = !_busy && (_browser.ConfiguredThemeId.Length != 0 || _failed);
        _light.Enabled = _dark.Enabled = _browser.SelectedId is not null;
    }

    private static Label Label(string text, float size, bool bold = false, Color? color = null) => new()
    {
        Text = text, Font = new Font("Segoe UI", size, bold ? FontStyle.Bold : FontStyle.Regular),
        ForeColor = color ?? StudioPalette.Ink, Dock = DockStyle.Fill, Margin = Padding.Empty,
        TextAlign = ContentAlignment.MiddleLeft, UseMnemonic = false
    };

    protected override void Dispose(bool disposing)
    {
        if (disposing) _tips.Dispose();
        base.Dispose(disposing);
    }

    private sealed class ThemeListBox : ListBox
    {
        internal string ConfiguredTheme = "";
        internal ThemeListBox()
        {
            DrawMode = DrawMode.OwnerDrawFixed;
            BorderStyle = BorderStyle.None;
            BackColor = StudioPalette.Surface;
            ForeColor = StudioPalette.Ink;
            IntegralHeight = false;
            ItemHeight = 65;
            DisplayMember = nameof(ThemeInfo.Name);
            AccessibleName = "Temas disponíveis";
        }

        protected override void OnDpiChangedAfterParent(EventArgs e)
        {
            base.OnDpiChangedAfterParent(e);
            ItemHeight = (int)(65 * DeviceDpi / 96f);
        }

        protected override void OnHandleCreated(EventArgs e)
        {
            base.OnHandleCreated(e);
            ItemHeight = (int)(65 * DeviceDpi / 96f);
        }

        protected override void OnDrawItem(DrawItemEventArgs e)
        {
            if (e.Index < 0 || Items[e.Index] is not ThemeInfo theme) return;
            float scale = DeviceDpi / 96f;
            bool selected = (e.State & DrawItemState.Selected) != 0;
            bool configured = theme.Id.Equals(ConfiguredTheme, StringComparison.OrdinalIgnoreCase);
            using var background = new SolidBrush(selected ? StudioPalette.Selected : BackColor);
            e.Graphics.FillRectangle(background, e.Bounds);
            if (selected)
            {
                using var line = new SolidBrush(StudioPalette.Accent);
                e.Graphics.FillRectangle(line, e.Bounds.X, e.Bounds.Y + 12 * scale, 3 * scale, e.Bounds.Height - 24 * scale);
            }
            int left = e.Bounds.X + (int)(13 * scale);
            int available = e.Bounds.Width - (int)(26 * scale);
            using var name = new Font("Segoe UI", 10, selected ? FontStyle.Bold : FontStyle.Regular);
            using var subtitle = new Font("Segoe UI", 8);
            TextRenderer.DrawText(e.Graphics, ThemeBrowserState.HumanizeName(theme.Name), name,
                new Rectangle(left, e.Bounds.Y + (int)(10 * scale), available, (int)(24 * scale)), StudioPalette.Ink,
                TextFormatFlags.EndEllipsis | TextFormatFlags.NoPrefix);
            TextRenderer.DrawText(e.Graphics, configured ? "Configurado" : theme.Author.Length == 0 ? "Tema da comunidade" : theme.Author, subtitle,
                new Rectangle(left, e.Bounds.Y + (int)(34 * scale), available, (int)(20 * scale)),
                configured ? StudioPalette.Accent : StudioPalette.Muted, TextFormatFlags.EndEllipsis | TextFormatFlags.NoPrefix);
            if ((e.State & DrawItemState.Focus) != 0) e.DrawFocusRectangle();
        }
    }
}
