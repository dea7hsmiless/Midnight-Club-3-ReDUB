<p align="center"><img src="launcher/art/hero.jpg" alt="Midnight Club 3 cars in a city at night" width="1200"></p>
<p align="center"><img src="launcher/art/logo.png" alt="Midnight Club 3 ReDUB Edition" width="560"></p>

<p align="center"><strong>A Windows-focused community edition built through static recompilation.</strong><br>
Original Xbox code translated to native C, with OpenGL and Vulkan renderers, DLSS Super Resolution, and a portable launcher.</p>

<p align="center"><a href="https://github.com/dea7hsmiless/Midnight-Club-3-ReDUB/tree/main">Source</a> · <a href="https://github.com/dea7hsmiless/Midnight-Club-3-ReDUB/releases">Releases</a> · <a href="BUILDING-WINDOWS.md">Build from source</a></p>

# Midnight Club 3: ReDUB

A Windows-focused community edition of **Midnight Club 3: DUB Edition Remix**, built on [mcdub3-nx](https://github.com/laureladastra/mcdub3-nx) and [xboxrecomp](https://github.com/sp00nznet/xboxrecomp).

ReDUB combines a portable setup launcher, native in-game Port Options, graphics adjustments, Xbox controller support, and optional PC music integration.

**Bring your own original Xbox Remix game files. No disc image, game assets, soundtrack, or saves are included.** This project is unofficial and is not affiliated with Rockstar Games, Take-Two, or Microsoft.

## Which game files do I need?

Use **Midnight Club 3: DUB Edition Remix for the original Xbox** (the first Xbox console). Supply your own supported Xbox ISO/XISO disc dump, or the complete extracted Xbox disc folder containing `default.xbe` and all remaining game data.

**PS2/PCSX2 images, the non-Remix DUB Edition, and Nintendo Switch NSP/XCI/NRO files are not compatible inputs.** Xbox 360/One/Series controller support does not change the required source game. Upstream Switch builds are an output platform, not a source disc format.

Use only game files you are legally authorized to use. Where permitted, these may come from your own disc backup; applicable law and license terms vary. ReDUB does not provide game downloads or permission to redistribute copyrighted game data. Its source-code license does not license the game's executable, assets, or soundtrack.

## Quick start

1. Put the self-contained **ReDUB Launcher.exe** in the writable folder where you want to install, such as `D:\Games\ReDUB`. The full release zip already contains the game executable and its runtime. A full source checkout also works.
2. Open **ReDUB Launcher.exe**. From source without that executable, open **Launch ReDUB.cmd** to compile the launcher using Windows' .NET Framework compiler.
3. For a new installation, open **Install** and choose your **original Xbox DUB Edition Remix ISO/XISO** or the complete extracted Xbox folder.
4. Select **Install**. Extracted folders are copied and ISOs are unpacked by the built-in reader. Nothing is compiled, and no MSYS2, CMake or internet download is needed. Import shows progress; **Cancel import** or Esc stops it and keeps the existing game files.
5. Choose **Play**. Open **Options → Port Options** for graphics and PC Music settings.

An existing installation is detected automatically and can play immediately. Replacing game files keeps saves and settings. If the release folder is missing the game executable, use the full ReDUB Windows zip or **Settings → Advanced → Rebuild from source**. The launcher does not download games or pull source updates from GitHub.

## Linux AppImage

Download **ReDUB-0.2.4-x86_64.AppImage** from [Releases](https://github.com/dea7hsmiless/Midnight-Club-3-ReDUB/releases/latest). Mark it executable, then select your extracted Xbox Remix game folder once:

```sh
chmod +x ReDUB-0.2.4-x86_64.AppImage
./ReDUB-0.2.4-x86_64.AppImage --game-dir "/path/to/extracted/game"
```

A plain `ReDUB-0.2.4-x86_64.tar.gz` carries the same AppDir. `--portable DIR` keeps config, data, cache and state beside the package instead of in XDG directories. The folder is remembered for later launches. Requires x86_64 Linux, glibc 2.39+ (Ubuntu 24.04 / Debian 13 tested), X11/XWayland, and OpenGL 3.3. The package bundles SDL2, D-Bus, Bink-only FFmpeg, and the ReDUB logo artwork. Saves and Port Options live in user directories; original game files may be read-only. If FUSE is unavailable, use `APPIMAGE_EXTRACT_AND_RUN=1 ./ReDUB-0.2.4-x86_64.AppImage`.

Shared menu, synchronization, pushbuffer, and movie fixes match the Windows release source; the entry/page title uses **Port Options** capitalization. Linux PC Music uses MPRIS, with transliterated song text. Native Wayland, Vulkan/DLSS, and experimental 60/120 FPS are excluded. Native desktop sound and MPRIS output remain unverified in our WSLg environment. See [Linux setup and build instructions](BUILDING-LINUX.md).

## Launcher

The sidebar layout is inspired by [SafeHouse](https://github.com/FridiNaTor1/SafeHouse/tree/main/launcher/ui), with ReDUB's own code and branding.

| Page | Purpose |
| --- | --- |
| Game | Installation status, Play/Install, verification, repair, game folder, desktop/library shortcuts. |
| Install | Choose an Xbox Remix ISO/XISO or extracted folder; copies or unpacks the disc data only. |
| Settings | Display, Graphics, Controls, Music, Diagnostics, Launcher, and Advanced groups. Advanced rebuilds the game from source. |
| Support | Local setup output, checks, logs, and this guide. |
| About | Project identity, features, limitations, and upstream credits. |

Enable **Settings → Launcher → Skip launcher on startup** to start the installed game directly. This is off by default. Hold **Shift** while starting, or use **Open ReDUB Launcher.cmd**, to open the launcher again. Missing required files fall back to setup. The preference is stored separately in `launcher/preferences.ini`.

**Settings → Graphics → Render queue** exposes the existing renderer queue limit. One frame can reduce queued input delay; two frames is the default and may tolerate uneven rendering better. Restart to apply it. This does not unlock the original frame-rate target.

Close the game before changing launcher game settings. During gameplay, use native Port Options for supported live changes. Launcher saves preserve comments and unrelated settings, write atomically, and retain an initial `.launcher-backup`. Graphics presets preserve HUD quality, gamma, brightness, and music preferences.

Setup output appears on Support. Save and close the game before adding or replacing game files, or before an Advanced rebuild. Existing game files, saves, and settings are preserved; there is no destructive reset/delete button. Import is copy-only; the release zip supplies the executable. Move the complete installation together to retain installed game data and recreate shortcuts afterward. Moving only the launcher carries artwork and setup source, not game data. Advanced builds use a build-tool junction alias for paths with spaces; files stay in the selected folder.

Optional artwork lives in `launcher/art`: `hero.jpg` or `hero.png`, `logo.png`, and `redub.ico`. Restart to reload images. Missing or unreadable images use the gradient/text fallback. See the [artwork guide and provenance](launcher/art/ARTWORK.md).

## Graphics and display

These settings are available through native **Port Options** with controller navigation. Render queue is currently launcher-only.

The same Port Options menu and graphics settings backend are built for Windows and Linux. The Linux AppImage ships the OpenGL renderer, with shared display, FXAA, sharpening, gamma/brightness, FPS overlay, controller vibration, PC Music controls, final presentation overlays, and the ReDUB HD logo pack. Linux uses MPRIS over the desktop D-Bus for track metadata, previous/next, menu pause/resume, and player volume when supported. The external player continues through the normal desktop audio mix; the game radio is muted while driving. DLSS and the experimental Vulkan path remain Windows-side work and are not part of the Linux AppImage. Linux build and WSLg test setup are documented in [BUILDING-LINUX.md](BUILDING-LINUX.md).

### Experimental Vulkan and DLSS

The original OpenGL renderer remains available. **Settings → Graphics → Graphics API** selects OpenGL or Vulkan; both renderers share Career/profile data and local Xbox system data. Its **DLSS Super Resolution** control is a familiar On/Off switch, with **Quality**, **Balanced**, and **Performance** modes. Choosing **Play** builds missing Vulkan/DLSS files automatically. All three modes passed controlled GPU evaluations on an RTX 3070. Quality, Balanced, and Performance also passed short user driving checks on that system with jitter disabled. Other GPUs/drivers and longer sessions remain unverified. If DLSS cannot initialize, Vulkan falls back to native rendering. NVIDIA runtime files are downloaded into ignored build data and are not part of the GitHub source package. Fullscreen and resolution settings are shared with Vulkan and persist across launches.

Vulkan now follows OpenGL for small effects-buffer scaling, render-texture padding sampling, and fixed-function texture matrices. User checks found the earlier white border and blurred right-side strip resolved, with particles and race markers animating correctly. No presentation crop is applied. DLSS jitter is disabled by default because it caused fine-detail shaking in local testing. No frame generation or higher simulation rate is included.

| Setting | Behavior |
| --- | --- |
| Fullscreen | Live borderless desktop fullscreen; **F11** and **Alt+Enter** also toggle it. |
| VSync | Off, On, Adaptive; unsupported adaptive mode falls back through SDL. |
| Presets | Performance, Balanced, Quality, and Custom after adjustments. |
| Render scale | 1×, 1.5×, 2×, 3×, 5×; restart required. |
| HUD texture quality | Independent 1×, 2×, 3×, 5× upscaling; restart required. Improves existing art rather than supplying an HD asset pack. |
| FXAA | Off, Low, Standard, High; changes live while driving. Menus/loading bypass it for sharp text, and DLSS bypasses it to avoid double anti-aliasing. |
| Texture filtering | 1×, 4×, 16× anisotropic filtering; restart required. |
| Sharpening | 0–100% in 25% steps, live; uses the FXAA presentation path. |
| Gamma | 0.8–1.4, live even with FXAA off; default 1.0. |
| Brightness | 80–120%, live even with FXAA off; default 100%. |
| FPS overlay | Frame rate and smoothed frame-time display. |
| Restore Graphics Defaults | Balanced graphics, HUD 2×, neutral image adjustments, FPS overlay off; music and fullscreen retained. |

| Preset | World scale | Filtering | Sharpening | VSync |
| --- | --- | --- | --- | --- |
| Performance | 1.5× | 4× | 25% | Off |
| Balanced | 2× | 16× | 50% | Off |
| Quality | 3× | 16× | 50% | On |

Presets enable standard FXAA and preserve HUD quality, gamma, and brightness. Restart for scale and filtering changes; FXAA changes live. VSync Off may reduce display latency with possible tearing; no setting guarantees zero input lag.

## HD texture packs

The renderer's native replacement loader uses Xbox texture hashes and PNG files at `hd/textures/<16-digit-hash>.png`. This source includes a small optional ReDUB title/menu logo set at the three matching hashes. It uses the custom ReDUB logo artwork supplied by the maintainer; no original game textures or disc data are included. The loader finds `hd/` beside the player's `game-remix/` folder automatically. Other replacement packs are not included. Large packs can increase memory and loading costs.


## Controllers

Xbox controllers use SDL's controller mappings, including Xbox 360 and Xbox One-family devices recognized by the runtime. Windows setup preserves the tested native SDL2 runtime. Connect before launching if detection is inconsistent. Port Options uses normal controller navigation.

With PC Music enabled while driving, **D-pad Left/Right** request previous/next tracks. Holding a direction sends one request. The player must expose Windows media-session controls or, on Linux, MPRIS controls. Menu navigation and normal radio behavior with PC Music off are retained.

**Settings → Controls** adjusts controller and wheel rumble strength. Raw SDL steering-wheel input is an experimental compatibility path with device/axis selection, inversion, and deadzone controls. It maps wheel steering, pedals, hats, and buttons into the game's Xbox controller input. It has not been tested across a range of physical wheels. Rumble works only when the connected device/runtime exposes it; full wheel force-feedback effects, wheel-specific shifter/clutch mappings, and per-game sensitivity curves are not implemented.

## PC Music / Now Playing

Start Spotify, YouTube, or another desktop player normally, then enable **PC Music (Driving)**. No Spotify login or account connection is needed. Windows uses media sessions; Linux uses the player's MPRIS interface over D-Bus. Both leave the player routed through the normal system audio mix and mute the game's radio while driving.

- Audio continues through the normal desktop mix; ReDUB does **not** capture and replay system audio.
- Driving silences the game's radio/music category while retaining engine, dialogue, and effects.
- Menus restore game music. The helper pauses the selected external session once per menu visit and resumes that exact session when driving resumes. It does not start a player that was already manually paused and avoids resuming over another playing app.
- Failed resume requests retain ownership and retry. Start the desired player once before testing, especially after changing apps.
- Title and artist appear for about **six seconds**. A new track triggers a fresh announcement; repeated metadata polls do not.
- **Song Display: Radio + Fallback** prefers the native cruising radio-title area. Unsupported fonts, long titles, or unavailable HUDs use the Unicode backup. Shared routing stops native animation before fallback and suppresses muted game-radio notifications while PC Music owns the display.
- **Song Display: Overlay** selects the backup directly.
- **PC Music Volume** adjusts the matched player's Windows mixer session in 10% steps. This affects that app outside the game too; browser tabs may share a session. Windows master volume is unchanged.

Features depend on the player's Windows metadata and transport support. Not every app/tab supports title reporting, volume matching, skip, pause, or resume. Metadata normally updates once a second; playback commands run through a background helper. Unsupported operations appear in the log.

## Stability work and limitations

Included fixes developed through local testing:

- Native menu ownership/lifetime validation, row bounds, and build re-entry protection.
- Port Options Back handling preserves the native input-enable flag; confirmation presses are debounced.
- Asynchronous Windows settings dispatch avoids waiting on the window thread from guest callbacks.
- Atomic settings saves no longer add a blank line on every adjustment.
- OHCI DMA/guest interrupt synchronization for a captured USB descriptor race.
- Startup vblank acknowledgement and verified GPU-fence notification recovery.
- Bounded pushbuffer execution publishes actual GET, retains incomplete packets, and refreshes stale endpoints after ring reuse. Captured race-command replay checks cover the failure that prompted this work.
- Career-menu music restoration after a zero-volume configuration reset.
- Idle native message-prompt handling removes a clipped blue/white screen edge.

**Windows remains experimental.** These fixes do not establish that every crash or hitch is eliminated. A later long-running local session produced invalid GPU pushbuffer jump warnings; that case remains under investigation. Multiplayer and every career event have not been exhaustively validated.

The original **30 FPS simulation target** remains. 60/120 FPS modes, frame generation, and ray tracing are not included.

## Requirements and troubleshooting

- x64 Windows and a GPU/driver supporting the existing OpenGL renderer (OpenGL 3.3 or newer).
- Complete supported original Xbox Remix data. Modified/unsupported executables may not match converted code.
- A writable folder and free space for the extracted disc (about 4.6 GB).
- Normal setup is copy-only: no compiler, MSYS2, CMake or internet download. **Advanced → Rebuild from source** uses MSYS2/Clang tools and internet for missing dependencies. The launcher uses Windows' .NET Framework compiler, with no separate Node/Python launcher runtime download.

Start with **Verify setup** or **Support → Check setup**. For performance, try Performance and restart; high HUD scale also increases GPU work. For controllers, check Windows recognition and retain the supplied SDL2 runtime. For music, start the desired player once and check its Windows media controls.

Logs: `work-remix/logs`. Build backups: `work-remix/build-backups`. Inspect logs for personal paths/media titles before choosing to share them. Do not delete saves as a troubleshooting step.

## Building and project layout

See [Windows build instructions](BUILDING-WINDOWS.md) or the [Linux build guide](BUILDING-LINUX.md). ReDUB builds on the [mcdub3-nx project](https://github.com/laureladastra/mcdub3-nx) and the [xboxrecomp toolkit](https://github.com/sp00nznet/xboxrecomp).

- `.portable/Launcher.cs`: launcher source.
- `.portable/Build-Launcher.ps1`: compile launcher/create shortcuts.
- `.portable/Build.ps1`: folder import, advanced build/repair, and prerequisite checks.
- `.portable/Xdvdfs.cs`: in-house Xbox disc reader used by **Install**.
- `src/windows_settings.c` and `src/linux_settings.c`: shared native options with per-platform persistence and display/input hooks.
- `src/windows_media.c` and `tools/windows/now_playing.ps1`: optional Windows media integration.
- `work-remix/build-windows-x86_64/dist`: installed game/settings.

Use `-NoInstall` to stage a game build while playing. Save and close before installation. The launcher does not automatically update source or publish to GitHub.

## Credits and licensing

ReDUB builds on [laureladastra/mcdub3-nx](https://github.com/laureladastra/mcdub3-nx), [sp00nznet/xboxrecomp](https://github.com/sp00nznet/xboxrecomp), and their contributors. SafeHouse inspired the launcher layout; ReDUB has its own implementation. SDL, FFmpeg, and other dependencies retain their licenses/notices. See repository license files and upstream documentation.

Game names, trademarks, executable code, soundtrack, and original artwork belong to their rights holders. Artwork provenance is documented separately; inclusion does not grant redistribution rights for third-party material. The source-code license does not license game data or texture packs.

Anti-aliasing offers FXAA Low, standard FXAA, and FXAA High during driving; menus and loading screens bypass it to preserve sharp text, and DLSS does not stack it. MSAA is not included because it requires multisampled render targets, depth handling, and resolve support; SMAA is not included.
