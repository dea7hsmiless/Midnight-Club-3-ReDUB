# Release stability pass: idle in the front end, memory/handle growth, exit, event log.
#
#   powershell -ExecutionPolicy Bypass -File tools/windows/Test-Release-Stability.ps1 `
#       -Minutes 6 -GameDir 'C:\Games\ReDUB\game-remix'
#
# Pass criteria:
#   - stays Responding for the whole run;
#   - private bytes stop growing in the second half (the main menu loads the world
#     in the background: a one-shot burst after the intro, so the verdict comes
#     from steady state, not from the run as a whole);
#   - handles grow < 150;
#   - the window closes and the process exits well inside the watchdog's 6 s;
#   - no new Application event id 1000/1001/1002 for this process.
#
# The run happens in a copy under %TEMP%, never in the build's dist: a test writes
# <app>_env.txt, and a package must never carry one. The copy also avoids the
# loader trap that costs 20 minutes every time - an exe run without its DLLs dies
# before printing a banner, with no log and no crash event (2026-11-24).
param(
    [int]$Minutes = 6,
    [string]$Dist,
    [string]$GameDir = $env:MC3_GAME_DIR,
    [string]$App = 'mcdub3remix',
    [int]$Online = 1
)
$ErrorActionPreference = 'Continue'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
if (-not $Dist) { $Dist = Join-Path $repo 'work-remix\build-windows-x86_64\dist' }
if (-not $GameDir) { throw 'Pass -GameDir or set MC3_GAME_DIR to the extracted disc folder.' }
$srcExe = Join-Path $Dist "$App.exe"
if (-not (Test-Path $srcExe)) { throw "No $App.exe in $Dist. Build it first." }

# Fresh copy keyed by the exe's hash, so a rebuilt exe is never tested stale.
$hash = (Get-FileHash $srcExe -Algorithm SHA256).Hash.Substring(0, 12)
$testDir = Join-Path $env:TEMP "redub-stability-$hash"
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
Set-Content -Path (Join-Path $testDir "$App`_env.txt") -Value $envLines -Encoding ascii
$log = Join-Path $env:TEMP "redub-stability-$hash.log"
Remove-Item $log -ErrorAction SilentlyContinue

$start = Get-Date
$p = Start-Process -FilePath (Join-Path $testDir "$App.exe") -WorkingDirectory $testDir -PassThru `
     -RedirectStandardError $log -RedirectStandardOutput "$log.out"
Start-Sleep -Seconds 70          # boot to the front end

$first = $null; $last = $null; $bad = @(); $samples = @()
while ((Get-Date) -lt $start.AddMinutes($Minutes)) {
    if ($p.HasExited) { $bad += "exited early at $([int]((Get-Date)-$start).TotalSeconds)s"; break }
    $p.Refresh()
    $row = [pscustomobject]@{
        At      = ((Get-Date) - $start).ToString('mm\:ss')
        Respond = $p.Responding
        Private = $p.PrivateMemorySize64
        Handles = $p.HandleCount
    }
    $samples += $row
    if (!$row.Respond) { $bad += "unresponsive at $($row.At)" }
    if ($null -eq $first) { $first = $row } else { $last = $row }
    Start-Sleep -Seconds 15
}

$closeSecs = -1
if (!$p.HasExited) {
    $t = Get-Date
    $p.CloseMainWindow() | Out-Null
    if ($p.WaitForExit(15000)) { $closeSecs = [math]::Round(((Get-Date) - $t).TotalSeconds, 2) }
    else { $bad += 'did not exit within 15 s of close'; $p.Kill() }
}

# Steady state = second half of the run; the world load lands in the first.
$ss = @()
if ($samples.Count -ge 4) { $ss = $samples[([int]($samples.Count / 2))..($samples.Count - 1)] }
$growPct = 0
if ($ss.Count -ge 2 -and $ss[0].Private -gt 0) {
    $growPct = [math]::Round(100.0 * ($ss[-1].Private - $ss[0].Private) / $ss[0].Private, 1)
}
$peakPct = 0
if ($ss.Count -ge 2 -and $ss[0].Private -gt 0) {
    $peak = ($ss | Measure-Object Private -Maximum).Maximum
    $peakPct = [math]::Round(100.0 * ($peak - $ss[0].Private) / $ss[0].Private, 1)
}
$handleGrow = 0
if ($first -and $last) { $handleGrow = $last.Handles - $first.Handles }
if ($growPct -gt 10)  { $bad += "private bytes still growing in steady state: ${growPct}%" }
if ($handleGrow -gt 150) { $bad += "handles grew by $handleGrow" }
if ($closeSecs -ge 0 -and $closeSecs -gt 3) { $bad += "close took ${closeSecs}s (watchdog fires at 6)" }

$events = Get-WinEvent -FilterHashtable @{LogName='Application'; StartTime=$start} -ErrorAction SilentlyContinue |
    Where-Object { $_.Id -in 1000,1001,1002 } |
    ForEach-Object { "$($_.Id) $($_.TimeCreated.ToString('HH:mm:ss'))" }
if ($events) { $bad += "application events: $($events -join '; ')" }

$fatal = @()
if (Test-Path $log) {
    $fatal = Select-String -Path $log -Pattern 'decode abort|bad target|ACCESS_VIOLATION|unhandled|assert' -ErrorAction SilentlyContinue |
             ForEach-Object { $_.Line.Trim() } | Select-Object -First 10
}
if ($fatal) { $bad += "log errors: $($fatal.Count)" }

[pscustomobject]@{
    Exe           = $hash
    Minutes       = $Minutes
    Samples       = $samples.Count
    PrivateStart  = if ($first) { $first.Private } else { 0 }
    PrivateEnd    = if ($last)  { $last.Private }  else { 0 }
    SteadyGrowth  = "$growPct%"
    SteadyPeak    = "$peakPct%"
    HandleStart   = if ($first) { $first.Handles } else { 0 }
    HandleEnd     = if ($last)  { $last.Handles }  else { 0 }
    CloseSeconds  = $closeSecs
    Problems      = if ($bad.Count) { $bad -join ' | ' } else { 'none' }
    Log           = $log
} | Format-List | Out-String | Write-Output

if ($bad.Count) { exit 1 }
