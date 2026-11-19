# Release notes

## v0.2.4 - 2026-11-18

**Setup copies your disc and plays. A normal install never compiles.**

### Install (no compiler)

- The launcher's install page is now **Install ReDUB**. Choose an original Xbox Remix ISO/XISO or an extracted folder; the disc is copied into `game-remix/` beside the launcher.
- ISOs are read by a new in-house XDVDFS reader in the launcher. It verifies `default.xbe` against the supported region7/EUR/USA Remix editions before keeping the files. No third-party extractor is bundled.
- Import stages in `game-remix.importing`, checks the disc, then swaps it in. A failed or cancelled import leaves the previous game files, saves and settings alone. Replacing game files asks first.
- Import shows progress; **Cancel import** or Esc stops it without touching the existing game files. Closing the launcher mid-import offers to cancel and exit.
- The release zip already contains the game executable and its runtime. Play starts it directly; MSYS2, CMake and internet downloads are not used for normal setup.

### Advanced

- Build/Repair moved to **Settings -> Advanced -> Rebuild from source**, with the MSYS2, about 10 GB and 20+ minute warning. Repair build and prerequisite checks live there too.
- `Build.ps1 -Mode Import` copies an extracted folder and stops before MSYS2. Doctor no longer treats MSYS2 as required for a shipped install.
- Launcher command line: `--import <folder-or-iso>`, `--check`, `--play`, `--build` (advanced). Logs go to `work-remix/logs/launcher-cli.log`.

### Linux

- `ReDUB-0.2.4-x86_64.AppImage` and `ReDUB-0.2.4-x86_64.tar.gz` are built from the same source in a pinned Ubuntu container. The tarball is a plain AppDir for systems that cannot run an AppImage.
- AppRun accepts `--game-dir`, `--portable DIR`, `--save-dir`, `--settings`, `--hd`, `--fullscreen`, `--resolution`, `--scale`, `--frame-rate`, `--diagnose`, `--version`, `--help` and `--` passthrough. XDG paths stay the default; portable mode is opt-in.
- The package version comes from the repository version rather than a hardcoded AppImage number.

### iOS (unsigned sideload; beta validation)

- `mcdub3remix-0.2.4-garagefix-unsigned.ipa` is the official-bundle-id 0.2.4 build (`com.ladastra.mcdub3remix`) with the garage render-target feedback fix: a stage that samples the surface being drawn into now uses a `glCopyTexSubImage2D` snapshot. `RECOMP_GL_FEEDBACK=0` restores the old behavior. Unsigned sideload only.
- User-confirmed on device: the garage graphics glitch is gone. 16 XDVDFS reader tests and `verify_packages.py` pass. Because this uses the official bundle id, it installs alongside a previous `.beta` build rather than replacing it; Documents and saves are separate.
- iOS remains outside the Git tree for this release; `ReDUB-iOS-0.2.4-source.zip` is attached so the IPA can be rebuilt. The side-by-side beta IPA is not included.

### Android (new; sideload APK, project signing key)

- `ReDUB-0.2.4-Android-universal.apk` carries arm64-v8a, armeabi-v7a and x86_64 in one APK. Android 13+ and OpenGL ES 3.1+; sideload only, signed with the project's own key (certificate SHA-256 `ecd472d0d6cb4d676e5f4e5d583649d20e69eacbe8d46c8f916a40c13cec0272`).
- The launcher follows the iOS launcher: rail of pages (Game / Install / Settings / Support / About), hero art and logo, the same palette, and the same 35 settings rows in the same groups with expandable sections and graphics presets. Values are written to the app's `mcdub3remix_env.txt`, the file in-game Port Options writes, so launcher and game agree.
- **Install from an ISO on the device.** The Install page takes an original Xbox Remix ISO through Android's file picker and extracts it with an in-house XDVDFS reader. Extraction stages in `game-iso.importing`, verifies `default.xbe`, then swaps into `game-iso`; the previous install is backed up and restored if the swap fails, so a failed import cannot damage a working install or its saves. An extracted `default.xbe` folder is still accepted.
- The custom ReDUB boot/menu logo set is bundled and extracted on first run; a user copy beside the game folder still wins. The logos now actually appear on OpenGL ES: replacement textures used to be uploaded as S3TC, which ES does not have, so the renderer disabled the whole pack; they are decoded to RGBA and uploaded uncompressed when the compressed upload is refused.
- Tilt steering with recentring at race start (stick / tilt / stick + tilt, response, dead zone, tilt range, invert), touch haptics, controller rumble, and an on-screen pad that hides itself when a controller connects.
- 60 FPS mode as on PC: one flip retirement per vblank in gameplay, menus and pause at 30, the race step halved to 1/60 only while driving, with real-time catch-up so a late frame cannot slow the game down.
- The Android app keeps its own version key (`ANDROID_VERSION=0.1.0`); the runtime, settings and saves are the same project as the PC packages.

