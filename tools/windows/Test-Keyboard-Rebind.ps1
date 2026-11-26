# Does the keyboard rebind page behave like a modern game's?
#
#   powershell -File tools/windows/Test-Keyboard-Rebind.ps1
#
# The page is a grid: one group per part of the pad, the action on the left and
# its key in a field on the right. Click a field and it reads "Press a key...";
# the next key you press becomes it. Escape cancels, or closes the page when
# nothing is being captured. Clear unbinds the selected row, Reset restores the
# defaults, right-click clears one row.
#
# The important part: nothing in the capture path is modal. A key that is
# already used turns the field amber, names the action holding it in the status
# line, and is taken only if it is pressed again. A prompt there would arrive
# mid-finger-stroke and had to be read before it could be answered, so the
# checks below assert that no message box appears at all.
#
# The page is modal (ShowDialog blocks its caller), so the probe cannot drive it
# from outside. A timer is queued on the UI thread instead, which runs inside the
# page's own message loop: exactly where a player's click happens. A watchdog
# thread reports and answers any message box, so one where none belongs is a
# failure rather than a hang.
#
# Exit 0 = all pass. Needs the .NET Framework compiler, like the launcher.
param(
    [string]$Root = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
)

$ErrorActionPreference = "Stop"
$csc = Join-Path $env:WINDIR "Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if (-not (Test-Path $csc)) { throw "csc.exe not found; this test needs the .NET Framework compiler." }
$work = Join-Path ([IO.Path]::GetTempPath()) ("redub-rebind-" + [guid]::NewGuid().ToString("N").Substring(0, 8))
New-Item -ItemType Directory -Force -Path $work | Out-Null

