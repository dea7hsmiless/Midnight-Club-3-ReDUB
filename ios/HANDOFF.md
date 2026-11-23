# Startup repair � build 4 (2026-11-16)

User reports splash then immediate exit; iOS version/signing method and .ips report
still pending. Binary SDK is 27.1 but build 3 had no scene manifest or launch-screen
entry. Added real UIScene startup, scene-associated launcher + SDL windows,
UILaunchScreen dictionary, and early startup exception logging. Source changes
are in ios_scene.m, ios_launcher.m and ios/prepare_sdl_scenes.py. SDL original
sources are backed up with .redub-before-scenes suffix; applied diff retained at
ios/sdl-scenes.patch. build_launcher.py now applies patch and rebuilds SDL archive.
Packages are *-startup-v4-unsigned.ipa. No device validation or crash report yet.

---

# Latest update � iOS launcher (2026-11-16)

Icon update: both launcher IPAs now use ios/assets/redub-icon.ico from the
user-supplied Desktop artwork. Bundle build version 3; opaque RGB icon PNGs
include iPhone 180px and iPad 167px. The original baseline IPA is unchanged.

See LAUNCHER.md for the new launcher build, testing, package names and limitations.
New sources: src/ios_launcher.m, src/ios_xdvdfs.c/.h. main.c now shows the native
launcher before runtime startup; ios_host.c refreshes settings after Play and
captures stdout/stderr in Documents/mcdub3remix-launch.log. The pre-existing
!defined(TARGET_OS_SIMULATOR) guards were corrected to !TARGET_OS_SIMULATOR:
Apple defines this macro as 0 on device. The build script now asserts that
main.o exports SDL_main and references ios_launcher_run, and links libSDL2main. The original full IPA
is preserved. The phone installs/extracts/verifies disc data; it does not compile
native code. Both new IPAs require signing and an actual device test.

The older checkpoint below is historical; its SDL launcher proposal was replaced
with UIKit controls to reuse the existing framework and native Files integration.

---

# ReDUB iOS port — checkpoint / handoff (2026-11-16)

> Read this first when resuming. Everything needed to continue is in here.
> Project: `D:\Launchbox\Games\Recomps\Apps\mcdub3-nx`
> Goal: iOS port of the Xbox recomp (Midnight Club 3: DUB Edition Remix),
> built 100% on Windows, shipped as an **unsigned** IPA the user signs with
> their own dev certs (Sideloadly / AltStore custom-cert mode). Device:
> iPhone 16 Pro Max, latest iOS.

## CURRENT STATE: COMPLETE — unsigned IPA built

- **IPA:** `ios\dist\mcdub3remix-unsigned.ipa` (4.8 GB, ZIP_STORED, 920 entries)
  - `Payload/mcdub3remix.app/mcdub3` — 30.4 MB arm64 Mach-O (min iOS 16.0)
  - `Payload/mcdub3remix.app/game/` — full disc (from `game-remix/`, 913 files, 4.4 GB)
  - `Payload/mcdub3remix.app/save/` — empty dir (T:/U:/Z: writes land there)
  - `Payload/mcdub3remix.app/Info.plist` + 5 loose PNG icons (60/76/120/152/167)
  - Bundle id `com.ladastra.mcdub3remix`, `MinimumOSVersion 16.0`, `UIDeviceFamily [1,2]`,
    landscape-only, `UIRequiresFullScreen`, `LSRequiresIPhoneOS`
- **Entitlements (separate file):** `ios\dist\mcdub3remix.entitlements` — only
  `com.apple.developer.kernel.increased-memory-limit` = true. The signing tool
  applies it; the IPA itself stays unsigned.
