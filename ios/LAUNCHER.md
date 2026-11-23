# ReDUB iOS launcher

Midnight Club 3: DUB Edition Remix, statically recompiled for iOS.
Version 0.2.3 · iOS 16+ · ARM64 · landscape · requires signing.

This package contains the recompiled runtime and launcher. It does **not**
contain the game. You need your own supported Xbox disc image (XDVDFS XISO, or
a full-disc image with the standard partitions). PS2, Switch, Xbox 360 GOD,
split and compressed disc images are not supported.

## Packages

- `mcdub3remix-beta-0.2.3-unsigned.ipa` — **ReDUB Beta**, the current iOS package.
  Bundle ID `com.ladastra.mcdub3remix.beta`, so it installs beside any earlier
  stable install and keeps its own Documents data.

The whole iOS line is beta for now: one package, version 0.2.3. A non-beta build
(bundle ID `com.ladastra.mcdub3remix`) can be produced later with
`python ios/package_launcher.py` once the garage work settles.

It is unsigned; sign it with your own certificate. The
`com.apple.developer.kernel.increased-memory-limit` entitlement in
`mcdub3remix.entitlements` is requested; if your signing profile cannot grant
it, strip it with your signer (the game may then have less memory headroom).

## First run

1. Sign and install the launcher IPA with your own certificate/profile.
2. Install page: Choose Xbox ISO, then Install game. The picker imports a
   local copy; extraction writes `Documents/game.installing`, verifies
   `default.xbe`, then swaps it into `Documents/game`. An existing install is
   kept until the new one verifies; an interrupted swap is recovered at next
   launch.
3. Free space: the installer checks available capacity first. A fresh install
   needs roughly the ISO size plus 1 GB; a repair needs room for both the old
   and new copies.
4. Keep the app foregrounded during installation. iOS can suspend a long
   background install; reopen and reinstall if interrupted. The screen is kept
   awake during install and during play.
5. Saves live in `Documents/save`. File sharing is enabled, and Support >
   Share diagnostics collects the log and env file.

Game data is excluded from iCloud backup (it is reinstallable); saves are
backed up normally.

## Controls

The on-screen pad is the controller when no Bluetooth pad is connected. A real
Xbox/PlayStation/MFi controller connected to port 0 hides the overlay and drives
the game, with rumble.

On-screen pad defaults: left stick steers and is the throttle/brake (push
forward for gas, pull back for brake); A confirms/handbrake, B backs; X/Y,
WHITE/BLACK, BACK/START; 5% steering dead zone. Other pedal layouts (LT/RT
corner buttons, shoulder bars, edge sliders), throttle ramp, latch gas,
auto-accelerate and stick haptics are available in Settings.

## Settings

Launcher > Settings (left/right arrows on each value; saved to
`Documents/mcdub3remix_env.txt`, applied on the next game launch):

- Display: gamma, brightness, vsync.
- Graphics: render scale, HUD texture quality, anti-aliasing, texture
  filtering, sharpening, frame queue, display shape (Auto 16:9 / 4:3 /
  Stretch).
- Controls: on-screen controls, pad opacity, pad size, pedals, latch gas,
  auto-accelerate, stick haptics, controller vibration.
- Advanced: steering dead zone, throttle ramp.
- Diagnostics: GL error log (Off / Step / Call).

Defaults on iOS: render 2x, HUD 3x, filtering 4x, FXAA on, sharpening 50%,
display shape Auto, pedals stick, latch off, dead zone 5%.

In-game: Options > Port Options shows the same settings, with an ADVANCED
page. Touch/pad, rumble, anti-aliasing, gamma, brightness and sharpening apply
live; render scale, display shape and filtering are marked (RESTART) because
they are read when the renderer starts. HUD texture quality is set in the
launcher only.

## Known issues

- GL_INVALID_OPERATION (0x0502) is logged during racing on some scenes. Under
  investigation; Diagnostics > GL error log = Call names the exact call.
- Rendering can still submit GPU work while the app is backgrounded; a
  background/foreground transition may log Metal background-execution errors.
- The FPS overlay was a Windows-only feature and has been removed from the iOS
  launcher rather than shown as a dead toggle.

## Build on this machine

```
python ios/build_launcher.py
python ios/tests/test_reader.py
python ios/package_launcher.py
python ios/package_launcher.py --bundle-game
python ios/tests/verify_packages.py --output <folder>
```

The build links cached generated game objects; `build_launcher.py` force
rebuilds the iOS host, renderer, touch pad, input, and Port Options objects so
stale baseline objects cannot ship. Validation covers archive CRC, plist,
scene manifest, ISO document type, arm64 Mach-O, executable permissions, icon
files, and binary equality; it does not cover device behaviour.

## Legal

ReDUB is a static recompilation and requires the user's own legally acquired
disc. No game data is included in the launcher package. Do not redistribute the
full-test IPA or any extracted game files.
