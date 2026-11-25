# Windows boot soak: N boots to the cruise and out, with the online path on.
#
#   powershell -ExecutionPolicy Bypass -File tools/soak-windows.ps1 -Runs 4
#
# Each run is a real boot (autostart drives the menus to Cruise). The watchdog
# ends it, so a run that does not reach the cruise is a result, not a timeout.
#
# Flags are matched case-sensitively and with word boundaries on purpose: the
# first version of this script used PowerShell's case-insensitive -match for
# 'HANG' and flagged all four runs, because "[ROAM] the city changed at the
# load" contains "hang" inside "changed". A test that cries wolf is worse than
# no test.
param(
    [int]$Runs = 4,
    [int]$WatchdogSecs = 150,
    [string]$Dist = 'C:\Users\Donte\Desktop\ReDUB-0.2.4-build-kit\source\work-remix\build-windows-x86_64\dist',
    [string]$Disc = 'C:\Users\Donte\Desktop\ReDUB-Archive\ReDUB-0.2.4a-release\ReDUB-0.2.4-Windows-x86_64\game-remix',
    [string]$Join = '162.35.56.66',
    [string]$SkipMovies = '0'   # 1 = the broken skip path, for reproducing it
)

$ErrorActionPreference = 'Stop'
$env:MC3_GAME_DIR = $Disc
$env:MC3_ROAM_JOIN = $Join
$env:MC3_ROAM_NAME = 'Soak'
$env:MC3_NETPLAY = '1'
$env:MC3_AUTOSTART = 'cruise'
# Play the movies: that is what ships. Skipping them is not a shortcut - with
# MC3_SKIP_MOVIES=1 the title stalls inside the mc3intro.bik open (two of three
# runs sat there until the watchdog, 2026-11-24), so a skipped-movie soak
# measures a broken path, not the release. -SkipMovies 1 reproduces that.
$env:MC3_SKIP_MOVIES = $SkipMovies
$env:RECOMP_WATCHDOG_SECS = "$WatchdogSecs"

Push-Location $Dist
Remove-Item mcdub3remix_env.txt -Force -ErrorAction SilentlyContinue
$rows = @()
for ($i = 1; $i -le $Runs; $i++) {
    $o = "$env:TEMP\opencode\soak-$i.out.txt"
    $e = "$env:TEMP\opencode\soak-$i.err.txt"
    $t0 = Get-Date
    $p = Start-Process -FilePath '.\mcdub3remix.exe' -WorkingDirectory $Dist -PassThru `
         -RedirectStandardOutput $o -RedirectStandardError $e
    $p.WaitForExit(($WatchdogSecs + 120) * 1000) | Out-Null
    if (!$p.HasExited) { $p.Kill() }
    $ran  = [int]((Get-Date) - $t0).TotalSeconds
    $code = if ($p.HasExited) { $p.ExitCode } else { 'killed' }
    $txt  = ((Get-Content $o -ErrorAction SilentlyContinue) +
             (Get-Content $e -ErrorAction SilentlyContinue)) -join "`n"

    $cruise = 'MISSED'
    $m = [regex]::Match($txt, 'AUTOSTART\]\s+([0-9.]+)s in Cruise')
    if ($m.Success) { $cruise = "$($m.Groups[1].Value)s" }

    $flags = @()
    if ($txt -cmatch '\bHANG\b' -or $txt -cmatch 'deadlock' -or $txt -cmatch 'no new frame') { $flags += 'hang' }
    if ($txt -cmatch 'pb_drop_batch|s_get=at') { $flags += 'resync' }
    if ($txt -cmatch 'ACCESS VIOLATION|unhandled exception|crash') { $flags += 'crash' }
    # 'killed' is the harness ending a run that reached Cruise: the game has no
    # quit of its own, and RECOMP_WATCHDOG_SECS is only a dead-man switch (its
    # stack dump can itself stall, so the script's own kill is the normal end).
    if ($code -ne 3 -and $code -ne 0 -and $code -ne 'killed' -and "$code" -ne '') { $flags += "exit $code" }

    $roam = if ($txt -cmatch '\[ROAM\] transport up as') { 'up' } else { 'NO' }
    $xnet = if ($txt -cmatch 'HLE up: transport relay') { 'relay' }
            elseif ($txt -cmatch 'HLE up: transport udp') { 'udp' } else { '-' }

    $rows += [pscustomobject]@{
        Run = $i; Secs = $ran; Cruise = $cruise; Roam = $roam; XNet = $xnet
        Exit = $code; Flags = ($flags -join ' ')
    }
    Write-Host ("run {0}: {1}s  cruise {2}  roam {3}  xnet {4}  exit {5}  {6}" -f $i, $ran, $cruise, $roam, $xnet, $code, ($flags -join ' '))
}
Pop-Location
Write-Host ''
$rows | Format-Table -AutoSize
$bad = @($rows | Where-Object { $_.Flags -ne '' -or $_.Cruise -eq 'MISSED' -or $_.Roam -eq 'NO' })
if ($bad.Count) { Write-Host "$($bad.Count) run(s) need a look"; exit 1 }
Write-Host "all $Runs runs clean"
