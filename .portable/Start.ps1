param([switch]$CheckOnly)
$ErrorActionPreference = 'Stop'
try {
    $repo = Split-Path -Parent $PSScriptRoot
    $preferences=Join-Path $repo 'launcher\preferences.ini'
    if((Test-Path -LiteralPath $preferences) -and (Get-Content -LiteralPath $preferences | Where-Object {$_ -match '^\s*Renderer\s*=\s*vulkan\s*$'})){
        $dlss='Off'
        if(Test-Path -LiteralPath $preferences){$line=Get-Content -LiteralPath $preferences | Where-Object {$_ -match '^\s*DlssMode\s*='} | Select-Object -Last 1;if($line -match '^\s*DlssMode\s*=\s*(Quality|Balanced|Performance)\s*$'){$dlss=$Matches[1]}}
        $vulkanDist=Join-Path $repo 'work-remix\build-windows-vulkan-experimental\dist'
        if($CheckOnly){
            foreach($file in @((Join-Path $vulkanDist 'mcdub3remix.exe'),(Join-Path $vulkanDist 'SDL2.dll'),(Join-Path $repo 'game-remix\default.xbe'))){if(!(Test-Path -LiteralPath $file)){throw 'The selected Vulkan renderer is not built. Choose Play from the launcher to finish setup, or select OpenGL.'}}
            if($dlss -ne 'Off'){foreach($file in @('redub_dlss.dll','nvngx_dlss.dll')){if(!(Test-Path -LiteralPath (Join-Path $vulkanDist $file))){throw 'The DLSS runtime is missing. Choose Play from the launcher to finish setup, or turn DLSS Off.'}}}
            Write-Host 'Selected Vulkan renderer, controller runtime and disc files found.';exit 0
        }
        $vulkanScript=Join-Path $PSScriptRoot 'Start-Vulkan-Experiment.ps1'
        if($dlss -ne 'Off'){& $vulkanScript -DlssMode $dlss -PresentScene}
        else{& $vulkanScript -DlssMode $dlss}
        exit $LASTEXITCODE
    }
    $dist = Join-Path $repo 'work-remix\build-windows-x86_64\dist'
    $exe = Join-Path $dist 'mcdub3remix.exe'
    foreach ($file in @($exe, (Join-Path $dist 'SDL2.dll'), (Join-Path $repo 'game-remix\default.xbe'))) {
        if (!(Test-Path -LiteralPath $file)) { throw "Missing $file. Run 'Build - Midnight Club 3 Remix.cmd' first." }
    }
    if ($CheckOnly) { Write-Host 'Installed game, controller runtime and disc files found.'; exit 0 }
    if (Get-Process mcdub3remix -ErrorAction SilentlyContinue) { Write-Host 'Midnight Club 3 Remix is already running.'; exit 0 }
    # The game is interactive; its own startup handles logging and settings.
    Start-Process -FilePath $exe -WorkingDirectory $dist -WindowStyle Normal
} catch { Write-Host $_.Exception.Message -ForegroundColor Red; exit 1 }
