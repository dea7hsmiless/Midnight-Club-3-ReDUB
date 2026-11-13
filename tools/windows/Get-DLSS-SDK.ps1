param()
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$manifest=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'dlss-sdk.json') -Raw | ConvertFrom-Json
if($manifest.commit -notmatch '^[a-f0-9]{40}$'){throw 'Invalid pinned SDK revision.'}
$destination=Join-Path $repo 'work-remix\deps\nvidia-dlss'
New-Item -ItemType Directory -Path $destination -Force | Out-Null
Write-Host 'NVIDIA SDK files are subject to NVIDIA''s license, separately from ReDUB''s source license:'
Write-Host "https://github.com/NVIDIA/DLSS/blob/$($manifest.commit)/LICENSE.txt"
foreach($file in $manifest.files){
    if($file.path -notmatch '^[A-Za-z0-9_./-]+$' -or $file.path -match '(^|/)\.\.(/|$)'){throw 'Invalid SDK manifest path.'}
    $target=Join-Path $destination $file.path
    if((Test-Path -LiteralPath $target) -and (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash -eq $file.sha256){continue}
    New-Item -ItemType Directory -Path (Split-Path -Parent $target) -Force | Out-Null
    $temporary=$target+'.download'
    try {
        Invoke-WebRequest -UseBasicParsing "https://raw.githubusercontent.com/NVIDIA/DLSS/$($manifest.commit)/$($file.path)" -OutFile $temporary
        if((Get-FileHash -LiteralPath $temporary -Algorithm SHA256).Hash -ne $file.sha256){throw "SDK checksum mismatch: $($file.path)"}
        Move-Item -LiteralPath $temporary -Destination $target -Force
    }finally{if(Test-Path -LiteralPath $temporary){Remove-Item -LiteralPath $temporary}}
}
Write-Host "Pinned SDK verified: $($manifest.commit). Stored only in ignored work-remix/deps."
