# The sources/output stay in the selected installation folder. A directory
# junction gives upstream tools a stable path without whitespace when needed.
function Get-ReDUBBuildRoot([string]$ProjectPath) {
    $physical=[IO.Path]::GetFullPath($ProjectPath).TrimEnd('\')
    if($physical -notmatch '\s'){return $physical}
    $base=Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'ReDUB\build-paths'
    if($base -match '\s'){$base=Join-Path ([Environment]::GetFolderPath('CommonApplicationData')) ('ReDUB\build-paths\'+[Security.Principal.WindowsIdentity]::GetCurrent().User.Value)}
    if($base -match '\s'){throw 'No usable build alias location was found. The installation remains untouched.'}
    $hash=[Security.Cryptography.SHA256]::Create()
    try{$id=([BitConverter]::ToString($hash.ComputeHash([Text.Encoding]::UTF8.GetBytes($physical.ToLowerInvariant())))).Replace('-','').Substring(0,16)}finally{$hash.Dispose()}
    New-Item -ItemType Directory -Path $base -Force | Out-Null
    $alias=Join-Path $base $id
    $existing=Get-Item -LiteralPath $alias -Force -ErrorAction SilentlyContinue
    if($existing){
        if(!($existing.Attributes -band [IO.FileAttributes]::ReparsePoint) -or !@($existing.Target).Count){throw 'An unrelated folder occupies the build alias path.'}
        $target=[IO.Path]::GetFullPath([string]@($existing.Target)[0]).TrimEnd('\')
        if(!$target.Equals($physical,[StringComparison]::OrdinalIgnoreCase)){throw 'The build alias points to a different installation.'}
    }else{New-Item -ItemType Junction -Path $alias -Target $physical | Out-Null}
    Write-Host 'Using a build-path alias; source, game files, caches and output remain in this installation folder.'
    return $alias
}
