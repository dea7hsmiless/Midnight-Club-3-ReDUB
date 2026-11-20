param([switch]$CheckOnly)
$ErrorActionPreference = 'Stop'
try {
    $repo = Split-Path -Parent $PSScriptRoot
    $preferences=Join-Path $repo 'launcher\preferences.ini'
    $dist = Join-Path $repo 'work-remix\build-windows-x86_64\dist'
    $exe = Join-Path $dist 'mcdub3remix.exe'
    foreach ($file in @($exe, (Join-Path $dist 'SDL2.dll'), (Join-Path $repo 'game-remix\default.xbe'))) {
        if (!(Test-Path -LiteralPath $file)) { throw "Missing $file. Open ReDUB Launcher.exe and " }
    }
    if ($CheckOnly) { Write-Host 'Installed game, controller runtime and disc files found.'; exit 0 }
    if (Get-Process mcdub3remix -ErrorAction SilentlyContinue) { Write-Host 'Midnight Club 3 Remix is already running.'; exit 0 }
    # The game is interactive; its own startup handles logging and settings.
    Start-Process -FilePath $exe -WorkingDirectory $dist -WindowStyle Normal
} catch { Write-Host $_.Exception.Message -ForegroundColor Red; exit 1 }
