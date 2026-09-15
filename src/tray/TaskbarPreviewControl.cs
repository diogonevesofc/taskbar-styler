// SPDX-License-Identifier: GPL-3.0-or-later
using System.Drawing.Drawing2D;
using System.Drawing.Text;
using TaskbarStyler.Tray.Core;

namespace TaskbarStyler.Tray;

internal sealed class TaskbarPreviewControl : Control
{
    private ThemePreviewProfile? _profile;
    private bool _lightBackground;

    // Segoe Fluent Icons: home, search, folder, globe and photo.
    private static readonly string[] AppGlyphs = ["\uE80F", "\uE721", "\uE8B7", "\uE774", "\uE91B"];
    private static readonly Color[] AppColors =
    [Color.FromArgb(90, 183, 238), Color.Empty, Color.FromArgb(233, 188, 97),
     Color.FromArgb(91, 188, 173), Color.FromArgb(170, 151, 217)];
    private static readonly Color[] LightAppColors =
    [Color.FromArgb(27, 117, 159), Color.Empty, Color.FromArgb(164, 116, 31),
     Color.FromArgb(27, 133, 119), Color.FromArgb(108, 81, 157)];

    public TaskbarPreviewControl()
    {
        SetStyle(ControlStyles.UserPaint | ControlStyles.AllPaintingInWmPaint |
            ControlStyles.OptimizedDoubleBuffer | ControlStyles.ResizeRedraw |
            ControlStyles.SupportsTransparentBackColor, true);
        DoubleBuffered = true;
        TabStop = false;
        AccessibleRole = AccessibleRole.Graphic;
        AccessibleName = "Prévia ilustrativa da barra de tarefas";
        BackColor = Color.Transparent;
    }

    public void SetPreview(ThemePreviewProfile? profile, bool lightBackground)
    {
        _profile = profile;
        _lightBackground = lightBackground;
        AccessibleDescription = $"{profile?.Summary ?? "Barra de tarefas neutra"}. " +
            $"Fundo ilustrativo {(lightBackground ? "claro" : "escuro")}; relógio e clima fictícios.";
        Invalidate();
    }

    protected override void OnPaint(PaintEventArgs e)
    {
        base.OnPaint(e);
        if (ClientSize.Width < 2 || ClientSize.Height < 2) return;

        var graphics = e.Graphics;
        var state = graphics.Save();
        try
        {
            graphics.SmoothingMode = SmoothingMode.AntiAlias;
            graphics.PixelOffsetMode = PixelOffsetMode.HighQuality;
            graphics.TextRenderingHint = TextRenderingHint.AntiAliasGridFit;

            // A uniform transform keeps glyphs and rounded corners proportional
            // at every control size. WinForms supplies device-pixel ClientSize.
            float scale = Math.Min(ClientSize.Width / 700f, ClientSize.Height / 300f);
            graphics.ScaleTransform(scale, scale);
            float width = ClientSize.Width / scale;
            float height = ClientSize.Height / scale;
            using var clip = Rounded(new RectangleF(0, 0, width, height), 18);
            graphics.SetClip(clip, CombineMode.Intersect);
            DrawDesktop(graphics, width, height);
            DrawTaskbar(graphics, width, height);
            using var frame = new Pen(Color.FromArgb(_lightBackground ? 28 : 42, Color.White), 1);
            graphics.DrawPath(frame, clip);
        }
        finally
        {
            graphics.Restore(state);
        }
    }

    protected override void OnDpiChangedAfterParent(EventArgs e)
    {
        base.OnDpiChangedAfterParent(e);
        Invalidate();
    }

