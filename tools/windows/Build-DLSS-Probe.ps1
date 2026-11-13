param([switch]$Run,[switch]$Evaluate,[switch]$Bridge,[string]$Msys2Root='C:\msys64')
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$sdk=Join-Path $repo 'work-remix\deps\nvidia-dlss'
$outDir=Join-Path $repo 'work-remix\dlss-probe'
$finder=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if(!(Test-Path -LiteralPath $finder)){throw 'MSVC x64 Build Tools are required for this experimental NVIDIA SDK probe.'}
$vs=& $finder -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if(!$vs){throw 'Install the Visual Studio C++ x64 Build Tools before building the experimental probe.'}
$msvc=Get-ChildItem -LiteralPath (Join-Path $vs 'VC\Tools\MSVC') -Directory | Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1
$kit=(Get-ItemProperty 'HKLM:\SOFTWARE\Microsoft\Windows Kits\Installed Roots').KitsRoot10
$version=Get-ChildItem -LiteralPath (Join-Path $kit 'Lib') -Directory | Where-Object {$_.Name -match '^10\.'} | Sort-Object {[version]$_.Name} -Descending | Select-Object -First 1 -ExpandProperty Name
if(!$msvc -or !$version){throw 'A complete MSVC toolset and Windows 10/11 SDK are required.'}
$library=Join-Path $sdk 'lib\Windows_x86_64\x64\nvsdk_ngx_d.lib'
$runtime=Join-Path $sdk 'lib\Windows_x86_64\rel\nvngx_dlss.dll'
if(!(Test-Path -LiteralPath $library) -or !(Test-Path -LiteralPath $runtime)){throw 'Download the pinned NVIDIA SDK first: tools\windows\Get-DLSS-SDK.ps1. SDK files remain under ignored work-remix/deps.'}
$oldInclude=$env:INCLUDE;$oldLib=$env:LIB
try {
    $env:INCLUDE="$($msvc.FullName)\include;$kit\Include\$version\ucrt;$kit\Include\$version\shared;$kit\Include\$version\um;$Msys2Root\clang64\include"
    $env:LIB="$($msvc.FullName)\lib\x64;$kit\Lib\$version\ucrt\x64;$kit\Lib\$version\um\x64"
    New-Item -ItemType Directory -Path $outDir -Force | Out-Null
    & (Join-Path $msvc.FullName 'bin\Hostx64\x64\cl.exe') /nologo /EHsc /MD /std:c++17 /O2 /LD "/I$sdk\include" (Join-Path $PSScriptRoot 'dlss_bridge.cpp') "/Fo$outDir\dlss_bridge.obj" "/Fe$outDir\redub_dlss.dll" /link "/implib:$outDir\redub_dlss.lib" $library "$Msys2Root\clang64\lib\libvulkan-1.dll.a" advapi32.lib shell32.lib ole32.lib user32.lib
    if($LASTEXITCODE -ne 0){throw 'Optional DLSS runtime module compilation failed.'}
    & (Join-Path $msvc.FullName 'bin\Hostx64\x64\cl.exe') /nologo /EHsc /MD /std:c++17 /O2 "/I$sdk\include" (Join-Path $PSScriptRoot 'dlss_probe.cpp') "/Fo$outDir\dlss_probe.obj" "/Fe$outDir\dlss-probe.exe" /link $library "$Msys2Root\clang64\lib\libvulkan-1.dll.a" advapi32.lib shell32.lib ole32.lib user32.lib
    if($LASTEXITCODE -ne 0){throw 'Experimental DLSS probe compilation failed.'}
    Copy-Item -LiteralPath $runtime -Destination $outDir -Force
    if($Run -or $Evaluate){
        Push-Location -LiteralPath $outDir
        try {
            $probeArgs=@();if($Evaluate){$probeArgs+='--evaluate'};if($Bridge){$probeArgs+='--bridge'}
            & (Join-Path $outDir 'dlss-probe.exe') @probeArgs 2>&1 | Tee-Object -FilePath (Join-Path $outDir 'probe.log')
            if($LASTEXITCODE -ne 0){throw "DLSS probe failed with exit code $LASTEXITCODE. See work-remix/dlss-probe/probe.log."}
        }finally{Pop-Location}
    }
}finally{$env:INCLUDE=$oldInclude;$env:LIB=$oldLib}
Write-Host 'Experimental probe built. No installed game or settings were changed.'