### iOS — 60 FPS build (unsigned sideload; replaces the 0.2.4 install)

- `ReDUB-0.2.4-iOS-mobile-unsigned.ipa` holds 60 FPS on device. A feedback stage that used `glReadPixels` plus CPU strip replication on sampled-surface changes now copies on the GPU to a scratch texture and blits the last column/row/corner back, removing a per-frame synchronisation cost.
- The 60 FPS race step now compensates in real time when a frame is late; the earlier build ran the simulation at two-thirds speed at about 40 FPS, which read as the whole game slowing down.
- Tilt: corrected landscape sensor axes, a steadier default range, stronger smoothing and a small dead zone by default (the phone is held in both hands and cannot be re-centred by thumb). Pedal layouts and touch haptics as on Android.
- The same custom boot/menu logo set is bundled in the IPA.
- Renderer fixes from PC: the minimap scissor seam (fractional horizontal scale left a one-pixel line) and the HUD upscale shader's alpha (transparent HUD edges no longer ring into faint square or line fringes).
- Same bundle id as the garagefix IPA, so it **replaces** that install; Documents and saves are kept. Unsigned — sign with your own certificate. The garagefix IPA remains the conservative choice for a garage-only install.

### Known issues

- iOS: a few-hundred-millisecond hitch can still occur at the race -> menu transition (the game loads `flash/transition.xbck` and runs a worker thread; renderer instrumentation shows it is not GPU work). Builds with `MC3_PERF>0` now log which side owns the time.
- iOS: occasional 51-59 FPS dips rather than a perfectly locked 60.
- Android: logo replacement, haptics and tilt feel still need on-device confirmation. BlueStacks 5 (Android 13) runs the launcher and completes an ISO import, but the game itself does not boot there (it spins after loading its asset files); the Android SDK emulator with -gpu host does render it. Neither is a phone.
- Android: no store distribution; sideload only, and Play Protect will ask about it.

### Packaging

- Windows zip: launcher, setup source and the prebuilt game in `work-remix/build-windows-x86_64/dist/`.
- Existing 0.2.3 Windows/Linux saves, settings and game files remain compatible.

## v0.2.3 - 2026-11-17

**60 FPS becomes an option, and the release stops looking like malware to virus scanners.**

### 60 FPS (optional; 30 FPS stays the default)

- The launcher's **Graphics → Frame rate** row and **Options → Port Options → FRAME RATE** (restart to apply) choose between **30 FPS (Xbox)** and **60 FPS**. 30 FPS remains the default and behaves exactly as published.
- At 60 FPS the game retires one flip per vblank instead of one per two, the race fixed step halves to 1/60, and pedestrian animation gets a clip-step correction plus pose blending. The two animation helpers can be switched off with the documented environment settings; 30 FPS mode is untouched.
- 120 FPS is not part of a release build: the pacing code is compiled out and a larger `MC3_FRAME_RATE` is treated as 60.

### Port Options

- New `FRAME RATE` row on the Options page. The page holds 16 rows including `BACK`, so `PC MUSIC VOLUME` gives up its slot (the Audio Options music slider owns that value); `SONG DISPLAY` and `CONTROLLER VIBRATION` stay. The row count now falls back to 15 settings on a 16-row page instead of pushing `BACK` off the list.

### Linux

- `ReDUB-0.2.3-x86_64.AppImage`, built from the same source in a pinned Ubuntu container, with the in-game frame-rate row and the bundled libraries documented alongside it.

### Packaging and trust

- The launcher no longer embeds the setup source in the executable. `launcher-source.zip` ships beside it; the embedded 17 MB archive was the main reason virus scanners read the launcher as a packed dropper.
- The launcher now carries a real version resource (product, company, description, 0.2.3) instead of an empty one reporting 0.0.0.0, and the game executable takes its icon from the existing artwork.

### Other fixes

- Pose wrapper: guest pointers and frame indices are validated before use, guest registers and stack arguments are restored after the second decode, and non-finite transforms keep the real pose.
- Semaphore overflow now reports `STATUS_SEMAPHORE_LIMIT_EXCEEDED` with a rate-limited log instead of a bad handle, and the Linux compatibility layer refuses an over-limit release with `ERROR_TOO_MANY_POSTS` instead of silently clamping.

