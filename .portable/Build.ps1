param(
    [ValidateSet('Build','Rebuild','Import','Doctor')][string]$Mode = 'Build',
    [string]$Disc,
    [string]$Msys2Root = $env:MSYS2_ROOT,
    [switch]$NoInstall
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$work = Join-Path $repo 'work-remix'
$dist = Join-Path $work 'build-windows-x86_64\dist'
if (!$Msys2Root) { $Msys2Root = 'C:\msys64' }
$bash = Join-Path $Msys2Root 'usr\bin\bash.exe'
$game = Join-Path $repo 'game-remix'

function Select-DiscPath {
    Add-Type -AssemblyName System.Windows.Forms
    $picker = New-Object System.Windows.Forms.OpenFileDialog
    $picker.Title = 'Choose your Xbox Midnight Club 3 Remix disc ISO'
    $picker.Filter = 'Xbox disc image (*.iso;*.xiso)|*.iso;*.xiso|All files (*.*)|*.*'
    try { if ($picker.ShowDialog() -eq 'OK') { return $picker.FileName } } finally { $picker.Dispose() }
    return $null
}

function Copy-GameFolder([string]$Source) {
    if (!(Test-Path -LiteralPath (Join-Path $Source 'default.xbe'))) { throw 'The extracted Xbox game folder must contain default.xbe.' }
    New-Item -ItemType Directory -Path $game -Force | Out-Null
    & robocopy.exe $Source $game /E /COPY:DAT /R:1 /W:1 /NFL /NDL /NJH /NJS
    if ($LASTEXITCODE -ge 8) { throw 'Could not copy the extracted disc files.' }
}

function Run-Bash([string]$Command) {
    & $bash -lc $Command
    if ($LASTEXITCODE -ne 0) { throw "Build step failed (exit $LASTEXITCODE). See work-remix\logs\easy-build.log." }
}

try {
    Set-Location -LiteralPath $repo
    if ($Mode -eq 'Doctor') {
        $ok = $true
        foreach ($check in @(
            @('Xbox Remix disc', (Join-Path $game 'default.xbe')),
            @('Installed game', (Join-Path $dist 'mcdub3remix.exe')),
            @('Controller runtime', (Join-Path $dist 'SDL2.dll')),
            @('Saved settings', (Join-Path $dist 'mcdub3remix_env.txt'))
        )) {
            $found = Test-Path -LiteralPath $check[1]
            Write-Host (('{0}: {1}' -f $check[0], $(if ($found) { 'OK' } else { 'missing' })))
            if (!$found) { $ok = $false }
        }
        if (Test-Path -LiteralPath $bash) { Write-Host 'MSYS2 build tools: OK (Advanced - Rebuild from source is available)' }
        else { Write-Host 'MSYS2 build tools: not installed (only needed for Advanced - Rebuild from source)' }
        if (!$ok) { exit 1 }; exit 0
    }
    if ($Mode -eq 'Import') {
        if ($Disc) {
            $source = Get-Item -LiteralPath $Disc
            if ($source.PSIsContainer) { Copy-GameFolder $source.FullName }
            else { throw 'ISO import is handled by ReDUB Launcher.exe --import <disc.iso>; it needs no compiler.' }
        }
        elseif (!(Test-Path -LiteralPath (Join-Path $game 'default.xbe'))) {
            throw 'No folder selected. Use -Disc <extracted-folder>, or use ReDUB Launcher.exe --import for an ISO.'
        }
        if (!(Test-Path -LiteralPath (Join-Path $game 'default.xbe'))) { throw 'The game folder does not contain default.xbe.' }
        Write-Host "Game files are ready in $game. Nothing was compiled."
        exit 0
    }
    . (Join-Path $PSScriptRoot 'Build-Path.ps1')
    $buildRoot=Get-ReDUBBuildRoot $repo
    Set-Location -LiteralPath $buildRoot
    New-Item -ItemType Directory -Path (Join-Path $work 'logs') -Force | Out-Null

    if (!(Test-Path -LiteralPath (Join-Path $game 'default.xbe'))) {
        if (!$Disc) { $Disc = Select-DiscPath }
        if (!$Disc) { throw 'No disc selected. You can also run Build with -Disc followed by the ISO or extracted folder path.' }
        $source = Get-Item -LiteralPath $Disc
        if ($source.PSIsContainer) { Copy-GameFolder $source.FullName }
        else { $env:MC3_SOURCE_ISO = $source.FullName }
    }

    $env:GAME = 'remix'
    $env:CHERE_INVOKING = '1'
    $env:MSYSTEM = if ($env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'CLANGARM64' } else { 'CLANG64' }
    if ($env:MSYSTEM -ne 'CLANG64') { throw 'This portable Remix build is configured for x64 Windows.' }

    if (!(Test-Path -LiteralPath $bash)) {
        if ((Split-Path -Leaf $Msys2Root) -ne 'msys64') { throw 'For automatic setup, MSYS2_ROOT must end with msys64 (for example C:\msys64).' }
        $bootstrap = Join-Path $work 'tools\msys2-base.sfx.exe'
        New-Item -ItemType Directory -Path (Split-Path -Parent $bootstrap) -Force | Out-Null
        Write-Host 'Installing MSYS2 build tools. This is needed only once.'
        Invoke-WebRequest -UseBasicParsing 'https://github.com/msys2/msys2-installer/releases/latest/download/msys2-base-x86_64-latest.sfx.exe' -OutFile $bootstrap
        $parent = Split-Path -Parent ([IO.Path]::GetFullPath($Msys2Root))
        if ($parent.EndsWith('\')) { $parent += '.' }
        & $bootstrap -y "-o$parent"
        if ($LASTEXITCODE -ne 0 -or !(Test-Path -LiteralPath $bash)) { throw 'MSYS2 extraction failed.' }
        # MSYS2's first core update can close its shell; the second pass finishes.
        & $bash -lc 'pacman -Syuu --noconfirm'
        Run-Bash 'pacman -Syuu --noconfirm'
    }

    # Keep the native SDL2 runtime that detects this PC's Xbox Bluetooth pad.
    $runtime = Join-Path $work 'deps\sdl2-native'
    if (!(Test-Path -LiteralPath (Join-Path $runtime 'SDL2.dll'))) {
        New-Item -ItemType Directory -Path $runtime -Force | Out-Null
        $zip = Join-Path $runtime 'SDL2-2.32.10-win32-x64.zip'
        Invoke-WebRequest -UseBasicParsing 'https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10-win32-x64.zip' -OutFile $zip
        Expand-Archive -LiteralPath $zip -DestinationPath $runtime -Force
    }

    # A location-specific build cache avoids CMake's stale absolute paths after moves.
    $sha = [Security.Cryptography.SHA256]::Create()
    try { $suffix = ([BitConverter]::ToString($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($repo.ToLowerInvariant())))).Replace('-','').Substring(0,8) } finally { $sha.Dispose() }
    $cache = Join-Path (Join-Path $buildRoot 'work-remix') "build-windows-local-$suffix"
    $env:BUILD_DIR = $cache.Replace('\','/')
    $env:MC3_BUILD_MODE = $Mode
    $env:SCALE = '2'
    $env:UI_SCALE = '2'
    $env:MC3_NO_DESKTOP_SHORTCUT = '1'
    Write-Host "Building Midnight Club 3 Remix ($Mode). Later builds reuse cached work."
    Run-Bash 'set -o pipefail; bash .portable/build.sh 2>&1 | tee work-remix/logs/easy-build.log'
    $stage = Join-Path $cache 'dist'
    $newExe = Join-Path $stage 'mcdub3remix.exe'
    if (!(Test-Path -LiteralPath $newExe)) { throw 'The build completed without a game executable.' }
    if ($NoInstall) { Write-Host "Build ready at $stage"; exit 0 }
    if (Get-Process mcdub3remix -ErrorAction SilentlyContinue) { throw 'Build succeeded. Close the running game and run Build again to install it. Your current game was left running.' }

    New-Item -ItemType Directory -Path $dist -Force | Out-Null
    $installedExe = Join-Path $dist 'mcdub3remix.exe'
    if (Test-Path -LiteralPath $installedExe) {
        $backup = Join-Path $work ('build-backups\' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
        New-Item -ItemType Directory -Path $backup -Force | Out-Null
        Copy-Item -LiteralPath $installedExe -Destination $backup
        $settings = Join-Path $dist 'mcdub3remix_env.txt'
        if (Test-Path -LiteralPath $settings) { Copy-Item -LiteralPath $settings -Destination $backup }
    }
    foreach ($file in Get-ChildItem -LiteralPath $stage -File) {
        if ($file.Name -eq 'mcdub3remix.exe') { continue }
        $target = Join-Path $dist $file.Name
        if ($file.Name -in @('mcdub3remix_env.txt','mcdub3remix_defaults.txt') -and (Test-Path -LiteralPath $target)) { continue }
        Copy-Item -LiteralPath $file.FullName -Destination $target -Force
    }
    $logoSource = Join-Path $repo 'hd\textures'
    $logoDest = Join-Path $dist 'hd\textures'
    if (Test-Path -LiteralPath $logoSource) {
        New-Item -ItemType Directory -Path $logoDest -Force | Out-Null
        foreach ($name in @('22762f0e2f14cca4.png','7680ba077867b3e3.png','851e07656bc1c45f.png')) {
            $logo = Join-Path $logoSource $name
            if (Test-Path -LiteralPath $logo) { Copy-Item -LiteralPath $logo -Destination (Join-Path $logoDest $name) -Force }
        }
    }
    Copy-Item -LiteralPath $newExe -Destination ($installedExe + '.new') -Force
    Move-Item -LiteralPath ($installedExe + '.new') -Destination $installedExe -Force

    if (Test-Path -LiteralPath (Join-Path $repo 'ReDUB Launcher.exe')) {
        & (Join-Path $PSScriptRoot 'Build-Launcher.ps1') -ShortcutsOnly
    }
    Write-Host "Ready. Open ReDUB Launcher.exe and choose Play." -ForegroundColor Green
    Write-Host 'Your game files, saves and existing settings were preserved.'
} catch { Write-Host $_.Exception.Message -ForegroundColor Red; exit 1 }