try {
    $launcher = Join-Path $work "rebind-launcher.exe"
    & $csc /nologo /target:winexe /platform:x64 /codepage:65001 "/out:$launcher" `
        (Join-Path $Root ".portable\Launcher.cs") (Join-Path $Root ".portable\Xdvdfs.cs") `
        /reference:System.Windows.Forms.dll /reference:System.Drawing.dll `
        /reference:System.IO.Compression.dll /reference:System.IO.Compression.FileSystem.dll 2>&1 |
        Where-Object { $_ -match ': error ' } | Select-Object -First 5
    if (-not (Test-Path $launcher)) { throw "the launcher did not compile." }

    $probe = Join-Path $work "RebindProbe.cs"
    @'
using System;
using System.Collections.Generic;
using System.Drawing;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Forms;

class RebindProbe {
    [DllImport("user32.dll")] static extern bool EnumWindows(EnumProc cb, IntPtr p);
    [DllImport("user32.dll")] static extern bool EnumChildWindows(IntPtr h, EnumProc cb, IntPtr p);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] static extern bool PostMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] static extern IntPtr SendMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    delegate bool EnumProc(IntPtr h, IntPtr p);

    static readonly object gate = new object();
    static string logPath;
    static int fails;
    static volatile int boxes;
    static Form dialogRef;
    static bool expectClose;

    static readonly Color Ink = Color.FromArgb(235, 239, 247);
    static readonly Color Amber = Color.FromArgb(245, 180, 120);
    static readonly Color Muted = Color.FromArgb(153, 165, 188);

    static void Log(string s) {
        lock (gate) File.AppendAllText(logPath, s + "\r\n", Encoding.UTF8);
        Console.WriteLine(s);
    }
    static void Check(string what, bool ok, string detail) {
        Log((ok ? "PASS: " : "FAIL: ") + what + "  [" + detail + "]");
        if (!ok) fails++;
    }
    static string Tail() {
        lock (gate) return File.Exists(logPath) ? File.ReadAllText(logPath) : "";
    }

    static List<IntPtr> WindowsOfClass(uint pid, string cls) {
        var found = new List<IntPtr>();
        EnumWindows((h, p) => {
            uint owner; GetWindowThreadProcessId(h, out owner);
            if (owner != pid) return true;
            var sb = new StringBuilder(256); GetClassName(h, sb, 256);
            if (sb.ToString() == cls) found.Add(h);
            return true;
        }, IntPtr.Zero);
        return found;
    }
    static string TextOf(IntPtr h) {
        var sb = new StringBuilder(512); GetWindowText(h, sb, 512);
        return sb.ToString();
    }

    // A message box blocks the UI thread, which is where this probe runs. The
    // watchdog answers it so a run cannot hang, and records the title.
    static void ExpectBox(string title, int before) {
        for (int i = 0; i < 24 && boxes == before; i++) Thread.Sleep(100);
        Check("a \"" + title + "\" prompt appears", boxes > before, boxes - before + " box(es)");
        for (int i = 0; i < 10 && Tail().IndexOf("title=[" + title + "]") < 0; i++) Thread.Sleep(100);
    }

    static Control ByTag(Control root, string tag) {
        foreach (Control c in root.Controls) {
            if (c.Tag as string == tag) return c;
            Control d = ByTag(c, tag);
            if (d != null) return d;
        }
        return null;
    }
    static Control ByText(Control root, string text) {
        foreach (Control c in root.Controls) {
            if (c.Text == text) return c;
            Control d = ByText(c, text);
            if (d != null) return d;
        }
        return null;
    }
    static Control ByName(Control root, string name) {
        foreach (Control c in root.Controls) {
            if (c.Name == name) return c;
            Control d = ByName(c, name);
            if (d != null) return d;
        }
        return null;
    }
    static int CountLabels(Control root) {
        int n = 0;
        foreach (Control c in root.Controls) { if (c is Label) n++; n += CountLabels(c); }
        return n;
    }
    static List<Control> Fields(Control root) {
        var l = new List<Control>();
        foreach (Control c in root.Controls) {
            if (c.GetType().Name == "RedubButton") l.Add(c);
            l.AddRange(Fields(c));
        }
        return l;
    }
    static void Click(Control c) {
        typeof(Control).GetMethod("OnClick", BindingFlags.NonPublic | BindingFlags.Instance)
            .Invoke(c, new object[] { EventArgs.Empty });
    }
    static void Key(Control f, Keys k) {
        typeof(Control).GetMethod("OnKeyDown", BindingFlags.NonPublic | BindingFlags.Instance)
            .Invoke(f, new object[] { new KeyEventArgs(k) });
    }
    static string Env(string path) { return File.Exists(path) ? File.ReadAllText(path) : ""; }
    static bool Wrote(string text, string line) {
        return text.Replace(" ", "").IndexOf(line.Replace(" ", ""), StringComparison.Ordinal) >= 0;
    }
    static bool Same(Color a, Color b) { return a.R == b.R && a.G == b.G && a.B == b.B; }

    [STAThread]
    static int Main(string[] args) {
        logPath = Path.Combine(args[2], "rebind.log");
        File.WriteAllText(logPath, "");

        uint pid = (uint)System.Diagnostics.Process.GetCurrentProcess().Id;
        var seen = new HashSet<IntPtr>();
        var watchdog = new Thread(() => {
            for (int i = 0; i < 400; i++) {
                Thread.Sleep(150);
                foreach (IntPtr h in WindowsOfClass(pid, "#32770")) {
                    if (!seen.Add(h)) continue;
                    var body = new StringBuilder();
                    IntPtr answer = IntPtr.Zero;
                    EnumChildWindows(h, (c, p) => {
                        var cls = new StringBuilder(64); GetClassName(c, cls, 64);
                        string text = TextOf(c);
                        if (cls.ToString() == "Button") {
                            if (text.IndexOf("No", StringComparison.OrdinalIgnoreCase) >= 0 && answer == IntPtr.Zero)
                                answer = c;
                        } else if (text.Length > 0) {
                            body.Append(text).Append(" | ");
                        }
                        return true;
                    }, IntPtr.Zero);
                    if (answer == IntPtr.Zero) {
                        EnumChildWindows(h, (c, p) => {
                            var cls = new StringBuilder(64); GetClassName(c, cls, 64);
                            if (cls.ToString() == "Button" && answer == IntPtr.Zero) answer = c;
                            return true;
                        }, IntPtr.Zero);
                    }
                    boxes++;
                    Log("!! MESSAGE BOX: title=[" + TextOf(h) + "] body=[" + body + "]");
                    if (answer != IntPtr.Zero) SendMessage(answer, 0x00F5 /* BM_CLICK */, IntPtr.Zero, IntPtr.Zero);
                    else PostMessage(h, 0x0010, IntPtr.Zero, IntPtr.Zero);
                }
            }
        });
        watchdog.IsBackground = true;
        watchdog.Start();

        Assembly asm = Assembly.LoadFrom(args[0]);
        Type type = asm.GetType("ReDUB.Launcher", true);
        string root = args[1];
        Form app = (Form)Activator.CreateInstance(type, new object[] { root });
        app.Show();
        Application.DoEvents();

        string envPath = Path.Combine(root, @"work-remix\build-windows-x86_64\dist\mcdub3remix_env.txt");
        Directory.CreateDirectory(Path.GetDirectoryName(envPath));
        // The launcher refuses to save unless the settings file exists, which is
        // the "Install the game before saving settings" guard.
        if (!File.Exists(envPath)) File.WriteAllText(envPath, "# scratch settings\r\n");

        var t = new System.Windows.Forms.Timer { Interval = 400 };
        t.Tick += (s, e) => {
            Form dialog = null;
            foreach (Form f in Application.OpenForms)
                if (f != app && f.Text == "Keyboard (player 1)") dialog = f;
            if (dialog == null) return;
            t.Stop();
            dialogRef = dialog;
            try { Run(app, dialog, envPath); }
            catch (Exception ex) { Log("EXCEPTION: " + ex.ToString().Split('\n')[0]); fails++; }
            // The last step closes the page, which unwinds the modal loop this
            // timer runs inside; Main reports the result once ShowDialog returns.
            if (!expectClose) Environment.Exit(fails == 0 ? 0 : 1);
        };
        t.Start();

        type.GetMethod("ShowKeyBindings", BindingFlags.NonPublic | BindingFlags.Instance)
            .Invoke(app, null);
        if (!expectClose) {
            Log("FAIL: the page closed without the probe running");
            fails++;
        } else {
            Check("Escape closes the page when idle",
                  dialogRef == null || !dialogRef.Visible,
                  dialogRef == null ? "gone" : "visible=" + dialogRef.Visible);
        }
        Log(fails == 0 ? "ALL PASS" : fails + " FAILED");
        return fails == 0 ? 0 : 1;
    }

    static void Run(Form app, Form dialog, string envPath) {
        string[] actions = {
            "DPAD_UP","DPAD_DOWN","DPAD_LEFT","DPAD_RIGHT","START","BACK","LSTICK","RSTICK",
            "A","B","X","Y","WHITE","BLACK","LT","RT",
            "LSTICK_LEFT","LSTICK_RIGHT","LSTICK_UP","LSTICK_DOWN",
            "RSTICK_LEFT","RSTICK_RIGHT","RSTICK_UP","RSTICK_DOWN"
        };

        int bound = 0;
        foreach (string act in actions) if (ByTag(dialog, act) != null) bound++;
        Check("every action has a field", bound == 24, bound + " of 24");
        Check("the page is grouped, not one long list", ByText(dialog, "D-PAD") != null && ByText(dialog, "FACE BUTTONS") != null,
              "group headers");

        var status = ByName(dialog, "status");
        Check("the status line exists", status != null, status == null ? "missing" : "named");
        var clear = ByText(dialog, "Clear key");
        Check("Clear is disabled until a key is selected", clear != null && !clear.Enabled,
              clear == null ? "no button" : "Enabled=" + clear.Enabled);

        var a = ByTag(dialog, "A");
        var b = ByTag(dialog, "B");
        Check("a field shows its current key", a.Text == "A", a.Text);
        // Keys are shown the way a player reads them, not as the setting's name.
        Check("Return reads as Enter", ByTag(dialog, "START").Text == "Enter", ByTag(dialog, "START").Text);


        int labelsBefore = CountLabels(dialog);
        int boxesBefore = boxes;

        Click(a);
        Check("clicking a field asks for a key", a.Text.StartsWith("Press a key"), a.Text);
        Check("  and the status line names the action", status.Text.Contains("A"), status.Text);
        Check("  and Clear becomes available", clear.Enabled, "Enabled=" + clear.Enabled);

        Key(dialog, Keys.P);
        Check("the pressed key becomes the binding", a.Text == "P", a.Text);
        Check("the binding is written for the game", Wrote(Env(envPath), "MC3_KEY_A=P"), "MC3_KEY_A=P");
        Check("saving says what happened", status.Text.Contains("now P"), status.Text);
        Check("no duplicated labels after a rebind", CountLabels(dialog) == labelsBefore,
              labelsBefore + " -> " + CountLabels(dialog));

        Click(a);
        Key(dialog, Keys.Escape);
        Check("Escape cancels and keeps the key", a.Text == "P", a.Text);
        Check("  and puts the hint back", status.Text.StartsWith("Click a key"), status.Text);

        // A key already doing something else is named, not taken.
        Click(b);
        Key(dialog, Keys.P);
        Check("a key in use stays in the field, amber", b.Text == "P" && Same(b.ForeColor, Amber), b.Text);
        Check("  the status line names the action holding it", status.Text.Contains("already") && status.Text.Contains("A"), status.Text);
        Check("  and nothing is written yet", !Wrote(Env(envPath), "MC3_KEY_B=P"), "MC3_KEY_B not written");
        Check("  no message box interrupts the capture", boxes == boxesBefore, boxes - boxesBefore + " box(es)");

        Key(dialog, Keys.P);
        Check("pressing it again takes it", b.Text == "P" && Wrote(Env(envPath), "MC3_KEY_B=P"), "MC3_KEY_B=P");
        Check("  a shared key is highlighted on both rows", Same(a.ForeColor, Amber) && Same(b.ForeColor, Amber), "amber");

        // Clear works on the selected row.
        Click(a);
        Key(dialog, Keys.Escape);
        clear = ByText(dialog, "Clear key");
        Click(clear);
        Check("Clear unbinds the selected row", a.Text == "\u2014", a.Text);
        Check("  an unbound key is a dash", a.Text == "\u2014", a.Text);
        Check("  shown muted, not as an error", Same(a.ForeColor, Muted), a.ForeColor.ToString());
        Check("  and writes -1, which the game reads as unbound", Wrote(Env(envPath), "MC3_KEY_A=-1"), "-1");
        Check("  and says so", status.Text.Contains("unbound"), status.Text);

        // Right-click clears a single row without selecting it first.
        Check("a row has a right-click menu", a.ContextMenuStrip != null, a.ContextMenuStrip == null ? "none" : "present");
        if (a.ContextMenuStrip != null)
            Check("  offering Clear and Reset", a.ContextMenuStrip.Items.Count == 2 &&
                  a.ContextMenuStrip.Items[0].Text == "Clear this key", "items");

        // Reset asks first; declining leaves every binding alone.
        int beforeReset = boxes;
        Click(ByText(dialog, "Reset to defaults"));
        ExpectBox("Reset keyboard", beforeReset);
        Check("Reset asks before touching anything", a.Text == "\u2014" && b.Text == "P", "unchanged");

        // Escape closes the page when nothing is being captured. This is the
        // last step: it unwinds the modal loop, so Main checks the result.
        expectClose = true;
        Key(dialog, Keys.Escape);
    }
}
'@ | Set-Content -Path $probe -Encoding UTF8

    & $csc /nologo /target:exe "/out:$($work)\RebindProbe.exe" $probe `
        /reference:System.Windows.Forms.dll /reference:System.Drawing.dll 2>&1 |
        Where-Object { $_ -match ': error ' } | Select-Object -First 5
    if (-not (Test-Path "$work\RebindProbe.exe")) { throw "the probe did not compile." }

    $fixture = Join-Path $work "install"
    New-Item -ItemType Directory -Force -Path `
        (Join-Path $fixture "work-remix\build-windows-x86_64\dist"), (Join-Path $fixture "game-remix") |
        Out-Null

    $proc = Start-Process -FilePath "$work\RebindProbe.exe" `
        -ArgumentList @($launcher, $fixture, $work) -PassThru -WindowStyle Hidden
    if (-not $proc.WaitForExit(60000)) {
        $proc.Kill()
        Get-Content (Join-Path $work "rebind.log") -ErrorAction SilentlyContinue
        throw "the rebind probe hung; the log above names the last step."
    }

    $log = Get-Content (Join-Path $work "rebind.log")
    $log | ForEach-Object { $_ }
    $fails = ($log | Where-Object { $_ -like "FAIL:*" }).Count
    if ($proc.ExitCode -ne 0 -or $fails -gt 0) { throw "$fails rebind check(s) failed." }
    "Test-Keyboard-Rebind: PASS"
}
finally {
    Remove-Item -Recurse -Force $work -ErrorAction SilentlyContinue
}
