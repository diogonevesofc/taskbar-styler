// SPDX-License-Identifier: GPL-3.0-or-later
using System.Drawing.Drawing2D;

namespace TaskbarStyler.Tray;

internal static class StudioPalette
{
    internal static readonly Color Background = Color.FromArgb(246, 248, 248);
    internal static readonly Color Surface = Color.White;
    internal static readonly Color Ink = Color.FromArgb(25, 44, 47);
    internal static readonly Color Muted = Color.FromArgb(88, 105, 108);
    internal static readonly Color Border = Color.FromArgb(222, 230, 229);
    internal static readonly Color Accent = Color.FromArgb(12, 111, 101);
    internal static readonly Color Selected = Color.FromArgb(227, 241, 237);

    internal static GraphicsPath Rounded(RectangleF bounds, float radius)
    {
        var path = new GraphicsPath();
        float diameter = Math.Min(radius * 2, Math.Min(bounds.Width, bounds.Height));
        if (diameter <= 0) { path.AddRectangle(bounds); return path; }
        path.AddArc(bounds.X, bounds.Y, diameter, diameter, 180, 90);
        path.AddArc(bounds.Right - diameter, bounds.Y, diameter, diameter, 270, 90);
        path.AddArc(bounds.Right - diameter, bounds.Bottom - diameter, diameter, diameter, 0, 90);
        path.AddArc(bounds.X, bounds.Bottom - diameter, diameter, diameter, 90, 90);
        path.CloseFigure();
        return path;
    }
}

internal sealed class StudioButton : Button
{
    private bool _hover;
    private readonly bool _primary;

    internal StudioButton(string text, bool primary = false)
    {
        Text = text;
        _primary = primary;
        FlatStyle = FlatStyle.Flat;
        FlatAppearance.BorderSize = 0;
        Font = new Font("Segoe UI", 10, FontStyle.Bold);
        Cursor = Cursors.Hand;
        Size = new Size(164, 42);
        BackColor = StudioPalette.Surface;
        ForeColor = StudioPalette.Ink;
        UseVisualStyleBackColor = false;
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint |
            ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw, true);
    }

    protected override void OnMouseEnter(EventArgs e) { _hover = true; Invalidate(); base.OnMouseEnter(e); }
    protected override void OnMouseLeave(EventArgs e) { _hover = false; Invalidate(); base.OnMouseLeave(e); }
    protected override void OnGotFocus(EventArgs e) { Invalidate(); base.OnGotFocus(e); }
    protected override void OnLostFocus(EventArgs e) { Invalidate(); base.OnLostFocus(e); }

    protected override void OnPaint(PaintEventArgs e)
    {
        e.Graphics.Clear(Parent?.BackColor ?? BackColor);
        e.Graphics.SmoothingMode = SmoothingMode.AntiAlias;
        float scale = DeviceDpi / 96f;
        using var shape = StudioPalette.Rounded(new RectangleF(1, 1, Width - 3, Height - 3), 9 * scale);
        Color fill = !Enabled ? StudioPalette.Border : _primary
            ? (_hover ? Color.FromArgb(9, 89, 82) : StudioPalette.Accent)
            : (_hover ? StudioPalette.Selected : BackColor);
        using var brush = new SolidBrush(fill);
        e.Graphics.FillPath(brush, shape);
        if (!_primary)
        {
            using var border = new Pen(StudioPalette.Border, scale);
            e.Graphics.DrawPath(border, shape);
        }
        TextRenderer.DrawText(e.Graphics, Text, Font, ClientRectangle,
            !Enabled ? StudioPalette.Muted : _primary ? Color.White : ForeColor,
            TextFormatFlags.HorizontalCenter | TextFormatFlags.VerticalCenter | TextFormatFlags.EndEllipsis);
        if (Focused && ShowFocusCues)
        {
            using var focus = new Pen(_primary ? Color.White : StudioPalette.Accent, scale) { DashStyle = DashStyle.Dot };
            using var inset = StudioPalette.Rounded(new RectangleF(4 * scale, 4 * scale,
                Width - 9 * scale, Height - 9 * scale), 6 * scale);
            e.Graphics.DrawPath(focus, inset);
        }
    }
}
