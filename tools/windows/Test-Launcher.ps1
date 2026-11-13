param([string]$LauncherPath)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$source=if($LauncherPath){[IO.Path]::GetFullPath($LauncherPath)}else{Join-Path $repo 'ReDUB Launcher.exe'}
if(!(Test-Path -LiteralPath $source)){throw 'Build the embedded launcher first.'}
$fixture=Join-Path $repo ('work-remix\launcher-move-test-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $fixture -Force | Out-Null
Copy-Item -LiteralPath $source -Destination $fixture
$test=Join-Path $fixture 'launcher-tests.exe'
$compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
& $compiler /nologo /target:exe /platform:x64 /reference:System.Windows.Forms.dll /reference:System.Drawing.dll /reference:System.IO.Compression.dll /reference:System.IO.Compression.FileSystem.dll "/reference:$source" "/out:$test" (Join-Path $PSScriptRoot 'test_launcher.cs')
if($LASTEXITCODE -ne 0){throw 'Launcher regression tests did not compile.'}
& $test $repo
if($LASTEXITCODE -ne 0){throw 'Launcher regression tests failed.'}
