param([switch]$Headless,[switch]$CheckOnly,[ValidateRange(0,600)][int]$DiagnosticSeconds=0,[ValidateSet('Off','Quality','Balanced','Performance')][string]$DlssMode='Off',[switch]$CaptureTemporal,[switch]$EvaluateScene,[switch]$JitterScene,[switch]$PresentScene,[switch]$NoJitter)
$ErrorActionPreference='Stop'
if($PresentScene){$EvaluateScene=$true}
# Temporary user-approved default: DLSS jitter is disabled until temporal
# alignment is corrected. -JitterScene remains an explicit diagnostic opt-in.
if($NoJitter){$JitterScene=$false}
if(($EvaluateScene -or $JitterScene) -and $DlssMode -eq 'Off'){throw 'Scene evaluation requires a DLSS mode.'}
if($JitterScene -and !$EvaluateScene){throw '-JitterScene requires -EvaluateScene.'}
$repo=Split-Path -Parent $PSScriptRoot
$dist=Join-Path $repo 'work-remix\build-windows-vulkan-experimental\dist'
$exe=Join-Path $dist 'mcdub3remix.exe'
if(!(Test-Path -LiteralPath $exe)){throw 'Build the experiment with .portable\Build-Vulkan-Experiment.ps1 first.'}
if(Get-Process mcdub3remix -ErrorAction SilentlyContinue){throw 'Save and close the running game before starting a separate experimental session.'}
$sharedSave=Join-Path $repo 'work-remix\save'
$legacySave=Join-Path $repo 'work-remix\dlss-experimental-save'
# Older test builds isolated the emulated Xbox system partitions. Career
# profiles always lived in game-remix/UDATA. Reuse those large partition
# images instead of creating a duplicate set when moving to shared storage.
if(!(Test-Path -LiteralPath $sharedSave) -and (Test-Path -LiteralPath $legacySave)){
    Move-Item -LiteralPath $legacySave -Destination $sharedSave
}
$settingsKeys=@(
    'MC3_FULLSCREEN','MC3_RESOLUTION','MC3_GAMMA','MC3_BRIGHTNESS','MC3_FPS_OVERLAY',
    'MC3_GRAPHICS_PRESET','MC3_PC_MUSIC','MC3_PC_MUSIC_HUD','MC3_PC_MUSIC_VOLUME',
    'MC3_PC_MUSIC_RESTORE_VOLUME','MC3_WHEEL','MC3_WHEEL_DEVICE','MC3_WHEEL_RUMBLE','MC3_RUMBLE_STRENGTH',
    'MC3_WHEEL_STEER_AXIS','MC3_WHEEL_THROTTLE_AXIS','MC3_WHEEL_BRAKE_AXIS',
    'MC3_WHEEL_STEER_INVERT','MC3_WHEEL_THROTTLE_INVERT','MC3_WHEEL_BRAKE_INVERT',
    'RECOMP_GL_SCALE','RECOMP_GL_UI_SCALE','RECOMP_GL_VSYNC','RECOMP_GL_FXAA',
    'RECOMP_GL_ANISO','RECOMP_GL_SHARPEN','RECOMP_GL_QUEUE_FRAMES'
)
$settings=@{
    # UDATA/career profiles live with the shared game directory. Point the
    # emulated Xbox system partitions at the same local folder OpenGL detects.
    MC3_GAME_DIR=(Join-Path $repo 'game-remix');MC3_SAVE_DIR=$sharedSave;
    RECOMP_GL_SCALE='1.5';RECOMP_GL_VSYNC='1';MC3_PC_MUSIC='0';MC3_FULLSCREEN='0';MC3_RESOLUTION='auto';
    RECOMP_QUIET='1';RECOMP_FPS_LOG='1';
    RECOMP_VK_HEADLESS=$(if($Headless){'1'}else{'0'});RECOMP_WATCHDOG_SECS="$DiagnosticSeconds"
    MC3_DLSS=([Array]::IndexOf(@('Off','Quality','Balanced','Performance'),$DlssMode)).ToString()
    MC3_DLSS_PRESENT=$(if($PresentScene){'1'}else{'0'})
    MC3_DLSS_JITTER=$(if($JitterScene){'1'}else{'0'})
    MC3_DLSS_EVALUATE=$(if($EvaluateScene){'1'}else{'0'})
    MC3_DLSS_CAPTURE=$(if($CaptureTemporal){Join-Path $repo ('work-remix\logs\temporal-'+(Get-Date -Format 'yyyyMMdd-HHmmss'))}else{''})
}
# Both renderers keep their own settings file because the game writes beside
# its executable. Use whichever was edited most recently as the shared
# display/graphics/music preference source, then layer the chosen DLSS mode.
$settingFiles=@(
    (Join-Path $repo 'work-remix\build-windows-x86_64\dist\mcdub3remix_env.txt'),
    (Join-Path $dist 'mcdub3remix_env.txt')
) | Where-Object {Test-Path -LiteralPath $_} | Sort-Object { (Get-Item -LiteralPath $_).LastWriteTimeUtc }
foreach($file in $settingFiles){
    foreach($line in [IO.File]::ReadLines($file)){
        if($line -match '^\s*([A-Z0-9_]+)=(.*)$' -and $settingsKeys -contains $Matches[1]){
            $settings[$Matches[1]]=$Matches[2].Trim()
        }
    }
}
if($settings.MC3_FULLSCREEN -notin @('0','1','2')){throw "Invalid persisted fullscreen mode: $($settings.MC3_FULLSCREEN)"}
if($settings.MC3_RESOLUTION -ne 'auto' -and $settings.MC3_RESOLUTION -notmatch '^\d{3,4}x\d{3,4}$'){throw "Invalid persisted resolution: $($settings.MC3_RESOLUTION)"}
if($CheckOnly){
    Write-Host "Vulkan settings resolved: fullscreen=$($settings.MC3_FULLSCREEN), resolution=$($settings.MC3_RESOLUTION), DLSS=$DlssMode. The selected renderer settings are imported from the most recently edited renderer config."
    return
}
$previous=@{};foreach($key in $settings.Keys){$previous[$key]=[Environment]::GetEnvironmentVariable($key);[Environment]::SetEnvironmentVariable($key,$settings[$key])}
try {
    New-Item -ItemType Directory -Path $settings.MC3_SAVE_DIR -Force | Out-Null
    $logs=Join-Path $repo 'work-remix\logs';New-Item -ItemType Directory -Path $logs -Force | Out-Null
    $stamp=Get-Date -Format 'yyyyMMdd-HHmmss'
    $style=if($Headless){'Hidden'}else{'Normal'}
    $process=Start-Process -FilePath $exe -WorkingDirectory $dist -WindowStyle $style -RedirectStandardOutput (Join-Path $logs "vulkan-experiment-$stamp-stdout.log") -RedirectStandardError (Join-Path $logs "vulkan-experiment-$stamp-stderr.log") -PassThru
    Write-Host "Vulkan session PID $($process.Id); shared career saves and local system data. DLSS scene presentation requested: $PresentScene."
}finally{foreach($key in $settings.Keys){[Environment]::SetEnvironmentVariable($key,$previous[$key])}}