## ReDUB Windows edition — 2026-11-13

This source snapshot introduces the Windows-focused **Midnight Club 3: ReDUB** edition for the original Xbox release of *DUB Edition Remix*. No game data is included.

### Highlights

- Portable ReDUB launcher with Xbox Remix disc/folder import, setup verification, repair, local builds, shortcuts, renderer selection and launcher-bypass recovery.
- Native in-game Port Options with graphics presets, render/HUD scale, live FXAA profiles, filtering, sharpening, gamma, brightness, VSync, FPS display and restore defaults.
- OpenGL and Vulkan renderers, plus NVIDIA DLSS Super Resolution Quality, Balanced and Performance modes. DLSS jitter is disabled pending further work; frame generation is not included.
- Xbox controller reconnect and vibration support, configurable rumble strength, and an optional raw SDL wheel compatibility path.
- Windows media-session integration for Spotify, YouTube Music, Pandora and other compatible players, including volume, pause/resume, track skipping and native radio-title display with Unicode fallback.
- Renderer parity fixes for effects buffers, texture matrices, GPU copies, fullscreen/resolution handling, Career transitions, minimap masking and Vulkan presentation edges.

See `README.md` for requirements, setup, limitations and legal game-file guidance. Windows build instructions are in `BUILDING-WINDOWS.md`.

## v1.1.0

**Headlight beams in both games, and the first Android beta.** At night your car finally lights up the road ahead and we're releasing the Android version for further testing.

### Features

