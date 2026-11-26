# Does a crash leave something the launcher can find and package?
#
#   powershell -File tools/windows/Test-CrashReport.ps1
#
# The failure this guards: the game writes crash-<time>.txt itself, but the
# launcher only looked for its own exit-code stamp (written when *it* started
# the game), and its report zip globbed "*.log*" -- so neither the crash report
# nor build.txt was included, and a crash after starting the exe directly was
# invisible. Both are asserted here.
#
# Exit 0 = all pass.
param(
    [string]$Root = (Split-Path -Parent (Split-Path -Parent $PSScriptRoot))
)

$ErrorActionPreference = "Stop"
$csc = Join-Path $env:WINDIR "Microsoft.NET\Framework64\v4.0.30319\csc.exe"
if (-not (Test-Path $csc)) { throw "csc.exe not found; this test needs the .NET Framework compiler." }
$work = Join-Path ([IO.Path]::GetTempPath()) ("redub-crash-" + [guid]::NewGuid().ToString("N").Substring(0, 8))
New-Item -ItemType Directory -Force -Path $work | Out-Null

try {
    $launcher = Join-Path $work "launcher.dll"
    & $csc /nologo /target:library /platform:x64 /codepage:65001 "/out:$launcher" `
        (Join-Path $Root ".portable\Launcher.cs") (Join-Path $Root ".portable\Xdvdfs.cs") `
        /reference:System.Windows.Forms.dll /reference:System.Drawing.dll `
        /reference:System.IO.Compression.dll /reference:System.IO.Compression.FileSystem.dll 2>&1 |
        Where-Object { $_ -match ': error ' } | Select-Object -First 5
    if (-not (Test-Path $launcher)) { throw "the launcher did not compile." }

    $probe = Join-Path $work "CrashProbe.cs"
    @'
using System;
using System.IO;
using System.IO.Compression;
using System.Reflection;

public class CrashProbe {
    static int fails;
    static void Check(string what, bool ok, string detail) {
        Console.WriteLine((ok ? "PASS: " : "FAIL: ") + what + "  [" + detail + "]");
        if (!ok) fails++;
    }

    static void Clear(string logs) {
        foreach (string f in Directory.GetFiles(logs)) {
            string n = Path.GetFileName(f);
            if (n.StartsWith("crash-") || n == "last-exit.txt" || n.EndsWith(".log") || n == "build.txt")
                File.Delete(f);
        }
    }

    public static int Main(string[] args) {
        Assembly asm = Assembly.LoadFrom(args[0]);
        Type type = asm.GetType("ReDUB.Launcher", true);
        Type[] none = new Type[0];
        string root = args[1];
        string logs = Path.Combine(root, @"work-remix\logs");
        Directory.CreateDirectory(logs);

        MethodInfo last = type.GetMethod("LastCrash", BindingFlags.Static | BindingFlags.NonPublic);
        MethodInfo zip = type.GetMethod("MakeCrashZip", BindingFlags.Static | BindingFlags.NonPublic);

        // A clean folder reports nothing.
        Clear(logs);
        Check("a clean folder reports no crash", last.Invoke(null, new object[] { root }) == null,
              "LastCrash");

        // A crash the launcher did not see: the game started directly, or the
        // launcher closed while it ran. Only the game's own report exists.
        File.WriteAllText(Path.Combine(logs, "crash-20260101-120000.txt"), "ReDUB crash report\n");
        object found = last.Invoke(null, new object[] { root });
        Check("a crash report the launcher did not observe is found",
              found != null && found.ToString().Contains("crash-20260101-120000.txt"),
              found == null ? "nothing" : found.ToString());

        // Its own exit stamp still works, and takes precedence.
        File.WriteAllText(Path.Combine(logs, "last-exit.txt"), "-1073741819\t2026-01-01T12:00:00Z");
        object stamped = last.Invoke(null, new object[] { root });
        Check("an observed exit code is still reported",
              stamped != null && stamped.ToString().Contains("-1073741819"),
              stamped == null ? "nothing" : stamped.ToString());
        File.Delete(Path.Combine(logs, "last-exit.txt"));

        // The zip must carry the evidence, not only the log.
        File.WriteAllText(Path.Combine(logs, "latest.log"), "run log\n");
        File.WriteAllText(Path.Combine(logs, "crash-20260101-120000.txt"), "ReDUB crash report\n");
        File.WriteAllText(Path.Combine(logs, "build.txt"), "version = test\n");
        object zp = zip.Invoke(null, new object[] { root });
        Check("the report zip is written", zp != null, zp == null ? "null" : zp.ToString());
        if (zp != null) {
            var names = new System.Collections.Generic.List<string>();
            using (ZipArchive z = ZipFile.OpenRead(zp.ToString()))
                foreach (ZipArchiveEntry e in z.Entries) names.Add(e.FullName);
            Check("the zip carries the run log", names.Contains("latest.log"), string.Join(",", names));
            Check("the zip carries the game's crash report", names.Contains("crash-20260101-120000.txt"), string.Join(",", names));
            Check("the zip carries build.txt", names.Contains("build.txt"), string.Join(",", names));
        }

        Console.WriteLine(fails == 0 ? "ALL PASS" : fails + " FAILED");
        return fails == 0 ? 0 : 1;
    }
}
'@ | Set-Content $probe -Encoding UTF8

    $exe = Join-Path $work "CrashProbe.exe"
    & $csc /nologo /target:exe "/out:$exe" $probe /reference:System.Windows.Forms.dll `
        /reference:System.IO.Compression.dll /reference:System.IO.Compression.FileSystem.dll 2>&1 |
        Where-Object { $_ -match ': error ' } | Select-Object -First 5
    if (-not (Test-Path $exe)) { throw "the crash probe did not compile." }

    $fixture = Join-Path $work "fixture"
    New-Item -ItemType Directory -Force -Path $fixture | Out-Null
    & $exe $launcher $fixture
    if ($LASTEXITCODE -ne 0) { throw "Crash report tests failed." }
    exit 0
}
finally {
    Remove-Item $work -Recurse -Force -ErrorAction SilentlyContinue
}
