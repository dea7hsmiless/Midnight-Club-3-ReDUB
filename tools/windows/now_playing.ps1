# Local media-session bridge. No logins/network. Pauses only for game menus.
param([int]$OwnerPid, [switch]$Once, [switch]$EnableMenuPause)
$ErrorActionPreference = 'Stop'
[Console]::OutputEncoding = New-Object Text.UTF8Encoding($false)
$pausedByGame = $null
$view = $null
$mapping = $null
$lastCommand = 0
$nextMetadata = [DateTime]::MinValue
$nextResumeAt = [DateTime]::MinValue
$menuPauseAttempted = $false
try {
    Add-Type -AssemblyName System.Runtime.WindowsRuntime
    $null = [Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager, Windows.Media.Control, ContentType=WindowsRuntime]
    $asTask = [System.WindowsRuntimeSystemExtensions].GetMethods() | Where-Object { $_.Name -eq 'AsTask' -and $_.IsGenericMethod -and $_.GetParameters().Count -eq 1 -and $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1' } | Select-Object -First 1
    function Await($operation, $resultType) {
        $task = $asTask.MakeGenericMethod($resultType).Invoke($null, @($operation))
        if (!$task.Wait(3000)) { throw 'Media query timed out' }
        $task.Result
    }
    $manager = Await ([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager]::RequestAsync()) ([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionManager])
    $owner = if ($OwnerPid) { Get-Process -Id $OwnerPid -ErrorAction Stop } else { $null }
    if ($OwnerPid -and !$Once) {
        $mapping = [IO.MemoryMappedFiles.MemoryMappedFile]::OpenExisting('Local\MC3MediaMode_' + $OwnerPid)
        $view = $mapping.CreateViewAccessor(0,8,[IO.MemoryMappedFiles.MemoryMappedFileAccess]::Read)
    }
    function Resume-Owned {
        if ($script:pausedByGame) {
            if ([DateTime]::UtcNow -lt $script:nextResumeAt) { return }
            $script:nextResumeAt = [DateTime]::UtcNow.AddSeconds(1)
            try {
                $otherPlaying = @($manager.GetSessions() | Where-Object { $_.SourceAppUserModelId -ne $script:pausedByGame.SourceAppUserModelId -and $_.GetPlaybackInfo().PlaybackStatus.ToString() -eq 'Playing' }).Count -gt 0
                # Windows can change CurrentSession when the original player
                # pauses. A different *paused* session is not a reason to
                # abandon the exact session we own. Never interrupt another
                # session that is actually playing.
                if ($otherPlaying) {
                    [Console]::WriteLine('POLICY: resume cancelled because active player changed')
                    $script:pausedByGame = $null
                    return
                }
                $state = $script:pausedByGame.GetPlaybackInfo().PlaybackStatus.ToString()
                if ($state -eq 'Playing' -or $state -eq 'Closed' -or $state -eq 'Stopped') {
                    $script:pausedByGame = $null
                    return
                }
                if ($state -eq 'Paused') {
                    if (Await ($script:pausedByGame.TryPlayAsync()) ([bool])) {
                        [Console]::WriteLine('POLICY: restored player paused by game [' + $script:pausedByGame.SourceAppUserModelId + ']')
                        $script:pausedByGame = $null
                    } else {
                        [Console]::WriteLine('POLICY: player rejected resume; ownership retained for retry')
                    }
                }
            } catch { try { [Console]::WriteLine('POLICY: resume failed: ' + $_.Exception.Message) } catch {} }
        }
    }
    do {
        if ($owner -and $owner.HasExited) { break }
        $flags = if ($view) { $view.ReadInt32(0) } else { 0 }
        if (($flags -band 1) -eq 0 -or ($flags -band 4) -eq 0) { $menuPauseAttempted = $false }
        $command = if ($view) { $view.ReadInt32(4) } else { 0 }
        $skip = $command -ne 0 -and $command -ne $lastCommand
        $lastCommand = $command
        if ($pausedByGame -and (($flags -band 1) -eq 0 -or ($flags -band 4) -eq 0)) { Resume-Owned }
        $text = 'PC MUSIC: NO ACTIVE MEDIA'
        $source = ''
        try {
            $session = $manager.GetCurrentSession()
            if (!$session -or $session.GetPlaybackInfo().PlaybackStatus.ToString() -ne 'Playing') {
                foreach ($candidate in $manager.GetSessions()) {
                    if ($candidate.GetPlaybackInfo().PlaybackStatus.ToString() -eq 'Playing') { $session = $candidate; break }
                }
            }
            if ($session) {
                # Commands are consumed even in menus; never replay an old
                # press when returning to driving. Each held D-pad press is
                # already debounced by the native game thread.
                if ($skip -and ($flags -band 3) -eq 3 -and ($flags -band 4) -eq 0) {
                    $controls = $session.GetPlaybackInfo().Controls
                    $ok = $false
                    if (($command -band 1) -ne 0 -and $controls.IsNextEnabled) {
                        $ok = Await ($session.TrySkipNextAsync()) ([bool])
                    } elseif (($command -band 1) -eq 0 -and $controls.IsPreviousEnabled) {
                        $ok = Await ($session.TrySkipPreviousAsync()) ([bool])
                    }
                    [Console]::WriteLine('POLICY: D-pad ' + $(if (($command -band 1) -ne 0) { 'next' } else { 'previous' }) + $(if ($ok) { ' accepted' } else { ' unavailable for this player' }))
                    $nextMetadata = [DateTime]::MinValue
                }
                if ($EnableMenuPause -and $view -and !$menuPauseAttempted -and !$pausedByGame -and ($flags -band 1) -ne 0 -and ($flags -band 4) -ne 0 -and $session.GetPlaybackInfo().PlaybackStatus.ToString() -eq 'Playing') {
                    if (Await ($session.TryPauseAsync()) ([bool])) {
                        $pausedByGame = $session
                        $menuPauseAttempted = $true
                        $nextResumeAt = [DateTime]::MinValue
                        [Console]::WriteLine('POLICY: paused player for menu [' + $session.SourceAppUserModelId + ']')
                    }
                }
                $source = $session.SourceAppUserModelId
                if ($Once -or [DateTime]::UtcNow -ge $nextMetadata) {
                  $properties = Await ($session.TryGetMediaPropertiesAsync()) ([Windows.Media.Control.GlobalSystemMediaTransportControlsSessionMediaProperties])
                  if ($properties.Title) {
                    $state = $session.GetPlaybackInfo().PlaybackStatus.ToString()
                    $text = if ($state -eq 'Playing') { 'NOW PLAYING: ' } else { 'PAUSED: ' }
                    $text += $properties.Title
                    if ($properties.Artist) { $text += ' - ' + $properties.Artist }
                  }
                }
            }
        } catch { $text = 'PC MUSIC: MEDIA INFORMATION UNAVAILABLE' }
        if ($Once -or [DateTime]::UtcNow -ge $nextMetadata) {
            [Console]::WriteLine('SESSION:' + ($source -replace '[\r\n\t]', ' '))
            [Console]::WriteLine(($text -replace '[\r\n\t]', ' '))
            $nextMetadata = [DateTime]::UtcNow.AddSeconds(1)
        }
        if (!$Once) { Start-Sleep -Milliseconds 100 }
    } while (!$Once)
} catch { try { [Console]::WriteLine('PC MUSIC: WINDOWS MEDIA INFORMATION UNAVAILABLE') } catch {} }
finally {
    # This also runs after a broken stdout pipe when the game dies.
    for ($attempt = 0; $pausedByGame -and $attempt -lt 3; $attempt++) {
        $nextResumeAt = [DateTime]::MinValue
        Resume-Owned
        if ($pausedByGame) { Start-Sleep -Milliseconds 250 }
    }
    if ($view) { $view.Dispose() }
    if ($mapping) { $mapping.Dispose() }
}
