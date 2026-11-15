# ReDUB on Linux

ReDUB builds from the same game and Port Options source on Windows and Linux. Linux OpenGL and native Vulkan support the native Xbox Port Options menu, graphics presets, render scale, FXAA, filtering, sharpening, FPS overlay, gamma, brightness, HUD texture scale, fullscreen/VSync, resolution, controller vibration, and the notification cleanup. Both use the Linux settings backend and final presentation pass. Graphics controls use the same `mcdub3remix_env.txt` keys as Windows; controls labeled `(RESTART)` take effect after a restart. DLSS is not available on Linux; its host integration is currently Windows-only.

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

For the optional Vulkan build dependencies, add `--vulkan`.

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

## WIP binary packages

The GitHub WIP release includes separate `OpenGL` and `Vulkan` x86_64 archives. Extract one archive, place the legally owned Xbox Remix files in `game/` (or set `MC3_GAME_DIR`), and run `tools/run_linux.sh`. The package contains the native executable, launcher script, documentation, and optional ReDUB HD artwork only; it does not contain game code, assets, music, or saves. Install the normal SDL2/OpenGL or Vulkan, FFmpeg, and D-Bus runtime packages supplied by your distribution.
