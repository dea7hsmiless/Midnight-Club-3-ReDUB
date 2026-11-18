# ReDUB on Linux

## Ready-to-run AppImage

The Linux package is **ReDUB 0.2.4**, distributed on the existing Windows/Linux release page. Download `ReDUB-0.2.4-x86_64.AppImage`, make it executable, and run it with `--game-dir "/path/to/extracted/game"`. It remembers the folder. An explicit `MC3_GAME_DIR` overrides saved pointers. A `redub.game-location.txt` beside the AppImage is also supported; relative paths in it are resolved beside that file. A plain `ReDUB-0.2.4-x86_64.tar.gz` carries the same AppDir for systems that cannot run an AppImage.

The AppImage requires **x86_64, glibc 2.39+, X11/XWayland, and OpenGL 3.3**. Clean-container startup passed on Ubuntu 24.04 and Debian 13 with read-only game data; WSLg desktop gameplay/display was accepted by the user. It bundles 47 shared libraries, including SDL2, D-Bus, and Bink-only FFmpeg. Graphics drivers, GBM/DRM, and glibc are supplied by the host desktop. It includes only the three tracked ReDUB logo textures, not original game data or a third-party texture pack.

```sh
chmod +x ReDUB-0.2.4-x86_64.AppImage
./ReDUB-0.2.4-x86_64.AppImage --game-dir "/path/to/extracted/game"
# FUSE-free fallback:
APPIMAGE_EXTRACT_AND_RUN=1 ./ReDUB-0.2.4-x86_64.AppImage
# Keep config/data/cache/state beside the package instead of XDG:
./ReDUB-0.2.4-x86_64.AppImage --portable "./ReDUB-data"
# Library and display diagnostics:
./ReDUB-0.2.4-x86_64.AppImage --diagnose
```

`--fullscreen 0|1|2`, `--resolution auto|WxH`, `--scale N`, `--frame-rate 30|60`, `--save-dir`, `--settings` and `--hd` are also accepted. Flag values become the matching game environment settings for that launch; `--` passes the rest through.

| User files | Default location |
| --- | --- |
| Saves and Xbox title metadata | `~/.local/share/redub/saves` |
| Port Options | `~/.config/redub/mcdub3remix_env.txt` |
| Remembered game folder | `~/.config/redub/game-location.txt` |
| Renderer caches | `~/.cache/redub` |
| Current/previous run logs | `~/.local/state/redub/latest.log`, `previous.log` |

The corresponding XDG variables are respected. `MC3_SAVE_DIR`, `MC3_SETTINGS_FILE`, and `RECOMP_GL_HD_DIR` override individual locations. Existing Windows saves are not automatically imported. Linux maps Xbox Partition1/TDATA to `saves/TitleData`, UDATA to `saves/UserData`, and CACHE to `saves/Cache`, so the extracted game folder can stay read-only. Optional HD lookup is: explicit override, `hd` beside the AppImage, user-data `redub/hd`, then bundled ReDUB logos.

Native Wayland is unsupported after observed plugin/protocol failures; the launcher selects X11/XWayland. Vulkan/DLSS and experimental 120 FPS are excluded; the 60 FPS option is available and 30 FPS remains the default. The native Port Options entry/title uses `Port Options`, while setting labels retain the game's existing capitalization. Linux song text is transliterated rather than using Windows' full Unicode fallback. Real desktop audio and MPRIS player behavior remain unverified in the WSLg environment, whose PulseAudio socket was absent; decoded audio tests used dummy output.

## Rebuild the AppImage

First generate Remix game code locally using your legally owned files, then run:

```sh
GAME=remix make lift
bash tools/build_appimage.sh
```

Docker is required. Source and generated game code are mounted read-only; build output goes to `work-remix/appimage/release-out`. The recipe pins Ubuntu and upstream tool/source hashes, records the source commit, bundles the ELF dependency closure and notices, validates metadata, runs launcher checks, and downloads corresponding dependency sources. `MC3_GEN_DIR` and `REDUB_APPIMAGE_WORK_DIR` override the generated-code/build folders. See [the packaging guide](tools/appimage/README.md). No original game files or generated game C are uploaded by this process.


ReDUB builds from the same game and Port Options source on Windows and Linux. The Linux AppImage uses OpenGL as its supported renderer and includes the native Xbox Port Options menu, graphics presets, render scale, FXAA, filtering, sharpening, FPS overlay, gamma, brightness, HUD texture scale, fullscreen/VSync, resolution, controller vibration, notification cleanup, and the ReDUB HD logo pack. Graphics controls use the same `mcdub3remix_env.txt` keys as Windows; controls labeled `(RESTART)` take effect after a restart. DLSS and the experimental Vulkan path are not included in the Linux AppImage.

Linux PC Music uses the session D-Bus and the player's MPRIS interface for track metadata, previous/next, pausing in known game menus and resuming on return to driving. The supported player must publish MPRIS; volume control is best-effort because some players do not expose a writable `Volume` property. External music stays in the desktop audio mix while the game silences its radio. No audio is captured, copied, or recorded.

The native build links `libdbus-1` when its development files are installed. On Debian/Ubuntu, `tools/setup_linux.sh` installs `libdbus-1-dev`. If D-Bus development files are absent, PC Music options display as unavailable. WSLg may not provide a usable session bus or media-player service; test media control on a regular Linux desktop for parity confirmation. Linux controller rumble depends on SDL and the controller driver.

## Requirements

- A 64-bit Linux distribution supported by `xboxrecomp/tools/linux/install_deps.sh` (Ubuntu/Debian, Fedora, or Arch-based).
- An original Xbox **Midnight Club 3: DUB Edition Remix** dump that you are legally authorized to use. PS2 images and Switch/360 packages are not build inputs.
- Network access for system packages and the first-time game-code conversion.
- A GPU/driver with OpenGL 3.3 or newer for the default renderer.

Install prerequisites:

```sh
GAME=remix bash tools/setup_linux.sh
```

The optional Vulkan build remains available for developers with `VULKAN=1 make build`, but it is not the supported Linux release path yet.

## Build and run

Point `MC3_GAME_DIR` at your extracted game folder containing `default.xbe`, then build:

```sh
export GAME=remix
export MC3_GAME_DIR="$HOME/Games/MC3-Remix"
make build
make run
```

The first `make lift` converts the supplied Xbox executable to native C and stores generated code under `work-remix/`. Game assets, generated C, build outputs, and saves are not included in the source package. For Vulkan, use `VULKAN=1 make build` and `VULKAN=1 make run`.

A local texture pack can be placed under `hd/textures/`; it is personal game data and excluded from source control by default. 60/120 FPS simulation and frame generation are not enabled; core game systems use a 30 FPS fixed-step path.

## Renderer notes

OpenGL and Vulkan build as separate Linux renderer choices. Vulkan uses native Vulkan presentation and the same final FXAA/sharpening/gamma/brightness/music-overlay pass as Windows. Renderer output still needs visual validation on each Linux driver; WSLg's D3D12-backed graphics path is not a substitute for testing a native Linux GPU driver. DLSS remains Windows-only.
