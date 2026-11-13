# Windows Port Options

Open Options → Port Options. Select a row with the controller and press A to
change it; B returns to Options. F11 or Alt+Enter toggles fullscreen.

| Preset | Render/UI scale | FXAA | Filtering | Sharpening | VSync |
| --- | --- | --- | --- | --- | --- |
| Performance | 1.5x / 2x | On | 4x | 25% | Off |
| Balanced | 2x / 2x | On | 16x | 50% | Off |
| Quality | 3x / 3x | On | 16x | 50% | On |

Render scale, HUD texture quality and filtering require restarting. FXAA,
VSync, sharpening and the FPS/frame-time overlay change live. FXAA applies
only while driving so menu and loading text remain sharp; DLSS bypasses FXAA
instead of stacking a second anti-aliasing pass. Sharpening also works with
FXAA off. Manual graphics changes mark the preset
Custom. Restore Balanced Defaults resets graphics and disables the FPS overlay;
it preserves fullscreen, PC Music, saves and unrelated settings.

## Display adjustments

HUD Textures selects 1x, 2x, 3x or 5x texture upscaling independently of world
render resolution and requires a restart. Gamma (0.8–1.4) and Brightness
(80–120%) apply live in the final presentation pass, including with FXAA off.
Defaults are gamma 1.0 and brightness 100%, preserving the original picture.
Graphics presets preserve these adjustments; Restore Graphics Defaults resets
HUD textures to 2x and both image adjustments to neutral.

## PC Music / Now Playing

Play media in your browser or music player, then turn PC Music / Now Playing
on. Windows continues playing that audio normally; the port does not capture,
record, duplicate or retransmit system audio. No account login is needed.

The port announces the Windows media session's title and artist for six seconds
when a new track is detected,
then hides it. Repeated metadata updates and pause/resume do not restart the
banner. Enabling PC Music or returning to driving announces the current track.
Song Display selects Radio + Fallback or Overlay. Cruising uses the game's
native song-title text slot and animation; the Flash HUD path is retained for
modes where that HUD exists. Native placement requires a live validated HUD
and text that fits its font and buffer; other cases use the Unicode overlay.
The routing publishes one selected destination at a time and clears an old
native announcement when switching to the backup. Long overlay titles are shortened to fit. It prefers
the current playing session, falling back to another playing session. Apps that
do not publish Windows media information cannot supply titles; unsupported or
missing information cannot produce a track banner. Metadata updates about once
a second on a background helper, without
blocking input, audio or rendering. The FPS overlay is independent.

D-pad left/right controls are included: with PC Music enabled while driving,
left requests the previous track and right the next track.
Each press sends one request; holding a direction does not repeat. The selected
Windows media player must publish the corresponding skip control. Menu input
and normal radio behavior with PC Music off are unchanged. Requests are polled
on the helper thread at 100ms intervals; metadata remains polled once a second.

PC Music Volume changes the selected player's Windows mixer session volume in
10% steps (100% wraps to mute). Before a volume is chosen it follows the
player's current mixer level; the first selection without an available player
starts at 50%. The requested level is saved with port settings. Changes apply
in about one second while PC Music is on, including when the banner is hidden.
Windows master volume and the game's sound effects are untouched. This changes
the player's normal Windows mixer volume, so it also affects that player
outside the game, and all tabs sharing a browser audio session. It is not a
separate copy of the player's audio. Apps whose audio process cannot be matched
to their media identity show NO PLAYER when a requested volume cannot be
applied. Exclusive-mode audio is not supported by Windows session volume.

The port uses the same music category and volume setter as the game's Audio
Options music slider to silence the game radio, keeping effects/master audio
unchanged during driving. Menus restore the game's music level, rather than
replacing menu music. In a positively identified menu the helper requests a
pause from the active media player. Returning to driving (or an unknown state)
resumes only a player the game itself paused. Music already paused before the
game's intervention is never automatically started. Players must support
Windows media-session transport controls for this behavior. Unknown game
states never force a pause. The helper keeps the exact session it paused,
pauses at most once per menu visit, and retries rejected resume requests
instead of discarding ownership. It will not resume over another player that
is already playing. Current measured normal-driving state is primary
mode 2 / frontend screen 1; pause menu is mode 2 / screen 2. HUD-object presence
is not used as a gameplay detector.

Switching PC Music off restores the previous music level. That level
is stored beside the PC Music preference so it survives game restarts, including
when a game profile was saved while its music was silenced. Keep PC Music off
while adjusting the game's own music slider.

`now_playing.ps1` ships beside the executable. It uses built-in Windows
PowerShell and Windows media-session APIs (Windows 10 1809 or later). It only
reads local media metadata and sends the configured playback controls. If PowerShell/WinRT is unavailable, the overlay
reports that failure; PC audio can still play normally. The helper is tied to
the game process by a process handle and restores game-paused playback on
normal exit or crash before exiting itself. Turn PC Music off to hide the display and
restore game music. Once started, the metadata helper remains resident until
the game exits, allowing instant toggling. No system-wide mute or endpoint
volume change is used for menus.

Menu actions are posted to the Windows window thread without waiting from a
guest callback. Labels refresh on the following menu updates; settings logs
record both applying and applied actions for freeze diagnosis. The menu title
is set on entry/rebuild, rather than reapplying its movie state every frame.
Back preserves the native input-enable bit and waits for B release; settings
confirmations wait for A release before another change can be applied. The
row list's owner/vtable/page/capacity are validated after menu transitions.

VSync off can reduce display latency but may produce tearing. No setting
guarantees zero input lag. These presets do not unlock the original 30 FPS race
simulation; higher frame rates need separate physics/timing validation.


### Launcher display resolution

The Display page offers Automatic, 1280x720, 1600x900, 1920x1080,
2560x1440 and 3840x2160. This sets the client size in Windowed mode or
requests that display mode in Exclusive fullscreen. Unsupported exclusive
modes leave the game windowed. Borderless fullscreen uses the desktop mode.
Render scale is a separate internal rendering control. These settings require
restarting the game. The experimental Vulkan launcher now imports persisted
fullscreen and resolution from the most recently edited renderer settings file.
The launcher resolves the current 1920x1080 fullscreen configuration correctly;
exclusive-mode availability still depends on the monitor and driver.
