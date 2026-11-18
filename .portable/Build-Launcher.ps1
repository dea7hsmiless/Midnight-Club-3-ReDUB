param([switch]$ShortcutsOnly)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$exe=Join-Path $repo 'ReDUB Launcher.exe'
$candidateDir=Join-Path $repo '.redub-launcher-build'
$candidate=Join-Path $candidateDir 'ReDUB Launcher.exe'
$icon=Join-Path $repo 'launcher\art\redub.ico'
$manifest=Join-Path $PSScriptRoot 'Launcher.manifest'
if(!(Test-Path -LiteralPath $icon)){& (Join-Path $PSScriptRoot 'Build-Icon.ps1')}
if(!$ShortcutsOnly){
 & (Join-Path $PSScriptRoot 'Bundle-Launcher-Source.ps1')
 $compiler=Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
 if(!(Test-Path -LiteralPath $compiler)){throw 'The Windows .NET Framework C# compiler is missing.'}
 $resources=@()
 foreach($name in @('logo.png','hero.jpg','hero.png')){$art=Join-Path $repo "launcher\art\$name";if(Test-Path -LiteralPath $art){$resources+="/resource:$art,ReDUB.Art.$name"}}
 New-Item -ItemType Directory -Path $candidateDir -Force | Out-Null
 if(Test-Path -LiteralPath $candidate){Remove-Item -LiteralPath $candidate -Force}
 & $compiler /nologo /target:winexe /platform:x64 /optimize+ /debug- /win32manifest:$manifest /reference:System.Windows.Forms.dll /reference:System.Drawing.dll /reference:System.IO.Compression.dll /reference:System.IO.Compression.FileSystem.dll "/win32icon:$icon" @resources "/out:$candidate" (Join-Path $PSScriptRoot 'Launcher.cs')
 if($LASTEXITCODE -ne 0){throw 'Launcher compilation failed.'}
 try {Move-Item -LiteralPath $candidate -Destination $exe -Force}
 catch {throw "Launcher compiled successfully but Windows has the current launcher locked. Close it and rerun Build-Launcher.ps1; the new build is preserved at $candidate."}
 if(Test-Path -LiteralPath $candidateDir){Remove-Item -LiteralPath $candidateDir -Force}
 # The setup source ships beside the launcher. Embedding a 17 MB archive in a
 # .NET exe makes security scanners read it as a packed dropper.
 Copy-Item -LiteralPath (Join-Path $repo 'work-remix\launcher-source.zip') -Destination (Join-Path $repo 'launcher-source.zip') -Force
}
if(!(Test-Path -LiteralPath $exe)){throw 'Build the launcher first.'}
$shell=New-Object -ComObject WScript.Shell
$paths=@((Join-Path ([Environment]::GetFolderPath('Desktop')) 'Midnight Club 3 - ReDUB.lnk'))
$library=Join-Path (Split-Path -Parent $repo) 'shortcuts'
if(Test-Path -LiteralPath $library){$paths+=Join-Path $library 'Midnight Club 3 - ReDUB.lnk'}
foreach($path in $paths){$link=$shell.CreateShortcut($path);$link.TargetPath=$exe;$link.WorkingDirectory=$repo;$link.IconLocation="$icon,0";$link.Description='Midnight Club 3: ReDUB - Play and setup';$link.Save()}
Write-Host 'ReDUB launcher and shortcuts are ready.'
