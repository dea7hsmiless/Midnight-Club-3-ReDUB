param([string]$ClangPath)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$work=Join-Path $repo ('work-remix\stability-tests-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work -Force | Out-Null
$compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
$test=Join-Path $work 'stability-tests.exe'
& $compiler /nologo /target:exe /platform:x64 /codepage:65001 /main:StabilityTests /reference:System.Windows.Forms.dll /reference:System.Drawing.dll /reference:System.IO.Compression.dll /reference:System.IO.Compression.FileSystem.dll "/out:$test" (Join-Path $repo '.portable\Launcher.cs') (Join-Path $PSScriptRoot 'test_stability.cs')
if($LASTEXITCODE -ne 0){throw 'Stability tests did not compile.'}
& $test (Join-Path $work 'fixture')
if($LASTEXITCODE -ne 0){throw 'Stability tests failed.'}
if($ClangPath){
    $native=Join-Path $work 'test_paths.exe'
    & $ClangPath -std=c11 -I (Join-Path $repo 'src') -I (Join-Path $repo 'xboxrecomp\src') -I (Join-Path $repo 'config\remix') '-DMC3_APP=\"mcdub3remix\"' '-DMC3_DISC_PREFIX=\"game-remix\"' (Join-Path $repo 'src\windows_host.c') (Join-Path $repo 'xboxrecomp\src\kernel\kernel_path.c') (Join-Path $PSScriptRoot 'test_paths.c') -o $native -lshell32
    if($LASTEXITCODE -ne 0){throw 'Unicode path tests did not compile.'}
    # Generate non-ASCII names without depending on PowerShell's script encoding.
    $unicode='Jos'+[char]0xE9+'-'+[char]0x5DE5+[char]0x5177
    $install=Join-Path $work $unicode
    $dist=Join-Path $install 'work-remix\build-windows-x86_64\dist'
    $game=Join-Path $install 'game-remix';$save=Join-Path $install 'work-remix\save'
    New-Item -ItemType Directory -Force $dist,$game,$save | Out-Null
    [IO.File]::WriteAllText((Join-Path $game 'default.xbe'),'fixture')
    $moved=Join-Path $dist 'test_paths.exe';Copy-Item -LiteralPath $native -Destination $moved
    $oldGame=$env:MC3_GAME_DIR;$oldSave=$env:MC3_SAVE_DIR
    try{
        $env:MC3_GAME_DIR=$null;$env:MC3_SAVE_DIR=$null
        & $moved
        if($LASTEXITCODE -ne 0){throw 'Unicode automatic discovery failed.'}
        $env:MC3_GAME_DIR=$game;$env:MC3_SAVE_DIR=$save
        & $moved
        if($LASTEXITCODE -ne 0){throw 'Unicode environment paths failed.'}
    }finally{$env:MC3_GAME_DIR=$oldGame;$env:MC3_SAVE_DIR=$oldSave}
}
Write-Host "Fixtures retained in $work"
