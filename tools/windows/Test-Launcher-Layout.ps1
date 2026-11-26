# Does the launcher's settings page line up?
#
#   powershell -File tools/windows/Test-Launcher-Layout.ps1
#
# A row is a name on the left, a value and two arrows on the right. They are
# separate controls, so nothing in WinForms keeps them on one line: an earlier
# version had the name six pixels above its value and the arrows three below
# that, which reads as crooked. This renders each page off screen and measures
# where the *ink* actually is, because a control's bounds say nothing about
# where a glyph lands inside it.
#
# Each page is measured twice -- at the top and scrolled to the bottom -- so the
# rows below the fold are covered too.
#
# Checks, per page: name, value and both arrows share a baseline within 2 px;
# the arrow glyphs are at least 8 px tall (a 10 pt quote mark is 5 px); nothing
# is wider than the card; the page scrolls far enough to reach its last row.
#
# Exit 0 = all pass. Needs the .NET Framework compiler, like the launcher.
param(
    [string]$Root = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
)

$ErrorActionPreference = "Stop"
$csc = Join-Path $env:WINDIR "Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if (-not (Test-Path $csc)) { throw "csc.exe not found; this test needs the .NET Framework compiler." }
$work = Join-Path ([IO.Path]::GetTempPath()) ("redub-layout-" + [guid]::NewGuid().ToString("N").Substring(0, 8))
New-Item -ItemType Directory -Force -Path $work | Out-Null