    private void DrawDesktop(Graphics graphics, float width, float height)
    {
        Color top = _lightBackground ? Color.FromArgb(241, 239, 224) : Color.FromArgb(22, 43, 48);
        Color bottom = _lightBackground ? Color.FromArgb(185, 209, 199) : Color.FromArgb(54, 82, 75);
        using (var backdrop = new LinearGradientBrush(new RectangleF(0, 0, width, height), top, bottom, 115))
            graphics.FillRectangle(backdrop, 0, 0, width, height);

        var halo = new RectangleF(width * .60f, -height * .49f, height * 1.26f, height * 1.26f);
        using (var glow = new LinearGradientBrush(halo,
            _lightBackground ? Color.FromArgb(246, 232, 196) : Color.FromArgb(92, 111, 96),
            _lightBackground ? Color.FromArgb(220, 221, 191) : Color.FromArgb(48, 80, 76), 65))
            graphics.FillEllipse(glow, halo);

        using var ribbon = new GraphicsPath();
        ribbon.AddBezier(-width * .10f, height * .95f, width * .29f, height * 1.04f,
            width * .27f, height * .12f, width * .69f, height * .18f);
        ribbon.AddBezier(width * .69f, height * .18f, width * .84f, height * .18f,
            width * .98f, height * .41f, width * 1.12f, height * .11f);
        ribbon.AddLine(width * 1.12f, height * .11f, width * 1.12f, height * 1.15f);
        ribbon.AddLine(width * 1.12f, height * 1.15f, -width * .10f, height * 1.15f);
        ribbon.CloseFigure();
        using (var fill = new LinearGradientBrush(new RectangleF(0, 0, width, height),
            _lightBackground ? Color.FromArgb(120, 171, 160) : Color.FromArgb(34, 80, 82),
            _lightBackground ? Color.FromArgb(202, 220, 203) : Color.FromArgb(70, 105, 88), 20))
            graphics.FillPath(fill, ribbon);

        using var foreground = new GraphicsPath();
        foreground.AddBezier(-width * .12f, height * 1.08f, width * .29f, height * .52f,
            width * .40f, height * 1.02f, width * .82f, height * .43f);
        foreground.AddBezier(width * .82f, height * .43f, width * .96f, height * .23f,
            width * 1.06f, height * .52f, width * 1.12f, height * .42f);
        foreground.AddLine(width * 1.12f, height * .42f, width * 1.12f, height * 1.15f);
        foreground.AddLine(width * 1.12f, height * 1.15f, -width * .12f, height * 1.15f);
        foreground.CloseFigure();
        using (var fill = new LinearGradientBrush(new RectangleF(0, 0, width, height),
            _lightBackground ? Color.FromArgb(162, 197, 187) : Color.FromArgb(29, 65, 69),
            _lightBackground ? Color.FromArgb(214, 216, 188) : Color.FromArgb(49, 80, 72), 35))
            graphics.FillPath(fill, foreground);

        // Fine arcs belong to the illustrated wallpaper, never the real desktop.
        using var contour = new Pen(Color.FromArgb(_lightBackground ? 54 : 24, Color.White), 1);
        graphics.DrawBezier(contour, -30, height * .70f, width * .29f, height * .88f,
            width * .40f, height * .13f, width * .89f, height * .28f);
        graphics.DrawBezier(contour, width * .25f, height * 1.05f, width * .56f, height * .54f,
            width * .70f, height * .93f, width * 1.06f, height * .34f);
    }

