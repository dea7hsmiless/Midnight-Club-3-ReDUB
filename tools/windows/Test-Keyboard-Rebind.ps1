# Does the keyboard rebind dialog behave like a modern game's?
#
#   powershell -File tools/windows/Test-Keyboard-Rebind.ps1
#
# Click the key you want to change, the field says "Press a key...", the next
# key you press becomes it. Escape cancels, Clear unbinds, Reset restores the
# defaults, and a key already doing something else asks before it is taken --
# because two actions on one key is otherwise silent and unfixable in game.
#
# The dialog is modal (ShowDialog blocks its caller), so the probe cannot drive
# it from outside. A timer is queued on the UI thread instead, which runs inside
# the dialog's own message loop: exactly where a player's click happens. A
# watchdog thread reports and closes any message box that appears, so a prompt
# that should appear is an observable event rather than a hang.
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
    static int boxes;

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
    // watchdog closes it (WM_CLOSE answers No for a Yes/No box) so the run
    // continues; its title is recorded for the assertion.
    static void ExpectBox(string title, int before) {
        for (int i = 0; i < 24 && boxes == before; i++) Thread.Sleep(100);
        Check("a " + title + " prompt appears", boxes > before, boxes - before + " box(es)");
        for (int i = 0; i < 10 && Tail().IndexOf("title=[" + title + "]") < 0; i++) Thread.Sleep(100);
        Check("  the prompt is titled \"" + title + "\"",
              Tail().IndexOf("title=[" + title + "]") >= 0, "log");
    }

    static Control FindByText(Control root, string text) {
        foreach (Control c in root.Controls) {
            if (c.Text == text) return c;
            Control d = FindByText(c, text);
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

    [STAThread]
    static int Main(string[] args) {
        logPath = Path.Combine(args[2], "rebind.log");
        File.WriteAllText(logPath, "");

        uint pid = (uint)System.Diagnostics.Process.GetCurrentProcess().Id;
        var seen = new HashSet<IntPtr>();
        var watchdog = new Thread(() => {
            for (int i = 0; i < 200; i++) {
                Thread.Sleep(150);
                foreach (IntPtr h in WindowsOfClass(pid, "#32770")) {
                    if (!seen.Add(h)) continue;
                    var body = new StringBuilder();
                    IntPtr answer = IntPtr.Zero;
                    EnumChildWindows(h, (c, p) => {
                        var cls = new StringBuilder(64); GetClassName(c, cls, 64);
                        string text = TextOf(c);
                        if (cls.ToString() == "Button") {
                            // Answer No / OK: the declining answer, so an
                            // accidental prompt cannot change a setting.
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
                    lock (gate) boxes++;
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
        // the "Install the game before saving settings" guard. A scratch install
        // needs one to write into.
        if (!File.Exists(envPath)) File.WriteAllText(envPath, "# scratch settings\r\n");

        var t = new System.Windows.Forms.Timer { Interval = 400 };
        t.Tick += (s, e) => {
            Form dialog = null;
            foreach (Form f in Application.OpenForms)
                if (f != app && f.Text == "Keyboard (player 1)") dialog = f;
            if (dialog == null) return;
            t.Stop();
            try { Run(app, dialog, envPath); }
            catch (Exception ex) { Log("EXCEPTION: " + ex.ToString().Split('\n')[0]); fails++; }
            Environment.Exit(fails == 0 ? 0 : 1);
        };
        t.Start();

        type.GetMethod("ShowKeyBindings", BindingFlags.NonPublic | BindingFlags.Instance)
            .Invoke(app, null);
        Log("FAIL: the dialog closed without the probe running");
        return 1;
    }

    static void Run(Form app, Form dialog, string envPath) {
        var fields = Fields(dialog);
        Check("every action has a field", fields.Count == 27, fields.Count + " buttons");
        int labelsBefore = CountLabels(dialog);

        var target = fields[8];                       // A, default "A"
        Check("a field shows its current key", target.Text == "A", target.Text);

        Click(target);
        Check("clicking a field asks for a key", target.Text.StartsWith("Press a key"), target.Text);

        Key(dialog, Keys.P);
        Check("the pressed key becomes the binding", target.Text == "P", target.Text);
        Check("the binding is written for the game", Wrote(Env(envPath), "MC3_KEY_A=P"), "MC3_KEY_A=P");
        Check("no duplicated labels after a rebind", CountLabels(dialog) == labelsBefore,
              labelsBefore + " -> " + CountLabels(dialog));

        Click(target);
        Key(dialog, Keys.Escape);
        Check("Escape cancels and keeps the key", target.Text == "P", target.Text);

        // A key that already does something else must be named, not silently
        // taken: two actions on one key cannot be untangled in game.
        var second = fields[9];                       // B
        int before = boxes;
        Click(second);
        Key(dialog, Keys.P);
        ExpectBox("Key already used", before);
        Check("declining leaves the second action unbound", !Wrote(Env(envPath), "MC3_KEY_B=P"), "MC3_KEY_B not written");
        Check("declining leaves the first binding alone", target.Text == "P", target.Text);
        // Declining keeps the field waiting, so the player can simply press
        // another key instead of starting over.
        int before2 = boxes;
        Key(dialog, Keys.H);
        Check("a different key is taken straight away", second.Text == "H", second.Text);
        Check("  and it does not ask again", boxes == before2, boxes - before2 + " box(es)");

        Click(target);
        var clear = FindByText(dialog, "Clear");
        Check("a Clear button exists", clear != null, clear == null ? "missing" : "present");
        Click(clear);
        Check("Clear unbinds the action", target.Text == "not set", target.Text);
        Check("Clear writes -1, which the game reads as unbound", Wrote(Env(envPath), "MC3_KEY_A=-1"), "-1");

        before = boxes;
        Click(clear);
        ExpectBox("Keyboard", before);
        Check("Clear with nothing selected explains itself", true, "prompt shown");

        var reset = FindByText(dialog, "Reset to defaults");
        Check("a Reset button exists", reset != null, reset == null ? "missing" : "present");
        if (reset != null) {
            before = boxes;
            Click(reset);
            ExpectBox("Reset keyboard", before);
            // WM_CLOSE answers No, so the defaults must NOT have been applied yet.
            Check("Reset asks first", target.Text == "not set", target.Text);
        }

        Log(fails == 0 ? "ALL PASS" : fails + " FAILED");
        dialog.Close();
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
