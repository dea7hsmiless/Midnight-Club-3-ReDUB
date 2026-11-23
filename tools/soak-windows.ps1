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
    [string]$Join = '162.35.56.66'
)

$ErrorActionPreference = 'Stop'
$env:MC3_GAME_DIR = $Disc
$env:MC3_ROAM_JOIN = $Join
$env:MC3_ROAM_NAME = 'Soak'
$env:MC3_NETPLAY = '1'
$env:MC3_AUTOSTART = 'cruise'
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
    if ($code -ne 3 -and $code -ne 0) { $flags += "exit $code" }

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
