# Idle memory curve: boot, sit in the front end, print private bytes and handles
# every few seconds, then close. Use it when a run grows and you need to know
# *when* it grows, not just that it did.
#
#   powershell -ExecutionPolicy Bypass -File tools/windows/Test-Release-Idle.ps1 `
#       -Seconds 420 -GameDir 'C:\Games\ReDUB\game-remix'
#
# The switches below are the ones that convicted two 0.2.5 regressions
# (0.2.5 investigation notes, sections 15.5-15.7; kept locally, not in the
# repository). They exist so the next report can be bisected without rebuilding:
#   -Quiet 0        old per-read logging (the logo freeze came from it)
#   -MovieWaitMs 0  no INFINITE-wait ceiling (the 114 s crash came from it)
#   -Guard 0        no frame-progress guard on the capped wait
#
# The run happens in a copy under %TEMP% keyed by the exe's hash: tests write an
# _env.txt, and a package must never carry one. Copying the DLLs matters - an exe
# without them dies in the loader, silently (2026-11-24).
param(
    [int]$Seconds = 420,
    [int]$SampleSecs = 2,
    [string]$Dist,
    [string]$GameDir = $env:MC3_GAME_DIR,
    [string]$App = 'mcdub3remix',
    [int]$Online = 1,
    [int]$Quiet = -1,
    [int]$Guard = -1,
    [int]$MovieWaitMs = -1
)
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $Dist) { $Dist = Join-Path $repo 'work-remix\build-windows-x86_64\dist' }
if (-not $GameDir) { throw 'Pass -GameDir or set MC3_GAME_DIR to the extracted disc folder.' }
$srcExe = Join-Path $Dist "$App.exe"
if (-not (Test-Path $srcExe)) { throw "No $App.exe in $Dist. Build it first." }

$hash = (Get-FileHash $srcExe -Algorithm SHA256).Hash.Substring(0, 12)
$testDir = Join-Path $env:TEMP "redub-idle-$hash"
if (-not (Test-Path (Join-Path $testDir "$App.exe"))) {
    New-Item -ItemType Directory -Force -Path $testDir | Out-Null
    Copy-Item (Join-Path $Dist '*.dll') $testDir -Force -ErrorAction SilentlyContinue
    Copy-Item $srcExe $testDir -Force
    Get-ChildItem $Dist -File -ErrorAction SilentlyContinue |
        Where-Object { $_.Extension -in '.txt', '.ps1', '.bat' } |
        ForEach-Object { Copy-Item $_.FullName $testDir -Force }
}

$envLines = @(
    "MC3_GAME_DIR=$GameDir",
    'MC3_SKIP_MOVIES=0', 'MC3_FULLSCREEN=0', 'MC3_RESOLUTION=1280x720'
)
if ($Online) {
    $envLines += 'MC3_ONLINE=1', 'MC3_NETPLAY=1',
                 'MC3_ROAM_JOIN=162.35.56.66', 'MC3_ROAM_HUB_PORT=47210', 'MC3_ROAM_ROOM=22'
}
if ($Quiet -ge 0)      { $envLines += "RECOMP_QUIET=$Quiet" }
if ($Guard -ge 0)      { $envLines += "MC3_WAIT_GUARD=$Guard" }
if ($MovieWaitMs -ge 0){ $envLines += "MC3_MOVIE_WAIT_MS=$MovieWaitMs" }
Set-Content -Path (Join-Path $testDir "$App`_env.txt") -Value $envLines -Encoding ascii
$log = Join-Path $env:TEMP "redub-idle-$hash.log"
Remove-Item $log -ErrorAction SilentlyContinue

$t0 = Get-Date
$p = Start-Process -FilePath (Join-Path $testDir "$App.exe") -WorkingDirectory $testDir -PassThru `
     -RedirectStandardError $log -RedirectStandardOutput "$log.out"
"exe $hash   quiet=$Quiet guard=$Guard movieWait=$MovieWaitMs online=$Online"

$first = $null; $lastPriv = 0; $rows = @()
while (((Get-Date) - $t0).TotalSeconds -lt $Seconds) {
    Start-Sleep -Seconds $SampleSecs
    $p.Refresh()
    if ($p.HasExited) {
        "EXITED at $([int]((Get-Date)-$t0).TotalSeconds)s - see $log"
        break
    }
    $priv = $p.PrivateMemorySize64
    $d = if ($lastPriv) { ($priv - $lastPriv) / 1MB } else { 0 }
    $lastPriv = $priv
    if ($null -eq $first) { $first = $priv }
    $row = "{0,5}s {1,8:N1} MB  d={2,6:N1} MB  handles {3}" -f `
        [int]((Get-Date)-$t0).TotalSeconds, ($priv / 1MB), $d, $p.HandleCount
    $rows += $row
    $row
}

$closeSecs = -1
if (!$p.HasExited) {
    $t = Get-Date
    $p.CloseMainWindow() | Out-Null
    if ($p.WaitForExit(15000)) { $closeSecs = [math]::Round(((Get-Date) - $t).TotalSeconds, 2) }
    else { "did not close in 15 s"; $p.Kill() }
}

if ($null -ne $first) {
    "boot {0:N0} MB -> end {1:N0} MB; close {2}s; log {3}" -f ($first / 1MB), ($p.PrivateMemorySize64 / 1MB), $closeSecs, $log
    # The plateau, not the total: the menu loads the world in one burst.
    $nums = $rows | ForEach-Object { [double]($_ -replace '^\s*\d+s\s+([\d,\.]+) MB.*$', '$1' -replace ',', '') }
    if ($nums.Count -ge 6) {
        $tail = $nums[([int]($nums.Count / 2))..($nums.Count - 1)]
        $peak = ($tail | Measure-Object -Maximum).Maximum
        $end = $tail[-1]
        "second half peak {0:N1} MB -> end {1:N1} MB ({2:N1}% off peak)" -f $peak, $end, (100.0 * ($end - $peak) / $peak)
    }
}
