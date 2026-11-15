param()
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem
$repo=Split-Path -Parent $PSScriptRoot
$bundle=Join-Path $repo 'work-remix\launcher-source.zip'
New-Item -ItemType Directory -Path (Split-Path -Parent $bundle) -Force | Out-Null
Push-Location -LiteralPath $repo
try {
    if(Test-Path -LiteralPath (Join-Path $repo '.git')){
        $paths=@(& git -c core.quotepath=false ls-files --cached --others --exclude-standard) | Sort-Object -Unique
        if($LASTEXITCODE -ne 0){throw 'Git source inventory failed.'}
    }elseif(Test-Path -LiteralPath (Join-Path $repo 'launcher-source-manifest.json')){
        $paths=@((Get-Content -LiteralPath (Join-Path $repo 'launcher-source-manifest.json') -Raw | ConvertFrom-Json).files | ForEach-Object {$_.path})
    }else{throw 'No Git/source-bundle inventory found. Use the complete ReDUB source package.'}
    $requiredTextures=@('hd/textures/22762f0e2f14cca4.png','hd/textures/7680ba077867b3e3.png','hd/textures/851e07656bc1c45f.png')
    foreach($texture in $requiredTextures){if(!(Test-Path -LiteralPath (Join-Path $repo $texture))){throw "Missing required ReDUB logo texture: $texture"}}
    $paths=@($paths + $requiredTextures) | Sort-Object -Unique
    $selected=@();$total=0
    foreach($path in $paths){
        if($path -match '^(\.git/|source/|game(?:-[^/]*)?/|work(?:-[^/]*)?/|build[^/]*/|keys/|hd/(?!textures/(?:22762f0e2f14cca4|7680ba077867b3e3|851e07656bc1c45f)\.png$)|bugs/|drafts/)' -or $path -eq 'PORT-WORK-STATUS.md'){continue}
        if($path -match '(?i)\.(exe|dll|lib|obj|o|a|pdb|dmp|iso|xiso|xbe|bik|xwb|xsb|dat|wav|mp3|mp4|7z|zip|nsp|xci|nro|elf|key|keys|pem|lnk)$'){continue}
        if($path -match '(^|/)(preferences\.ini|\.env|prod\.keys)$'){continue}
        if($path -match '[\r\n]' -or [IO.Path]::IsPathRooted($path) -or $path -match '(^|/)\.\.(/|$)'){throw 'Invalid source inventory path.'}
        $file=Get-Item -LiteralPath (Join-Path $repo $path) -ErrorAction SilentlyContinue
        if(!$file -or $file.PSIsContainer){continue}
        if($file.Length -gt 10MB){throw "Unexpected large source file: $path"}
        $selected+=@{path=$path;sha256=(Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()}
        $total+=$file.Length
    }
    if($selected.Count -lt 100){throw 'Source inventory unexpectedly incomplete.'}
    $stream=[IO.File]::Open($bundle,[IO.FileMode]::Create)
    $zip=[IO.Compression.ZipArchive]::new($stream,[IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach($file in $selected){[IO.Compression.ZipFileExtensions]::CreateEntryFromFile($zip,(Join-Path $repo $file.path),$file.path,[IO.Compression.CompressionLevel]::Optimal) | Out-Null}
        $entry=$zip.CreateEntry('launcher-source-manifest.json')
        $writer=[IO.StreamWriter]::new($entry.Open(),[Text.UTF8Encoding]::new($false))
        try{$writer.Write((ConvertTo-Json -Depth 4 -InputObject @{format=1;files=$selected}))}finally{$writer.Dispose()}
    }finally{$zip.Dispose();$stream.Dispose()}
    Write-Host "Embedded source bundle: $($selected.Count) files, $([Math]::Round($total/1MB,1)) MB uncompressed. Game data, SDK binaries, build output and personal settings excluded."
}finally{Pop-Location}