- **Build script:** `%TEMP%\opencode\ios_build_app.py` (regenerates .app + IPA from
  `%TEMP%\iosbuild\mcdub3` + `game-remix/`; modeled on NXNative's `build_app.py`).
  Copy it somewhere durable if you want it beyond this machine's TEMP.
- Binary + all objects: `%TEMP%\iosbuild\` (main/ac97/bink/manual/difftest/ios_host.o,
  `gen\recomp_*.o` × 89, `xr\*.o` × 54), SDL2: `%TEMP%\iossdl\libSDL2.a` +
  `libSDL2main.a` + `libSDL2_test.a` (SDL 2.30.9 built for iPhoneOS).

## NEXT STEPS (resume here)

1. User signs the IPA with their own dev certs (Sideloadly/AltStore custom certs),
   applying `mcdub3remix.entitlements`, and sideloads to the iPhone 16 Pro Max.
2. First on-device run — things to watch:
   - SDL UIKit quirks: the game runs on a thread (host_main.c service loop holds the
     main thread for SDL). iOS may still have opinions about window creation timing.
   - GLES context reality: iOS caps GLES at **3.0**. nv2a_gl.c requests 3.2 → 3.1 → 3.0
     (fallback chain) and the ES shader header picks `#version 300 es` when
     `s_es_minor == 0`. If shaders still fail, `s_trace`/`RECOMP_*` env logging in
     nv2a_gl prints the rejected source.
   - Path layout: game dir resolves via `$HOME/game` (see main.c iOS branch).
     If HOME isn't the container root on device, `MC3_GAME_DIR` in the env file
     (`<HOME>/Documents/mcdub3remix_env.txt`) overrides.
   - Audio: CoreAudio route through SDL2 — untested on device.
3. If it crashes/blank-screens: log ring lives in `ios_host.c`; `mcdub3remix_env.txt`
   is the settings file; iterate with RECOMP_*/MC3_* flags (defaults in
   `config\remix\game.h` `MC3_GAME_DEFAULTS`).

## Toolchain (paths matter — all verified working)

- clang: `C:\Users\Donte\AppData\Local\Programs\Swift\Toolchains\6.4.1+Asserts\usr\bin\clang.exe`
- SDK: `E:\AI\iosc\xcode\Platforms\iPhoneOS.platform\Developer\SDKs\iPhoneOS.sdk`
- Linker: only `E:\AI\iosc\tools\ld64.lld.exe` (LLVM 23) works — Swift toolchain's own
  lld rejects iOS targets. Use `-fuse-ld=lld` with `-BE:\AI\iosc\tools\` (also supplies llvm-ar).
- CMake cross file (working): `ios\ios.toolchain.cmake` (built SDL2 with it).
- SDL2 source ref: `C:\Users\Donte\Documents\Codex\ios-toolchain-references\SDL2`
  (2.30.9). NXNative pattern ref:
  `C:\Users\Donte\Documents\Codex\ios-toolchain-references\NXNativeWindows\win\build_app.py`.

### Compile command (working, PowerShell)

```powershell
$proj = "D:\Launchbox\Games\Recomps\Apps\mcdub3-nx"
$sdk = "E:\AI\iosc\xcode\Platforms\iPhoneOS.platform\Developer\SDKs\iPhoneOS.sdk"
$clang = "C:\Users\Donte\AppData\Local\Programs\Swift\Toolchains\6.4.1+Asserts\usr\bin\clang.exe"
$incDirs = @("$proj\src","$proj\config\remix","$proj\work-remix\gen",
  "$proj\xboxrecomp\include","$proj\xboxrecomp\src","$proj\xboxrecomp\src\platform",
  "$proj\xboxrecomp\src\kernel","$proj\xboxrecomp\src\d3d","$proj\xboxrecomp\src\nv2a",
  "$proj\xboxrecomp\src\audio","$proj\xboxrecomp\src\input","$proj\xboxrecomp\src\video",
  "$proj\xboxrecomp\src\apu","$proj\xboxrecomp\src\usb","$proj\xboxrecomp\src\nv2a_gl",
  "C:\Users\Donte\Documents\Codex\ios-toolchain-references\SDL2\include") | % { "-I$_" }
& $clang -target arm64-apple-ios16.0 -isysroot $sdk @incDirs -D_GNU_SOURCE -O2 -w `
  "-DMC3_APP=\`"mcdub3remix\`"" "-DMC3_VERSION=\`"dev\`"" "-DMC3_BUILD_ID=\`"local-ios\`"" `
  -c <src> -o <obj>
```

**PowerShell quoting trap (cost us an hour):** `-DMC3_VERSION="""dev"""` collapses
to a bare unquoted `dev` → "use of undeclared identifier 'dev'". The working form is
`"-DMC3_VERSION=\`"dev\`""`. Never go back to the triple-quote form.

### Link command (working)

```powershell
& $clang -target arm64-apple-ios16.0 -isysroot $sdk "-BE:\AI\iosc\tools\" -fuse-ld=lld `
  "-Wl,--error-limit=0" -o mcdub3 <147 objects> libSDL2.a `
  -framework CoreAudio -framework AudioToolbox -framework AVFoundation `
  -framework CoreMotion -framework GameController -framework CoreHaptics `
  -framework Metal -framework Foundation -framework UIKit -framework OpenGLES `
  -framework CoreBluetooth -framework QuartzCore -framework CoreGraphics `
  -lobjc -lz -lm
```

- Object set = `iosbuild\*.o` (6) + `iosbuild\gen\*.o` (89) + `iosbuild\xr\*.o` (54)
  **minus** `nv2a_nv2a_pb_replay.o` and `nv2a_nv2a_pb_test.o` → 147 total.
- **Excluded on iOS:** `nv2a_pb_replay.o`/`nv2a_pb_test.o` (xemu-capture replay tool;
  nothing in the runtime references them; their dangling externs `g_fe_cursor` /
  `fe_menu_get_pb_state` are stubbed in `ios_host.c`).
- `-lobjc -lz -lm` resolve SDL2's UIKit-video/HID internals.

## Source changes made (all applied, all compiling)

### `src\main.c`
- iOS branch (`TARGET_OS_IPHONE && !TARGET_OS_SIMULATOR`):
  - `void ios_boot(void);` declared, `MC3_DEFAULT_GAME_DIR "game"`,
    `MC3_DEFAULT_SAVE_DIR "save"` (saves inside the app container).
  - `#include <SDL_main.h>` moved into a combined Android+iOS `#if` so `main`
    becomes `SDL_main` — `libSDL2main.a`'s `SDL_uikit_main.c` defines `main()` and
    calls `SDL_UIKitRunApp(argc, argv, SDL_main)`. Without this: duplicate `_main`.
  - `main()` calls `ios_boot()` before `xbox_host_main_run(...)` (guard at the call
    site **must include** `!defined(TARGET_OS_SIMULATOR)` to match the declaration).
  - Game dir resolution: relative default resolved through `$HOME` (iOS gives the
    container as HOME; SDL does not chdir). `MC3_GAME_DIR` env wins if set.

### `xboxrecomp\src\nv2a_gl\nv2a_gl.c`
- `RECOMP_GL_GLES`: on iOS defaults **ON** (`e[0]=='0'` opts out for testing).
- Context request chain: ES 3.2 → 3.1 → (iOS) **3.0**, `s_es_minor` tracks what
  landed. The 3.0 fallback block is inside the `if (!s_ctx && nv2a_gl_es)` nest,
  behind `#if __APPLE__ && TARGET_OS_IPHONE`.
- `es_header()`: iOS returns `#version 300 es` when `s_es_minor == 0` (3.1/3.2
  lines preserved for other platforms / future ANGLE-style drivers).
- Verified `gl_vsh.c`/`gl_psh.c` shaders use only ES3.0-safe constructs in
  330-core syntax (`texture()`, `layout(location=…)`, no textureLod/textureProj).

### `src\ios_host.c` (new file, compiles OK)
- Env-file load/apply (`<HOME>/Documents/mcdub3remix_env.txt`), settings atomics
  implementing the `windows_port_setting_change/_label` contract, `g_fe_cursor`
  + `fe_menu_get_pb_state` stubs (for the excluded pb_replay objects), log ring.

### Pre-existing (from earlier sessions, unchanged this session)
- `xboxrecomp\src\platform\host_main.c`: `__APPLE__` branch (main-thread service
  loop) covers iOS as-is.
- `xboxrecomp\src\platform\win32_compat.c`: iOS-fixed earlier
  (`IOS_NO_MACH_VM_MAP` hint-and-compare fallback).

## Key design decisions (do not re-litigate without reason)

| Decision | Choice |
|---|---|
| Min iOS | **16.0** (was 15.0; user is on latest iOS / A18 Pro) |
| Renderer | **nv2a_gl GLES path** — not Vulkan/MoltenVK; iOS GLES caps at 3.0 |
| Shader dialect | GLSL 330-core source, `#version 300 es` header on iOS |
| D3D | `d3d8_null.c` (like Switch/Android): draws go through the NV2A pushbuffer executor |
| Settings | env-file commits + `setenv` live, atomics in `ios_host.c` |
| Disc layout | ships **inside the bundle** at `game/`; saves at `save/` (T:/U:/Z: → `kernel_path.c` map) |
| Signing | **build stays unsigned**; user's sideload tool applies entitlements + their signature |
| pb_replay/pb_test | excluded from iOS link; externs stubbed in `ios_host.c` |

## Gotchas / hard-won knowledge

- **The `edit` tool fails on this repo's CRLF source files.** Use PowerShell
  `[IO.File]::ReadAllText` / `.Replace` / `WriteAllText` with `` `r`n `` strings
  (see session history for working patterns). The `edit` tool *does* work on
  LF files like `main.c` — test before assuming either way.
- `getenv` on iOS: game dir default `"game"` is relative; resolve via `$HOME`.
- `MinimumOSVersion 16.0` but objects were built at both `-ios15.0` and `-ios16.0`
  (compatible — link ran clean at 16.0).
- `cover-art-remix.jpg` referenced by `config\remix\app.env` is **not in the repo**
  (personal, per comment in app.env). The IPA icons were scaled from
  `assets\game-logo.jpeg` instead. If the real cover art surfaces, regenerate
  the IPA with the build script pointing at it.
- 64-bit xattrs/`PkgInfo` not included — not required for sideload-tooled IPAs.

## Project layout (what's where)

```
mcdub3-nx\
  config\remix\        app.env / game.env / game.h / xbe_editions.h / xbe_facts.h / edition_variants.json
  game-remix\          the disc: default.xbe + assets (4.4 GB, 913 files) → bundled as game/
  src\                 main.c (entry), ios_host.c (iOS settings/env/log glue)
  xboxrecomp\src\      kernel\ (22) apu\ (6) audio\ (2) nv2a\ d3d\ input\ (4) usb\ (2)
                       video\ (3) nv2a_gl\ (6) platform\ (host_main.c, win32_compat.c)
  ios\                 ios.toolchain.cmake, dist\ (IPA + entitlements)
  assets\              game-logo.jpeg, preview.png, switch.png
  switch\              app.env (shared keys), build.sh, nsp.sh
  work-remix\gen\      89 generated recomp_*.c files (compiled to %TEMP%\iosbuild\gen\)
```

## Open questions / risks (for the next session)

1. Does SDL2 2.30.9's UIKit backend behave with a non-main-thread game loop on
   modern iOS? (host_main.c service loop is designed for exactly this, but
   iOS 26 on-device behavior is unverified.)
2. GLES 3.0 context on device: Apple's GLES is deprecated-but-present; context
   creation through SDL may need `SDL_WINDOW_OPENGL` + no depth/stencil surprises.
   The 3.2→3.1→3.0 chain handles refusal gracefully.
3. Audio latency via CoreAudio through SDL — untested.
4. Performance: MC3_GAME_DEFAULTS (merge on, per-draw off, CUBO on) were tuned
   for Switch; A18 Pro is a different beast — revisit after first device run.
5. Memory: the increased-memory-limit entitlement helps the 4.4 GB in-bundle
   disc + 64 MB title heap; watch for jetsam on first run.

## NEXT BUILD (user request, 2026-11-16): ISO-mounting launcher

User wants the next iOS build as a **launcher**, not a 4.8 GB bundled IPA:
- The game *code* (the 89-file recomp + runtime) stays precompiled in the IPA —
  a true on-device recomp would need JIT entitlements (jailbreak only), off the table.
- The **disc is NOT bundled**. User drops their own MC3 ISO into the app's
  Documents folder; the app extracts/verifies it on-device, then plays.
- The current bundled full IPA stays as the user's own test build — don't delete it.

### Design (agreed with user)

**The iOS launcher mirrors the desktop ReDUB Launcher** (`.portable\Launcher.cs`,
WinForms, source of truth for layout/behavior) — same dark theme, same page
structure, same settings list — rendered with SDL2 instead of WinForms:

- **Theme** (from Launcher.cs colors): bg `#080D1A` (8,13,26), nav rail `#050912`
  (5,9,18), cards `#0F172A` (15,23,42), muted text `#99A5BC` (153,165,188),
  primary/accent red `#EE3E50` (238,62,80), selected-highlight red `#F55060`
  (245,80,96), footer `#0E1627` (14,22,39).
- **Layout**: left nav rail (Game / Install / Settings, Support+About pinned
  bottom), main content pane, footer status bar with the 1.5 s status refresh
  (desktop uses a `Timer` at 1500 ms → iOS: poll in the SDL event loop).
- **Pages** mapped 1:1 from the desktop:
  - `Game` (home): brand panel (hero.jpg + logo.png from `launcher\art\`,
    cover-fill zoom + gradient wash, drawn through SDL textures) over the
    Play / Verify / Game-folder buttons row. Play is primary red.
  - `Install`: "Choose your game source" — on iOS the file picker is the
    Files-app document picker (`UIDocumentPickerViewController` for .iso);
    source TextBox becomes a chosen-path label. "Install and build" button
    → extract ISO → verify → ready state.
  - `Settings`: category tabs Display / Graphics / Controls / Music /
    Diagnostics / Launcher → same `Choice()` rows writing the same
    MC3_*/RECOMP_* keys to the same env file (`<HOME>/Documents/
    mcdub3remix_env.txt`), so settings transfer from desktop unchanged.
    Desktop-PC-only rows are dropped: PC Music (Windows media controls),
    exclusive fullscreen, wheel axes, resolution list (iOS is always
    fullscreen at device resolution). Keep: render scale, HUD texture
    quality, FXAA, aniso, sharpening, gamma, brightness, vibration, FPS
    overlay, frame queue, HUD scale.
  - `Support`/`About`: log folder button → iOS shows the log file(s) /
    share sheet; About page reused as-is.

**Renderer for launcher UI**: plain SDL2 (the same library already in the
build) — SDL_Renderer + SDL textures for the art, TTF text via SDL_ttf (one
new dependency, build it with the same `ios.toolchain.cmake`; can also render
text to textures at load) — NOT UIKit/Storyboard, so the launcher runs as the
same single executable, plain C, in the existing `ios_host.c`/`main.c` flow.

**Launcher flow on iOS:**
1. `main()` → `ios_boot()` → `launcher_main()` (new, SDL window/renderer +
   event loop at 60 fps, nav state machine, touch buttons = the RedubButton
   hit-test rects).
2. `Ready()` = `<HOME>/Documents/game/default.xbe` exists (same
   `InstallationReady` check as desktop, adapted paths: `mcdub3remix.exe` →
   bundled `mcdub3` binary, `SDL2.dll` → linked-in, `game-remix/default.xbe`
   → `Documents/game/default.xbe`).
3. `Play` (or "Install" if not Ready): not-ready → Install page → pick .iso
   (document picker, import-copy into Documents) → verify disc (FN of
   `default.xbe` against `config\remix\xbe_editions.h`/`xbe_facts.h` — same
   runtime detection the game does at boot) → extract full XDVDFS to
   `Documents/game/` (progress bar on the Install page, same bar/percent UI)
   → Ready → home shows Play.
4. `Play` → quit the SDL launcher UI loop → existing game boot path (reuses
   the same binary; game takes over the window as today). No second
   executable — launcher and game are one binary, so the 30 MB binary
   contains both.

**XDVDFS reader**: new `ios_xdvdfs.c` — Xbox ISO (XDVDFS, 2048/0x800-byte
sectors, 14-byte dir entries) read + extract + verify. Reference:
`extract-xiso`, xemu disc handling, `xboxiso`. Plain C, built with the same
iOS clang command. **Verify the on-disk format details from a reference when
implementing — don't trust memory for the layout.**

**Info.plist additions** (ISO drop-in via Finder/iTunes too):
- `UIFileSharingEnabled` = true
- `LSSupportsOpeningDocumentsInPlace` = true
- `UISupportsDocumentBrowser` = true (Files app visibility)
- `CFBundleDocumentTypes` entry for public.iso-image so a chosen .iso
  offers "Open in ReDUB" → copies into Documents → goes to Install page.
- Remove `game/` from the bundle → IPA ~35 MB.

**Files-app path note**: iOS seals the bundle; user data lives in
`<HOME>/Documents`. The in-app document picker
(`UIDocumentPickerViewController`) gives the app a security-scoped copy of
any picked .iso, no entitlement needed beyond the plist keys above.

**Art**: reuse `launcher\art\hero.jpg` (414 KB) + `logo.png` (741 KB) +
`library\background-1920x620.jpg` — bundle them in `Assets/` (loose files,
not asset catalog — same pattern NXNative's build_app.py uses for icons).
Bundle size stays small.

**Settings parity**: desktop Launcher.cs writes settings to a file read by
the game at boot (`windows_port_setting_*` contract). iOS `ios_host.c`
already implements that contract with the same env-file format — the launcher
writes the same keys, so a settings file exported from desktop ReDUB drops
straight in as `mcdub3remix_env.txt`.

**Storage note to give the user**: ISO (4.4 GB) + extracted (4.4 GB) ≈ 9 GB
free needed once to install the disc; the ISO copy can be deleted after.

### What the launcher does NOT include (deliberate)

- No Vulkan/DLSS/VRAM/PS2 rows (desktop-only features).
- No "Build game code" — the recomp is compiled into the binary at build
  time (JIT on-device is a jailbreak-only path).
- No shortcut creation / registry / doctor scripts (Windows-only).
- No wheel mapping beyond a simple on/off (no axis remap UI on iOS).

### Why not a true on-device "compile"

The recomp (x86→ARM translation) runs as a build-time tool on the dev
machine; its output IS the game binary. Running it on-device needs
`MAP_JIT`/unsigned-executable pages — forbidden on stock iOS even with
dev certs (only `com.apple.developer.kernel.*`-style entitlements via
jailbreak/ldid). The launcher gives the same user-facing result
(bring-your-own-disc) without that wall. User confirmed this choice.