    private void DrawTaskbar(Graphics graphics, float width, float height)
    {
        PreviewLayout layout = _profile?.Layout ?? PreviewLayout.FullWidth;
        var color = _profile?.BackgroundArgb is uint argb
            ? Color.FromArgb(unchecked((int)argb))
            : _lightBackground ? Color.FromArgb(241, 243, 237) : Color.FromArgb(29, 43, 47);
        float opacity = FiniteClamp(_profile?.Opacity ?? .86f, 0, 1, .86f);
        float radius = FiniteClamp(_profile?.CornerRadius ?? 16, 0, 31, 16);
        bool glass = _profile?.Blur ?? true;
        Color fill = Color.FromArgb((int)Math.Round(color.A * opacity), color);

        // Estimate the composite's contrast using the wallpaper under the bar.
        // This affects illustrative icon legibility, not the real theme's rules.
        Color ground = _lightBackground ? Color.FromArgb(171, 196, 177) : Color.FromArgb(40, 74, 68);
        float a = fill.A / 255f;
        float luminance = (.2126f * color.R + .7152f * color.G + .0722f * color.B) * a +
            (.2126f * ground.R + .7152f * ground.G + .0722f * ground.B) * (1 - a);
        Color ink = luminance > 150 ? Color.FromArgb(39, 57, 59) : Color.FromArgb(241, 246, 243);

        float y = height - 88;
        var weather = new RectangleF(22, y, 112, 62);
        var apps = new RectangleF((width - 254) / 2, y, 254, 62);
        var tray = new RectangleF(width - 178, y, 156, 62);
        switch (layout)
        {
            case PreviewLayout.Dock:
                float dockX = (width - 560) / 2;
                DrawSurface(graphics, new RectangleF(dockX, y, 560, 62), radius, fill, glass);
                weather.X = dockX + 10;
                apps.X = dockX + 126;
                tray.X = dockX + 394;
                break;
            case PreviewLayout.Split:
                DrawSurface(graphics, weather, radius, fill, glass);
                DrawSurface(graphics, apps, radius, fill, glass);
                DrawSurface(graphics, tray, radius, fill, glass);
                break;
            case PreviewLayout.Pills:
                DrawSurface(graphics, weather, 31, fill, glass);
                DrawSurface(graphics, tray, 31, fill, glass);
                break;
            default:
                y = height - 68;
                weather.Y = apps.Y = tray.Y = y + 3;
                DrawSurface(graphics, new RectangleF(-1, y, width + 2, 69), 0, fill, glass);
                break;
        }

        using var icons = new Font("Segoe Fluent Icons", 23, FontStyle.Regular, GraphicsUnit.Pixel);
        using var smallIcons = new Font("Segoe Fluent Icons", 15, FontStyle.Regular, GraphicsUnit.Pixel);
        using var text = new Font("Segoe UI", 12, FontStyle.Regular, GraphicsUnit.Pixel);
        using var strongText = new Font("Segoe UI", 13, FontStyle.Bold, GraphicsUnit.Pixel);
        using var secondaryText = new Font("Segoe UI", 10, FontStyle.Regular, GraphicsUnit.Pixel);

        DrawWeather(graphics, weather, icons, text, secondaryText, ink);
        for (int i = 0; i < AppGlyphs.Length; ++i)
        {
            var slot = new RectangleF(apps.X + 8 + i * 48, apps.Y + 5, 46, 52);
            if (layout == PreviewLayout.Pills)
                DrawSurface(graphics, slot, 23, fill, glass);
            if (i == 2)
            {
                using var selected = new SolidBrush(Color.FromArgb(luminance > 150 ? 19 : 22, ink));
                using var highlight = Rounded(new RectangleF(slot.X + 5, slot.Y + 6, 36, 36), 10);
                graphics.FillPath(selected, highlight);
            }
            Color iconColor = i == 1 ? ink : luminance > 150 ? LightAppColors[i] : AppColors[i];
            DrawText(graphics, AppGlyphs[i], icons, iconColor,
                new RectangleF(slot.X + 3, slot.Y + 5, 40, 36));
            if (i == 2)
            {
                using var active = new SolidBrush(luminance > 150 ? Color.FromArgb(45, 112, 105) : Color.FromArgb(163, 218, 201));
                using var indicator = Rounded(new RectangleF(slot.X + 15, slot.Bottom - 7, 16, 3), 1.5f);
                graphics.FillPath(active, indicator);
            }
        }
        DrawTray(graphics, tray, smallIcons, strongText, secondaryText, ink);
    }