try {
    $launcher = Join-Path $work "layout-launcher.exe"
    & $csc /nologo /target:winexe /platform:x64 /codepage:65001 "/out:$launcher" `
        (Join-Path $Root ".portable\Launcher.cs") (Join-Path $Root ".portable\Xdvdfs.cs") `
        /reference:System.Windows.Forms.dll /reference:System.Drawing.dll `
        /reference:System.IO.Compression.dll /reference:System.IO.Compression.FileSystem.dll 2>&1 |
        Where-Object { $_ -match ': error ' } | Select-Object -First 5
    if (-not (Test-Path $launcher)) { throw "the launcher did not compile." }

    $harness = Join-Path $work "LayoutProbe.cs"
    @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.Reflection;
using System.Windows.Forms;

public class LayoutProbe {
    static int fails;
    static void Check(string what, bool ok, string detail) {
        Console.WriteLine((ok ? "PASS: " : "FAIL: ") + what + "  [" + detail + "]");
        if (!ok) fails++;
    }

    // The settings card: the scrollable panel holding the category heading and
    // the rows. The page panel has a heading too, so prefer the one with the
    // most non-label children (the arrows).
    static Panel FindCard(Control root, string cat) {
        Panel best = null;
        int bestRows = 0;
        Find(root, cat, ref best, ref bestRows);
        return best;
    }

    static void Find(Control root, string cat, ref Panel best, ref int bestRows) {
        foreach (Control c in root.Controls) {
            Panel p = c as Panel;
            if (p != null && p.AutoScroll) {
                bool heading = false;
                int rows = 0;
                foreach (Control inner in p.Controls) {
                    Label l = inner as Label;
                    if (l != null && l.Text == cat) heading = true;
                    if (!(inner is Label)) rows++;
                }
                if (heading && rows > bestRows) { best = p; bestRows = rows; }
            }
            Find(c, cat, ref best, ref bestRows);
        }
    }

    static bool Light(Color p) { return p.R > 90 || p.G > 90 || p.B > 110; }

    // Top and bottom of the drawn ink in a region of the bitmap.
    static void Ink(Bitmap b, Rectangle area, out int top, out int bottom) {
        top = -1; bottom = -1;
        for (int y = Math.Max(0, area.Top); y < Math.Min(b.Height, area.Bottom); y++) {
            for (int x = Math.Max(0, area.Left); x < Math.Min(b.Width, area.Right); x++) {
                if (Light(b.GetPixel(x, y))) {
                    if (top < 0) top = y;
                    bottom = y;
                    break;
                }
            }
        }
    }

    // Height of the ink a button draws, on its own bitmap.
    static int GlyphHeight(Control b) {
        Bitmap bmp = new Bitmap(Math.Max(1, b.Width), Math.Max(1, b.Height));
        b.DrawToBitmap(bmp, new Rectangle(0, 0, bmp.Width, bmp.Height));
        int top, bottom;
        Ink(bmp, new Rectangle(4, 4, bmp.Width - 8, bmp.Height - 8), out top, out bottom);
        bmp.Dispose();
        return top < 0 ? 0 : bottom - top + 1;
    }

    // Top of the custom scrollbar's thumb: the first light pixel in the strip
    // the panel paints over, from the top of the card.
    static int ThumbTop(Bitmap b, Panel card) {
        int col = card.ClientSize.Width - 5;
        for (int y = 0; y < b.Height; y++) {
            if (col < 0 || col >= b.Width) continue;
            Color p = b.GetPixel(col, y);
            if ((p.R + p.G + p.B) / 3 > 55) return y;
        }
        return -1;
    }

    static string Short(string s) { return s.Length > 26 ? s.Substring(0, 26) : s; }

    [STAThread]
    public static int Main(string[] args) {
        Assembly asm = Assembly.LoadFrom(args[0]);
        Type type = asm.GetType("ReDUB.Launcher", true);
        string root = System.IO.Path.Combine(System.IO.Path.GetTempPath(), "redub-layout-fixture");
        System.IO.Directory.CreateDirectory(System.IO.Path.Combine(root, @"work-remix\build-windows-x86_64\dist"));
        System.IO.Directory.CreateDirectory(System.IO.Path.Combine(root, "game-remix"));

        Form app = (Form)Activator.CreateInstance(type, new object[] { root });
        app.StartPosition = FormStartPosition.Manual;
        app.Location = new Point(-4000, -4000);   // never on screen
        app.Size = new Size(1200, 800);
        app.Show();
        Application.DoEvents();

        MethodInfo show = type.GetMethod("ShowSettings", BindingFlags.NonPublic | BindingFlags.Instance);
        string[] cats = new string[] { "Display", "Graphics", "Controls", "Music" };

        foreach (string cat in cats) {
            show.Invoke(app, new object[] { cat });
            Application.DoEvents();
            Panel card = FindCard(app, cat);
            if (card == null) { Check(cat + ": page built", false, "no card"); continue; }

            int measured = 0, skewed = 0, shortGlyph = 0, outside = 0, bottom = 0, worst = 0, overlapped = 0;
            int scrollMax = Math.Max(0, card.AutoScrollMinSize.Height - card.ClientSize.Height);

            // Top, then the bottom of the page: rows below the fold are only in
            // one of the two renders.
            int[] offsets = new int[] { 0, scrollMax };
            for (int pass = 0; pass < offsets.Length; pass++) {
                card.AutoScrollPosition = new Point(0, offsets[pass]);
                Application.DoEvents();
                Bitmap bmp = new Bitmap(card.Width, card.Height);
                card.DrawToBitmap(bmp, new Rectangle(0, 0, card.Width, card.Height));
                int shift = card.AutoScrollPosition.Y;   // zero or negative

                foreach (Control c in card.Controls) {
                    Label name = c as Label;
                    if (name == null || name.Height < 30 || name.Width < 180) continue;
                    if (name.Top + name.Height > bottom) bottom = name.Top + name.Height;

                    int top = name.Top + shift;
                    if (top < 0 || top + name.Height > bmp.Height) continue;   // not in this render

                    Label value = null;
                    Control left = null, right = null;
                    foreach (Control o in card.Controls) {
                        if (Math.Abs(o.Top - name.Top) > 8) continue;   // same row band
                        Label v = o as Label;
                        // The value sits right of the name in the same band. Test the
                        // left edge, not the name's right edge: the name label is wide
                        // enough to overlap the value in pre-layout coordinates.
                        if (v != null && v != name && v.Left > name.Left + 120) value = v;
                        else if (!(o is Label) && !(o is Panel)) {
                            if (left == null || o.Left < left.Left) left = o;
                            if (right == null || o.Left > right.Left) right = o;
                        }
                    }
                    if (value == null || left == null || right == null) continue;
                    measured++;

                    // No part of a row may sit on top of another. This is the
                    // failure that shipped once: the name label was 372 px wide
                    // and the left arrow started at 335, so the label drew over
                    // the button and it disappeared.
                    Control[] parts = new Control[] { name, left, value, right };
                    Array.Sort(parts, delegate(Control p1, Control p2) { return p1.Left.CompareTo(p2.Left); });
                    for (int k = 1; k < parts.Length; k++) {
                        if (parts[k].Left < parts[k - 1].Right) {
                            overlapped++;
                            if (overlapped <= 3)
                                Console.WriteLine("      overlap: " + Short(parts[k - 1].Text) + " (" + parts[k - 1].Left
                                                  + ".." + parts[k - 1].Right + ") over " + Short(parts[k].Text)
                                                  + " (" + parts[k].Left + ".." + parts[k].Right + ")");
                            break;
                        }
                    }

                    int at, ab, vt, vb, lt, lb, rt, rb;
                    Ink(bmp, new Rectangle(name.Left, top, name.Width, name.Height), out at, out ab);
                    Ink(bmp, new Rectangle(value.Left, top, value.Width, value.Height), out vt, out vb);
                    Ink(bmp, new Rectangle(left.Left + 5, top + 5, left.Width - 10, left.Height - 10), out lt, out lb);
                    Ink(bmp, new Rectangle(right.Left + 5, top + 5, right.Width - 10, right.Height - 10), out rt, out rb);
                    if (at < 0 || vt < 0 || lt < 0 || rt < 0) continue;

                    double a = (at + ab) / 2.0, vc = (vt + vb) / 2.0;
                    double lx = (lt + lb) / 2.0, rx = (rt + rb) / 2.0;
                    double lo = Math.Min(Math.Min(a, vc), Math.Min(lx, rx));
                    double hi = Math.Max(Math.Max(a, vc), Math.Max(lx, rx));
                    int spread = (int)Math.Round(hi - lo);
                    if (spread > 2) {
                        skewed++;
                        if (spread > worst) worst = spread;
                        if (skewed <= 3)
                            Console.WriteLine("      crooked: " + Short(name.Text)
                                + " name=" + a + " value=" + vc + " arrows=" + lx + "/" + rx);
                    }
                    // Glyph height from the button drawing itself: a partially
                    // scrolled row in the page bitmap would measure a clipped
                    // glyph. Counted once, in the first pass.
                    if (pass == 0 && GlyphHeight(left) < 8) shortGlyph++;
                    foreach (Control o in new Control[] { name, value, left, right }) {
                        if (o.Right > card.ClientSize.Width + 2 || o.Left < 0) { outside++; break; }
                    }
                }
                bmp.Dispose();
            }

            Check(cat + ": rows measured", measured >= 2, measured + " row measurements");
            Check(cat + ": name, value and arrows share a baseline", skewed == 0,
                  skewed == 0 ? "within 2 px" : skewed + " rows off, worst " + worst + " px");
            Check(cat + ": arrow glyphs are legible", shortGlyph == 0, shortGlyph + " rows under 8 px");
            Check(cat + ": nothing wider than the card", outside == 0, outside + " rows overflow");
            Check(cat + ": no part of a row overlaps another", overlapped == 0, overlapped + " rows overlap");

            // And nothing else may overlap a row either. Scrolling once pulled
            // the rows back to their unscrolled positions while the hint text
            // and the Advanced button stayed put, so the rows were drawn on top
            // of them. Checked at the bottom of the page, where that happened.
            card.AutoScrollPosition = new Point(0, Math.Max(0, scrollMax));
            Application.DoEvents();
            int shiftNow = card.AutoScrollPosition.Y;
            System.Collections.Generic.List<Rectangle> boxes = new System.Collections.Generic.List<Rectangle>();
            foreach (Control c in card.Controls) {
                int t = c.Top;
                if (t < 0 || t + c.Height > card.ClientSize.Height) continue;   // not fully visible
                boxes.Add(new Rectangle(c.Left, t, c.Width, c.Height));
            }
            int clashes = 0;
            for (int i = 0; i < boxes.Count; i++)
                for (int j = i + 1; j < boxes.Count; j++)
                    if (boxes[i].IntersectsWith(boxes[j])) { clashes++; }
            Check(cat + ": nothing overlaps when scrolled to the bottom", clashes == 0,
                  clashes + " overlapping pairs of " + boxes.Count + " visible controls");
            // Every row must explain itself on hover. The descriptions are a
            // table keyed by setting name, so a row added without one is a row
            // that says nothing.
            FieldInfo tipsField2 = type.GetField("tips", BindingFlags.NonPublic | BindingFlags.Instance);
            ToolTip tips2 = (ToolTip)tipsField2.GetValue(app);
            int rowsSeen = 0, rowsWithTip = 0;
            foreach (Control c in card.Controls) {
                Label n = c as Label;
                if (n == null || n.Height != 42 || n.Width < 180) continue;
                bool peer = false;
                foreach (Control o in card.Controls)
                    if (o != n && Math.Abs(o.Top - n.Top) < 8 && o.Left > n.Left + 120) peer = true;
                if (!peer) continue;
                rowsSeen++;
                string tip = tips2.GetToolTip(n);
                if (tip != null && tip.Length > 15) rowsWithTip++;
                else Console.WriteLine("      no tooltip: " + cat + " / " + n.Text);
            }
            Check(cat + ": every row explains itself on hover", rowsSeen > 0 && rowsWithTip == rowsSeen,
                  rowsWithTip + " of " + rowsSeen + " rows");

            Check(cat + ": scroll reaches the last row", scrollMax == 0 || scrollMax + card.ClientSize.Height >= bottom - 2,
                  "content " + bottom + " px, scroll " + scrollMax + " px, visible " + card.ClientSize.Height + " px");

            // The scrollbar is the one 0.2.5 ships, unchanged: hiding or
            // re-skinning it moves every right-anchored control by the width
            // of the bar (measured 17 px) on each scroll, which is what made
            // rows look crooked. Left alone on purpose.
        }

        app.Dispose();
        Console.WriteLine(fails == 0 ? "ALL PASS" : fails + " FAILED");
        return fails == 0 ? 0 : 1;
    }
}
'@ | Set-Content $harness -Encoding UTF8

    $probe = Join-Path $work "LayoutProbe.exe"
    & $csc /nologo /target:exe "/out:$probe" $harness `
        /reference:System.Windows.Forms.dll /reference:System.Drawing.dll 2>&1 |
        Where-Object { $_ -match ': error ' } | Select-Object -First 5
    if (-not (Test-Path $probe)) { throw "the layout probe did not compile." }

    & $probe $launcher
    if ($LASTEXITCODE -ne 0) { throw "Layout tests failed." }
    exit 0
}
finally {
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
}





