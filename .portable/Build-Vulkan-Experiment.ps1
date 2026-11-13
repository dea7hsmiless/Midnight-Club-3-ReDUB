param([string]$Msys2Root='C:\msys64',[switch]$WithDLSS)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$bash=Join-Path $Msys2Root 'usr\bin\bash.exe'
if(!(Test-Path -LiteralPath $bash)){throw 'Run normal ReDUB setup first. This experiment requires the existing MSYS2 tools.'}
. (Join-Path $PSScriptRoot 'Build-Path.ps1')
$buildRoot=Get-ReDUBBuildRoot $repo
if(!(Test-Path -LiteralPath (Join-Path $repo 'work-remix\gen\recomp_funcs.h'))){throw 'Run the normal build first to generate code from your own Xbox Remix disc.'}
if($WithDLSS){
    & (Join-Path $PSScriptRoot '..\tools\windows\Get-DLSS-SDK.ps1')
    if(!$?){throw 'Pinned NVIDIA DLSS SDK setup failed.'}
    & (Join-Path $PSScriptRoot '..\tools\windows\Build-DLSS-Probe.ps1') -Msys2Root $Msys2Root
    if(!$?){throw 'DLSS bridge build failed. The installed OpenGL game remains unchanged.'}
}
$previousSystem=$env:MSYSTEM;$previousChere=$env:CHERE_INVOKING
Push-Location -LiteralPath $buildRoot
try {
    $env:MSYSTEM='CLANG64';$env:CHERE_INVOKING='1'
    New-Item -ItemType Directory -Path work-remix/logs -Force | Out-Null
    & $bash -lc 'bash .portable/build-vulkan-experiment.sh'
    if($LASTEXITCODE -ne 0){throw 'Experimental Vulkan build failed. The installed OpenGL game was not replaced.'}
    $dist=Join-Path $repo 'work-remix\build-windows-vulkan-experimental\dist'
    New-Item -ItemType Directory -Path $dist -Force | Out-Null
    Copy-Item -LiteralPath work-remix/build-windows-vulkan-experimental/mcdub3remix.exe -Destination $dist -Force
    & $bash -lc 'python3 tools/windows/copy_dlls.py work-remix/build-windows-vulkan-experimental/dist/mcdub3remix.exe /clang64/bin work-remix/build-windows-vulkan-experimental/dist'
    if($LASTEXITCODE -ne 0){throw 'Experimental runtime dependency collection failed.'}
    Copy-Item -LiteralPath work-remix/deps/sdl2-native/SDL2.dll -Destination (Join-Path $dist 'SDL2.dll') -Force
    Copy-Item -LiteralPath (Join-Path $repo 'tools\windows\now_playing.ps1') -Destination (Join-Path $dist 'now_playing.ps1') -Force
    foreach($optional in @('redub_dlss.dll','nvngx_dlss.dll')){
        $module=Join-Path $repo "work-remix\dlss-probe\$optional"
        if(Test-Path -LiteralPath $module){Copy-Item -LiteralPath $module -Destination $dist -Force}
    }
    Write-Host "Experimental Vulkan build staged separately. DLSS module included: $WithDLSS. DLSS remains off unless selected at launch."
    Write-Host 'The normal launcher, installed OpenGL game, shortcuts and settings were not replaced.'
}finally{Pop-Location;$env:MSYSTEM=$previousSystem;$env:CHERE_INVOKING=$previousChere}