    private static void DrawWeather(Graphics graphics, RectangleF area, Font icons,
        Font text, Font secondary, Color ink)
    {
        DrawText(graphics, "\uE706", icons, ink.R < 100 ? Color.FromArgb(157, 111, 29) : Color.FromArgb(226, 181, 90),
            new RectangleF(area.X + 10, area.Y + 15, 29, 30));
        DrawText(graphics, "22°", text, ink, new RectangleF(area.X + 44, area.Y + 13, 49, 18), false);
        DrawText(graphics, "Tempo bom", secondary, Color.FromArgb(180, ink),
            new RectangleF(area.X + 44, area.Y + 32, 62, 15), false);
    }

    private static void DrawTray(Graphics graphics, RectangleF area, Font icons,
        Font clock, Font secondary, Color ink)
    {
        DrawText(graphics, "\uE70E", icons, ink, new RectangleF(area.X + 8, area.Y + 21, 20, 20));
        DrawText(graphics, "\uE701", icons, ink, new RectangleF(area.X + 34, area.Y + 21, 20, 20));
        DrawText(graphics, "\uE767", icons, ink, new RectangleF(area.X + 58, area.Y + 21, 20, 20));
        DrawText(graphics, "09:41", clock, ink, new RectangleF(area.X + 88, area.Y + 12, 55, 21));
        DrawText(graphics, "15/09", secondary, Color.FromArgb(180, ink),
            new RectangleF(area.X + 88, area.Y + 34, 55, 15));
    }

    private static void DrawSurface(Graphics graphics, RectangleF bounds, float radius, Color fill, bool glass)
    {
        for (int i = 3; i > 0; --i)
        {
            var shadowBounds = bounds;
            shadowBounds.Inflate(i * 1.4f, i * 1.4f);
            shadowBounds.Offset(0, 3 + i);
            using var shadow = new SolidBrush(Color.FromArgb(7, 8, 24, 27));
            using var shape = Rounded(shadowBounds, radius + i);
            graphics.FillPath(shadow, shape);
        }
        using var outline = Rounded(bounds, radius);
        using (var surface = new SolidBrush(fill)) graphics.FillPath(surface, outline);
        if (glass)
        {
            // A translucent light wash suggests glass; it never samples or
            // blurs desktop pixels, and does not claim XAML rendering parity.
            using var sheen = new LinearGradientBrush(bounds,
                Color.FromArgb(34, Color.White), Color.FromArgb(4, Color.White), 90);
            graphics.FillPath(sheen, outline);
        }
        using var border = new Pen(Color.FromArgb(glass ? 80 : 38, Color.White), 1);
        graphics.DrawPath(border, outline);
    }

    private static void DrawText(Graphics graphics, string value, Font font, Color color,
        RectangleF bounds, bool centered = true)
    {
        using var brush = new SolidBrush(color);
        using var format = new StringFormat
        {
            Alignment = centered ? StringAlignment.Center : StringAlignment.Near,
            LineAlignment = StringAlignment.Center,
            FormatFlags = StringFormatFlags.NoWrap,
            Trimming = StringTrimming.None
        };
        graphics.DrawString(value, font, brush, bounds, format);
    }

    private static GraphicsPath Rounded(RectangleF bounds, float radius)
    {
        var path = new GraphicsPath();
        radius = Math.Min(radius, Math.Min(bounds.Width, bounds.Height) / 2);
        if (radius <= 0)
        {
            path.AddRectangle(bounds);
            return path;
        }
        float diameter = radius * 2;
        path.AddArc(bounds.Left, bounds.Top, diameter, diameter, 180, 90);
        path.AddArc(bounds.Right - diameter, bounds.Top, diameter, diameter, 270, 90);
        path.AddArc(bounds.Right - diameter, bounds.Bottom - diameter, diameter, diameter, 0, 90);
        path.AddArc(bounds.Left, bounds.Bottom - diameter, diameter, diameter, 90, 90);
        path.CloseFigure();
        return path;
    }

    private static float FiniteClamp(float value, float minimum, float maximum, float fallback) =>
        float.IsFinite(value) ? Math.Clamp(value, minimum, maximum) : fallback;
}