- **Headlight beams.** Turning your lights on at night now throws a beam of light onto the road ahead, as on the original Xbox: it brightens the street, cars and walls it reaches and stops where it meets them ([#10](https://github.com/laureladastra/mcdub3-nx/issues/10), thanks for the report). Before, the lights worked in the game but no beam was drawn. Other emulators (i.e. xemu → https://github.com/xemu-project/xemu/issues/996) show a solid white shape instead. The game builds the beam in a small hidden picture using two tricks of the Xbox's graphics chip that the port did not reproduce yet: it reuses a picture as a depth map, and it counts the beam's layers with a special blending mode that works with negative numbers. Both are emulated now. Tested in the DUB Edition and the Remix. The WHITE button flashes your high beams while you hold it. The main headlights are on by themselves at night.
- **Android beta (app version 0.1.0).** Both games can be installed as an Android app: `mcdub3-0.1.0.apk` (DUB Edition) and `mcdub3remix-0.1.0.apk` (Remix). Like on the Switch you bring your own copy of the game.

### Android Installation Instructions

1. Download the APK for your game from the [release page](https://github.com/laureladastra/mcdub3-nx/releases/tag/v1.1.0): `mcdub3-<version>.apk` for DUB Edition, `mcdub3remix-<version>.apk` for Remix. Both can be installed side by side.
2. Open the APK on your phone to install it. Android asks you to allow installing apps from your browser or file manager; allow it. If Google Play Protect warns about an unknown app, choose **More details › Install without scanning**. The app is not on the Play Store, so Play Protect does not know it.
3. Start the app once before the next step. The `Android/data/…/files/` folder will be created only after starting the application for the first tinme.
4. Copy your extracted disc (the folder with `default.xbe` in it) to the app's own folder:
   - DUB Edition: `Android/data/io.github.laureladastra.mcdub3/files/game/`
   - Remix: `Android/data/io.github.laureladastra.mcdub3remix/files/game/`

   The app finds it there by itself and needs no extra permission. On Android 11 and newer most file managers cannot open `Android/data`, so copy it from a computer over USB. Note that Android deletes this folder, saves included, when you uninstall the app.

   **Or keep the game elsewhere:** put it at the top of your internal storage or SD card (`/sdcard/mcdub3-nx/`)
    `sdcard/mcdub3-nx/` (Remix: `sdcard/mcdub3remix-nx/`)where it is found by itself. Alternatively you can pick any folder with **Add folder** on the start screen. Both need **All files access**, which the app asks for.
5. Optionally choose how the on-screen gamepad behaves: **Automatic** (hidden while a controller is connected), **Always**, or **Never**. Controllers (Bluetooth pads, Joy-Cons) and keyboards work without setup.
6. Press **Play** to start the game.

If the game doesn't start, the log is `mcdub3_log.txt` (Remix: `mcdub3remix_log.txt`) in the same app folder as in step 3, one level up from `game/`. Please attach it to a bug report.

> [!WARNING]
> **The most common mistakes are: copying the disc image (`.iso`) instead of its extracted files, or the wrong game's disc (the DUB Edition application with the Remix disc, or the other way round).** The app needs the folder of extracted files, with `default.xbe` in it. To unpack an `.iso`, use [xdvdfs](https://github.com/antangelo/xdvdfs): `xdvdfs unpack <your disc>.iso <folder>`. The wrong game's disc is refused with a message when you press **Play**.
>

## v1.0.0

**Version 1.0 for both games on the Nintendo Switch.** This release removes the last annoyances: races that felt slow when the frame rate dipped, random zoom-in or out during loading screens, and vertical lines during upscaling.

### Features

- **Races keep their real speed.** The original game moves the race forward by a fixed step every frame, which was fine on the Xbox, where it runs on a fixed 30 FPS. On the Switch, in rain, with nitrous or in the busiest streets, the frame rate can dip and the whole race then went into slow motion. The race now proceeds by the time that really passed. Racing in the rain and with nitrous feels much steadier.
- **No more random zooms during loading screens.** Menus and loading screens are drawn for 4:3 TVs, with black strips at the sides, and the port enlarges them to fill your 16:9 screen. When something briefly reached those strips (the circle animation on loading screens for example), the picture behind it jumped in size for a brief moment. The size stays put now.
- **No more vertical white lines at the edges at speed.** The motion blur when you drive fast could pull in thin white lines along the left, right and bottom edges of the picture. Fixed in both games.

### Other changes

- **Mac and Linux:** while driving, the arrow keys only work the gas and brake, so the DUB Edition's camera no longer moved in and out when you accelerated or braked with the keyboard. This experience is stable now.
- **Mac and Linux:** with a UI scale of 2 or more (`RECOMP_GL_UI_SCALE`), faint regular stripes over the race screen are gone.

### Tips

- **Remix:** a picture size of 1 (`RECOMP_GL_SCALE=1` in `mcdub3remix_env.txt`) gives the smoothest races. The DUB Edition handles 1.5.
- For normal play, leave `MC3_PERF` out of your settings file: it is a diagnostic and costs some speed.

### ⚠️ Known issues

- **Remix:** in the busiest parts of the city at full speed with nitrous, the frame rate can still drop for a few seconds. The race no longer slows down, but the picture gets choppier.

## v0.5.0

**A big speed update for both games on the Switch.** Races, especially in the rain, now run close to the game's own top speed of 30 FPS most of the time. Freezes during races are minimised.

### Features

- Measured in rain races on the Switch (at the default 1x picture size):

  |                                    | v0.4.0                 | v0.5.0   |
  | ---------------------------------- | ---------------------- | -------- |
  | Midnight Club 3: DUB Edition       | 23 frames per second   | **28.5** |
  | Midnight Club 3: DUB Edition Remix | 20.5 frames per second | **27.5** |

  The game itself never runs faster than 30, as on the original Xbox, so most of the time you are now at or near full speed.
- **No more half-second freezes in races.** The game stopped for about half a second now and then during a race, most often right after the start.
- **Sharper look for less effort (Remix).** The Remix's glow and blur effects are drawn at normal size even when you raise the picture size, which cost a lot of speed for something that is blurred anyway.

### Other changes

- Each game now has its own tuned graphics settings, so improving one game does not change the other.
- Saves can be copied between the Switch and a computer for the Remix too (`make save-sync-down GAME=remix`).
- Fewer files are written to the SD card at every start (some leftover diagnostic files are no longer created; you can delete any `progcache-slow` files you find).
- For developers: a much more detailed performance report with `MC3_PERF=2` (time per part of a frame, on the processor and the graphics chip, and why draws could not be combined).

### Tips

- **Remix:** keep the picture size at 1 (`RECOMP_GL_SCALE=1` in `mcdub3remix_env.txt`). Higher values still cost the Remix more than the DUB Edition.
- After updating, the first race or two may stutter briefly while the game prepares its graphics again. This settles once the file `progcache.bin` has been rebuilt; don't delete it afterwards.

### ⚠️ Known issues

- **Remix:** in the busiest parts of the city at high speed, the Remix's wide speed camera shows far more buildings than usual and the game can drop to around 12 frames per second for a few seconds; your car then feels slower too. This is the next thing being worked on.
- Right after a race starts, the game can still pause for about half a second while it loads the race's textures.

## v0.4.0

**This is the first release of Midnight Club 3: DUB Edition Remix**. Runs from boot to the finish line: intro movies, menus, garage, career and races, with music and sound. It ships next to the DUB Edition as its own app, `mcdub3remix-nx-eur.nro`.

### Features

- **Midnight Club 3: DUB Edition Remix.** A new app for this game: `mcdub3remix-nx-eur.nro` on the Switch (in `sdmc:/switch/mcdub3remix-nx/`, disc in `game-eur/` or `game/`), and a Mac app built with `make mac-app GAME=remix`.
- **Remix races play as they should.** From the countdown on, the camera no longer turns into a spinning top view, you can drive, and the pause and options menus appear centred with their list shown once.
- **More of the game converted correctly.** Floating-point comparisons now give the same answers as the Xbox's processor, also when a value is not a number. The Remix's code relies on this throughout (it was compiled for the Xbox's SSE unit, the DUB Edition mostly for the older x87 one).
- **Keyboard driving on Mac and Linux.** While you drive, the arrow keys now only press the gas and the brake. In the Remix the up arrow also pulled the race camera in and up; in menus the arrows still move through the lists.

### Other changes

- NSP forwarders are no longer attached to releases: they need your console's keys, so they are built on your own computer (`make nsp`, or `make nsp GAME=remix`; see CONTRIBUTING.md). The NRO is all you need with hbmenu.
- On the Switch, a run with `MC3_PERF=2` in `mcdub3_env.txt` keeps writing its log while the game hangs, so a hang can be diagnosed from the log.
- For developers: the app's identity moved to `config/<game>/app.env`; the repository's history now holds this project's commits only (the toolkit is vendored as a snapshot).

## v0.3.0

### Features

- **Smooth loading screens.** The circles on the career loading screen now turn smoothly and close, on every platform. The game copies each frame into the next one with the Xbox GPU's 2D engine and only draws what changed.
- **Faster menus on the Switch.** Moving through the main menu and its city flythrough went from about 20 to 27-30 frames per second. A background thread of the game checked a one-second timer thousands of times a second and kept interrupting the main thread. It now checks every few milliseconds.
- **More of the game lifted correctly.** Functions that end in a switch table were sometimes cut into pieces when the game's code was converted. They are now kept whole.

### Other changes

- On the Switch, the previous run's log is kept as `mcdub3_log_prev.txt` next
  to `mcdub3_log.txt`, so a hang can still be reported after the game was
  started again.
- We're preparing to port Midnight Club 3: Dub Edition Remix and integrate its code into this repo. Tools and scripts have been updated and the repo has been reorganised to support parallel builds for these different games.
- Generated files now live in `work-dub/` instead of `work/`. The first
  `make` after updating moves the folder for you; run `make lift` again
  after updating, as the converted game code changed.
- `make mac-app` takes the disc folder as `DISC=` (it was `GAME=`).

### ⚠️ Known issues

- Driving on the Switch runs at 18-25 frames per second.
- A race can occasionally hang (the screen stops, the game does not crash).
  If it happens, leave it for about ten seconds before quitting and share
  `mcdub3_log.txt` (or `mcdub3_log_prev.txt` if you started the game again).
- Right after a race starts, the game can pause for about a second.
- With fast storage the career loading screen can end before its closing
  animation has played; this is how the game reacts to a load that finishes
  very quickly.
  
## v0.2.0

### Features

- **Support for European (PAL) disc.** Updated the [guided installation page](https://laureladastra.github.io/mcdub3-homepage/) and build artifacts (`mcdub3-nx-eur.nro`) to build for this edition.
- **Beta release for Windows.** Updated section in our [README](https://github.com/laureladastra/mcdub3-nx) contains the build and installation guide.
- **Readable tutorial video text**: the text within tutorial videos is no longer scrambled at higher game resolutions.
- **Saves**: profiles from an original Xbox (a memory unit dump such as a `.gsx` from a save site) can be imported with `make import-save`, and profiles can be copied between the Switch and a computer over FTP with `make save-sync-down` / `make save-sync-up` (see the README's Saves section).

### ⚠️ Known issues

- Menus and the city flythroughs run below full speed on the Switch.
- The circles on the career loading screen sometimes stutter.
  complete; this is being investigated.
- Occasional hangs on career loading screens are being worked on.


## v0.1.0

This is a beta release of the game to provide early access to the community. Expect some crashes.

### Features

- Intro movies, menus, garage, career and races, with music and sound.
- Saves: create, load and delete profiles.
- Widescreen, higher resolutions and picture settings
  (`mcdub3_env.txt`; see the README's Settings section).

### ⚠️ Known issues

- Menus and the city flythroughs run below full speed on the Switch.
- Occasional hangs on career loading screens and in races are being worked
  on.

