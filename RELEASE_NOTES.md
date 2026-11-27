# Release notes

## v0.2.6.1 - 2026-11-26

**The Android and iOS builds in 0.2.6 showed a black screen. This replaces them.**

Desktop is unaffected and unchanged: if you already have 0.2.6 for Windows or Linux, there is
nothing to do. The bug was in a code path only the two mobile builds take.

### What went wrong

0.2.6 added ambient occlusion, which needs a depth-only framebuffer, and to set that up it called
`glDrawBuffer`. That is a desktop OpenGL function. OpenGL ES, which is all Android and iOS have,
offers only the plural `glDrawBuffers`, so the name does not exist at any ES version. The renderer
asks the driver for every entry point it needs and treats one missing name as fatal, so it stopped
after creating the window and before drawing a single frame. Audio had already started, which is
why the game could be heard with nothing on screen.

Windows and Linux were never at risk: they get a real OpenGL 3.3 core context, where `glDrawBuffer`
exists and resolves normally.

### The fix

The renderer only ever calls it as `glDrawBuffer(GL_NONE)`, to detach colour from that framebuffer,
so it is now mapped onto `glDrawBuffers` for ES contexts - the same way this file already handles
`glClearDepth` and `glDepthRange`, which are float-only in ES. On desktop the real function is
found and the mapping is never used.

### Landscape, and the system bars (Android)

Both Android APKs were replaced on this release on 27 November with this fix; the file names and
the signing key are unchanged, and the version inside is the same.

The game could open in **portrait**, and the status and navigation bars stayed on screen. Neither
was new to 0.2.6 - the manifest and the SDL settings are identical in 0.2.5 and 0.2.6 - so anyone
who has seen it has seen it since the Android port landed.

- **Portrait.** The manifest asks for `sensorLandscape`, but SDL overrides that. `SDL_androidwindow.c`
  hands `SDL_HINT_ORIENTATIONS` to the activity, and with no hint and a resizable window
  `SDLActivity.setOrientationBis()` asks Android for `SCREEN_ORIENTATION_FULL_USER` - follow the
  device - so a phone held upright got a 1080x2340 portrait surface. The hint is now set before the
  window is made, and the log shows `requestedOrientation=6` (sensor landscape) instead of `13`.
  `resizeableActivity="false"` also stops a freeform window handing the game a portrait shape.
- **System bars.** The game inherited `Theme.NoTitleBar.Fullscreen`, which is the pre-KitKat
  fullscreen flag and does not hide the navigation bar, and the start screen set
  `Theme.Material.NoActionBar`, which is not fullscreen at all. Both activities now hide the system
  bars with `WindowInsetsController` (minSdk is 33, so it is always available) and restore them only
  as a transient swipe. The start screen's theme is now `Theme.Material.NoActionBar.Fullscreen`.

Verified on Android 15 with the phone held in portrait: the device reported `ROTATION_0` before
launch and `ROTATION_90` after, the surface stayed 2340x1080 with no portrait flip, and the
screenshots show no status bar and no navigation bar on either the start screen or the game.

### Verified, not assumed

On an Android 15 emulator, against the shipped 0.2.6 APK: before, the log read
`missing entry point glDrawBuffer` and the frame was entirely black (brightest pixel 0). After, no
missing entry points, 1092 draws per frame, and the intro, title screen and profile menu all
render. The same emulator plays the intro videos on 0.2.5, so the port itself was never the
problem.

### Upgrading

- **From 0.2.6 on Android: install 0.2.7 straight over it.** This APK is signed with the same key
  as 0.2.6, so Android accepts it as an update. Nothing is uninstalled, and the disc and career
  stay where they are. Keep the app open until it finishes.
- **From 0.2.5 on Android: uninstall first, and back up before you do.** 0.2.5 was signed with a
  different key, so Android refuses to update it. Uninstalling deletes `Android/data/<package>`,
  which is where both the extracted disc and `save/` live. Copy that folder somewhere else first.
- **On iOS:** delete the old app and install this one. Saves live inside the app's own container,
  so copy them out first if you want to keep them.

Every file here is named ReDUB-0.2.6.1-*.

Inside the Android APK the version reads 0.2.7, and its version code is 207999 rather than 206999.
That is deliberate. The code is built from the first three parts of the version, so 0.2.6.1 would
have produced the same 206999 as the broken build, and Android could not tell the two apart. 0.2.7
is the smallest number that moves the code forward. The file name follows the release, as always.
## v0.2.6 - 2026-11-26

**The look of the game becomes adjustable, and a crash finally leaves evidence.**
No new claims about online play; the networking story is unchanged from 0.2.5.

### Post effects: glow, vignette and colour

Three effects drawn after the scene, each off by default and each with its own strength in
the launcher's Graphics page.

- **Glow** — bright things bleed light. Streetlights, neon, headlights and tail lights pick up
  a soft halo; dark areas are left alone, so the night does not turn grey.
- **Vignette** — the corners of the picture are gently darkened, brightest in the middle.
- **Colour** — more saturation around each pixel's own brightness, plus a slightly warm tint.
  It does not touch contrast: an earlier version did, and measuring showed it made the night
  8% darker overall, so that part was removed.

All three read the frame after it is drawn, so the HUD and menus are untouched. They are
driving-only and cost eight texture samples per pixel for glow; vignette and colour are
almost free.

### Ambient occlusion

Soft contact shadows where surfaces meet: under the car, along the kerbs, in corners, where a
wall meets the pavement. It reads the scene's own depth buffer, so it follows the geometry
rather than guessing from the colour image. Measured on a night race: mean brightness 5.5%
lower, 7.9% of pixels more than 8 levels darker, 0.08 levels on the HUD, no halos, no frame
cost. `RECOMP_GL_AO_RADIUS` sets how far the shadows reach.

### Crash reports on Windows

A crash used to leave half a log line and nothing else. Windows now writes, next to the run
log:

- `crash-<time>.txt` — the exception, the faulting address and its guest address, host and
  guest registers, the Xbox return addresses still on the stack, the recent indirect-call
  targets, and the thread id (with a note when the fault was on a host thread that has no
  guest context, so empty registers are not misread as "the guest was at zero");
- `build.txt` — version, edition, OS and the settings that matter, rewritten at crash time so
  it describes the run that died rather than the one that started.

A first-chance handler logs access violations, illegal instructions, stack overflow and
divide-by-zero, at most eight lines so a loop cannot fill a disk. The exception is not
swallowed: a game that carries on with a corrupted heap is a worse bug later.

The launcher turns that into something sendable. When a run fails it offers **Report the last
crash**: a prefilled issue and a `ReDUB-crash-report.zip` with the run log, the previous log,
the crash report and `build.txt`, ready to attach. Nothing is ever uploaded by itself.

No minidump is written. `MiniDumpWriteDump` refuses with `0x80070066` unless the process holds
`SeDebugPrivilege`, which a normal player session does not; asking for it failed here, and a
zero-byte `.dmp` beside a crash report is worse than none. The guest call chain is the more
useful half anyway — a host stack names a `recomp_00xx.c` line and stops.

### An incomplete install is refused instead of crashing

`Xdvdfs.Verify`, the only thing an import checked, looked at `default.xbe` and nothing else.
A disc copy whose data archives were missing or cut short therefore passed Install, and the
game then died at the first frame that needed the absent bytes — `0xC0000034` or a `KeBugCheck`
mid-race, the path `roam_game.c` documents for `assets_x.dat_2`.

The launcher now reads the Dave pack header: the magic (`Dave`/`DAVE`), the entry count and
the table of 12-byte entries. The file must be long enough for its own table and every entry
that names data must end inside the file. Numbered parts are read in order, so a gap is caught
— which is the failure that matters, since the game opens them by number. `banks.dat` is
checked the same way. Play, Verify setup, the CLI and RecompLauncher's import all refuse with
the file named.

### Rebindable keyboard

Settings → Controls → **Rebind keyboard…** opens a page grouped by part of the pad — D-PAD, face
buttons, shoulders and triggers, system, and each stick — with every action on the left and its key
in a field on the right.

Click a field and it reads *Press a key…*; the next key you press becomes it, and the line at the
bottom says what changed. Escape cancels, or closes the page when nothing is selected. **Clear key**
unbinds the selected row, right-click clears or resets a single row, and **Reset to defaults** puts
every key back.

A key already in use is not taken silently: the field turns amber, the bottom line names the action
holding it, and pressing it again uses it for both — the two rows then stay highlighted so the
overlap is visible later. None of that is a message box any more, which used to interrupt the
keystroke you were in the middle of answering. Rebinding also no longer draws a second label over
the first, which used to double every row after one edit.

`MC3_KEY_TRACE=1` logs every key that becomes a pad button, which is the quickest answer to
"my rebound key does nothing".

### Quality of life

- **Pause on focus loss** (`RECOMP_PAUSE_ON_FOCUS`) opens the game's own pause menu when you
  switch to another window, so a race does not run on while you are away.
- **Loading screen minimum** (`RECOMP_LOADING_MIN_MS`) keeps a fast load's screen visible for
  at least the time you choose. A small load on a fast machine can otherwise finish between
  two of the screen's own frames and never be seen at all. The movie keeps animating while it
  waits, and only the first frame of a load is held, so a long load is never made longer.
- **Tooltips** — every setting in the launcher explains itself on hover.

### Settings pages

The Graphics page carries the four new effects below Sharpening; Display carries the two new
options. Every row's name, value and arrows now share one baseline (they were up to 10 pixels
apart), and the two arrow glyphs are real chevrons rather than quote marks that drew five
pixels tall.

### Stability

- **60 and 120 FPS can no longer run at double speed.** The frame cadence and the world's
  simulation step are chosen in two different places, and only the step was conditional: it is
  halved to 1/60 or 1/120 each frame, but only while the game is classified as being in a race.
  The flip count assumed that halving always happened. On any frame where the classification was
  wrong, the port retired 60 frames a second while advancing the world 30 times a second — two
  seconds of game per second, which came and went as the classification recovered. The cadence is
  now derived from the step the world is actually being advanced by, so a mismatch runs at the
  right speed with fewer frames instead of twice as fast. Nothing changes when the two agree, and
  30 FPS never used this path, which is why only 60 and 120 were ever reported.
  `MC3_FLIP_PACE_TRACE=1` prints a line the first time the clamp engages.
- A guest thread parked while nothing is presented now logs a heartbeat instead of freezing
  silently.
- The GPU command walker resumes at PUT when it loses the stream, which is what stopped a
  bad target being re-read for ever; the test now asserts it.
- A game folder another process still holds open is retried for about a second instead of
  failing the import and losing the career.
- `--check` and `--check-data` answer before the launcher unpacks its source bundle, which is
  what made them slow and, on a bare folder, left a message box with no exit code.

### Settings are kept when game files are replaced

Adding or replacing game files used to replace the whole game folder, taking the career save
and the title's own settings with it. The import now stages the copy, verifies it, carries the
player's UDATA/TDATA across, and only then swaps the folder in. It also refuses while the game
is running, since a running game holds those files open.

### Android and iOS rebuilt

Both mobile builds are now made from the same tree as everything else, on a Windows PC: the
APK through the Android SDK and NDK, the IPA through clang and the iPhoneOS SDK with no Mac.
The IPA is arm64 and unsigned, for Sideloadly or AltStore to sign with your own certificate (it
asks for the increased memory limit the game needs).

### Android ships two APKs, and you may need the second one

Google Play Protect now **automatically blocks** the install of an app that asks for
notification access when the file was opened from a browser, a messaging app or a file manager.
ReDUB asks for it for one reason: so the in-game radio can read the song playing on your phone.
It is not a malware warning, it is a rule about that permission, and it applies in some countries
and not others.

So this release carries two builds of the same game, same version, same key:

| File | What it declares | Use it when |
|---|---|---|
| `ReDUB-0.2.6-android.apk` | notification listener + all-files access | you can install it, and want the now-playing radio and a disc from anywhere on the phone |
| `ReDUB-0.2.6-android-compat.apk` | neither | Android refuses the first one |

The second build is the same game. It loses the phone-music bridge and the ability to read a disc
from a folder outside the app's own, and nothing else. It installs like any normal APK, because
the rule only looks at what the app asks for.

If you would rather keep the full build: `adb install` is not one of the checked sources, so it
installs without the block and Play Protect stays on. On a Samsung phone, turn off Auto Blocker
first, since it blocks USB installs as well.

### Upgrading from 0.2.5 on Android: back up first

0.2.6 is signed with a new key, so Android refuses it as an update over 0.2.5 and it has to be
uninstalled first. **Uninstalling deletes the app's own folder, and that is where the disc copy
and the saves usually live** — the career is `save/` inside the game folder. Copy the folder out
before you uninstall, then point the new install at it.

- **Full APK:** copy the game folder to somewhere else first, such as `Download/ReDUB` or an SD
  card. Uninstall, install 0.2.6, and choose that folder on the start screen. Nothing is lost.
- **Compat APK:** it has no all-files access, so it cannot read a disc from elsewhere on the
  phone. Copy the folder back over `adb` instead — `adb` can read `Android/data`, Android's own
  file managers cannot:

```
adb pull /sdcard/Android/data/io.github.laureladastra.mcdub3remix/files redub-backup
adb uninstall io.github.laureladastra.mcdub3remix
adb install ReDUB-0.2.6-android-compat.apk
adb push redub-backup/files /sdcard/Android/data/io.github.laureladastra.mcdub3remix/
```

### Known issues

- **Crashes inside guest code still produce no report on Windows** if the fault is a hang
  rather than an exception. A freeze leaves nothing; a crash leaves the files above.
- **No minidump** (see above), so "which function faulted" is answerable and "what was the
  corrupted pointer pointing at" often is not.
- **The pause-on-focus menu** was verified as a hook firing; the menu appearing was not
  recorded on screen.
- **Android has to be uninstalled before 0.2.6, and that deletes your game folder** — disc copy
  and career alike. This build is signed with a new key (the 0.2.5 one was lost with the build
  kit), so Android will not install it over 0.2.5. Copy the folder out first, as described under
  *Upgrading from 0.2.5 on Android*. Staying on 0.2.5 is also fine.

### Not in this release

- **Nothing new for online.** The relay, rooms and matchmaking described in
  `docs/0.2.6-PLAN.md` are still ahead; this release is the single-player and diagnostics work.
- **PlayStation and Switch button glyphs**, deferred from 0.2.5.
- **Music playlists**, deferred from 0.2.5; the built-in PC Music integration is unchanged.
- **The twelve advanced tuning settings** (`MC3_PERF`, `MC3_HANG_SECS`, `RECOMP_NET_*` and the
  rest) remain file-only; they are documented in `mcdub3remix_env.example.txt`.

## v0.2.5 - 2026-11-24

### Split-screen, verified

The game's own 2-player mode now has a recorded result rather than a claim: with a second
pad connected, Arcade offers `PLAYERS: 2`, both start prompts appear, and the race draws two
live viewports with their own cameras and HUDs.

### A freeze that needed a kill is fixed

Some sessions froze on a single frame and had to be killed. The port's GPU command walker
could lose track of the command stream and then refuse to move on: it re-read the same word
every frame, thousands of times a second, executing nothing. In one run it logged
**4.88 million resyncs to a single address** with the frame count frozen. The game was then
waiting for a fence that the skipped commands were supposed to raise, and waited for ever.

The walker now always moves forward when it loses the stream, and a fence that can no longer
arrive is released: one frame is dropped instead of the session.

### Mods: unlock everything

A Mods menu with an "unlock everything" toggle, plus the home for future mods.

### Aspect ratio

Aspect ratio (Auto 16:9 / 4:3 / Stretch) is available in the launcher's Display page and in
the in-game Port Options as Display shape. It applies on the next start.

Auto shows a widescreen game at 16:9; 4:3 keeps the original shape with bars at the sides;
Stretch fills the window. The value is also accepted as `stretch` in the settings file, and an
unknown value now says so in the log instead of silently showing 16:9.

This also fixes a long-standing framing error where the picture was shown at 1.6:1 with unused
columns on the right (Midnight Club 3 draws 720 of a 768-pixel pitch). All three modes were
checked on screen.

### Stability and diagnostics

- **Log growth fixed.** A single short cruise used to write **322 MB / 5.0 million lines**
  because a diagnostic trace was armed by a runtime default. The same scene is now 0.5 MB,
  and the trace needs `RECOMP_NV2A_TRACE`.
- **Boot stages carry their timing.** The autostart log prints seconds next to each stage
  (`[AUTOSTART] 11.4s title`), so start-up problems can be measured instead of guessed.
- **A run log on Windows.** `work-remix/logs/latest.log` holds the last run, with the previous
  one kept as `.previous`, so a report has something to attach.
- **Closing cannot hang.** A watchdog arms on every quit; if teardown has not finished six
  seconds later it dumps each thread's stack and leaves. Windows used to show "stopped
  interacting with Windows" and close the game instead.
- **The logo screen can no longer freeze.** An intermittent stall inside the native movie
  open (one boot in ten in a soak) held the front end on `rockstar.bik`. The per-read and
  kernel-summary traces are off by default now (`RECOMP_QUIET=0` restores them): one
  synchronous log write per 8 KB read was stalling the boot thread on I/O. The hang watch
  also reports a first-screen stall instead of ignoring it.
- **Saves complete reliably.** The title's saves finish through an APC, and delivering one
  used to clobber the interrupted CPU state, including the I/O status the write reports.
  The APC now runs with the guest's registers, FPU state and argument frame preserved.
- **Non-ASCII install paths.** The Windows host, the settings writer and the run log use
  wide-character paths, so a disc folder such as `C:\Jeux\Édition` works for the disc,
  the saves and the log.

### No settings file in the package

0.2.4a's Windows zip shipped a real player settings file, so a fresh extract inherited the
machine's own settings (including a forced 60 FPS). This build ships no settings file at all:
the game creates one from the example on first run, and the release check refuses a package
that contains one.

### Online: staying in the room

A client's receive loop parked the network thread on a blocking socket after the first
datagram. A relay only forwards, so a quiet room sent nothing back: the thread never returned
to its tick, the keepalives stopped, the relay reaped the player after 30 seconds, and the
other machine saw **0 rooms** while both games were open. The socket is non-blocking now, and
a datagram forwarded by the relay counts as the hub being alive. A four-minute idle test keeps
the player in the census, and a synthetic player's 24-byte System Link broadcast reaches the
game (`[XNET] rx ... 01 37 18 00`).

The relay's own source is in the repository under `tools/` (`mc3relay.py`, `mc3hub.py`), and
the release page carries the same files packaged as **`ReDUB-relay-server-0.2.5.zip`**. The
game does not need it — it already uses a running server — but anyone who would rather host a
room server can: the package has the two Python scripts, a Dockerfile, a systemd unit and
start scripts, and needs nothing outside Python 3.8.

**Online Race was played across two machines on this build**: one machine created the match,
the other joined it, and both cars raced in the same event.

### Your name, without editing files

The launcher's **Online** page has **Your name** on Windows, Android and iOS. It follows the
game's own rule (1-24 of `A-Z a-z 0-9`, space, `_`, `-`, `.`) and applies on the next start.
Invites now name the sender: `ReDUB - <name> invites you to room N`, then the `redub://room/N`
link.

### Phones online

A phone defaults to a 180 m remote-car radius and a 0.10 s pose-blend cap, so a spread-out
field and one stalled frame cost less. 60 fps is still a setting the phone has to earn: render
scale (2X by default) and anti-aliasing are the biggest levers. Adaptive resolution is
scheduled for 0.2.6.

Both mobile packages were installed on a device and boot into the game from these builds.

### Known issues

- **Start-up time is intermittent on the machine used for testing.** Runs reached the title
  in 11.4-11.6 s, while a few sat on a frozen frame for minutes. Three separate freezes were
  found and all three are addressed: two in the GPU command walker, and one where the movie
  layer waited for ever on a thread that stopped signalling. The last is bounded now — Bink's
  two-object wait times out after two seconds so the game continues. `MC3_MOVIE_WAIT_MS=0`
  turns that protection off if it ever causes trouble. If you still get a freeze,
  `work-remix/logs/latest.log` says which one: the walker ones log `[PB] decode abort` or
  `[PB] bad target`.
- The **0.2.4a report of "does not get past the first race" did not reproduce** here: a
  career race was completed at 60 FPS, past RESULTS and back into the cruise. If you can
  reproduce it, please say which platform and package you used and attach
  `work-remix/logs/latest.log`.

### Not in this release

Free Roam (deferred to 0.2.6; its Online item reads **`LINK: OFF`**) and its in-game nickname
editor - the launcher's **Online** page sets your name instead. macOS is not built or tested.

## v0.2.4 - 2026-11-18

### Frame rate

- **30 FPS remains the default**, the title's own cadence. Nothing changes for anyone who
  does not touch the setting.
- **60 FPS and 120 FPS are opt-in** (launcher -> Settings -> Graphics -> Frame rate, or
  in-game Port Options, which cycles 30 -> 60 -> 120 on this build).
- **120 needs three things:** a Windows build with `MC3_ALLOW_120` (all of them now), the
  setting on 120, and a display actually running at 120 Hz or higher. A CPU that can carry
  120 steps per second makes it native; below that the game keeps real time and presents
  what it can.
- **60 FPS keeps real time.** The simulation now compensates when a race cannot hold its
  cadence instead of advancing at `fps/target` speed. Measured: 19.85 s of race clock in
  20.10 s of wall time (98.8 %). Before the fix: 70.5 %.
- **Frame generation** (launcher -> Graphics -> Frame generation, default **Off**):
  - `Crossfade` - plumbing test, visible double exposure in motion.
  - `Motion-compensated` - half-res block match between history and the current frame, the
    history warped by half the motion, mix weight scaled by match confidence so a failed
    match repeats a frame instead of ghosting.
  - Costs about 0.5 ms per frame. The HUD is captured after every overlay and never
    interpolated.
  - **Requires a 120 Hz display.** On anything slower it refuses to run rather than halve
    the game rate; `RECOMP_FG_FORCE=1` overrides for testing.
  - Measured at the title on a 60 Hz machine with the override: rendering 28.5 fps,
    presenting 57.0 fps - exactly two presented frames per rendered frame.

### Hardware for 120 (full table in SPECS-120.md)

- **Minimum, 120 via frame generation:** 120 Hz display, CPU that holds 60 fps (Ryzen 5 3600
  / i5-10400 class), GPU with ~10 ms of headroom (GTX 1660 Super / RX 5600 XT class).
- **Recommended, native 120:** 120 Hz display, single-thread ~25-30 % faster than a Ryzen 5
  5600 (7600 / 7800X3D / 13600K / 13700K class). GPU is not the limit.
- **Ceiling is 120.** No 144 or 240 mode.
- Why it is CPU-bound: 9.2 ms per frame in guest code and pushbuffer decode against an 8.3 ms
  budget at 120, with only 1.9 ms of GL work. Lowering resolution does not help.

### Renderer

- **OpenGL is the renderer.** DLSS was removed from the launcher: the NGX bridge is
  Vulkan-only, and the Vulkan renderer runs at 5-20 fps against OpenGL's 60-95 on the same
  race, so the control could only promise something the shipping renderer cannot do.
- The Vulkan experiment remains in the source for investigation; it is not part of the
  release and is not selectable for play.

### Known limitations

- 120 and frame generation are **unverified on a 120 Hz display** - the reference machine is
  60 Hz. Correctness, cost, pacing and HUD handling are verified; smoothness is not.
- Frame generation's flow search covers about +/-4 px per frame; faster camera motion falls
  back to a repeated frame rather than warping.
- The in-game Port Options row for frame generation is launcher-only.
- Android and iOS packages carry their own mobile work (phone music in the radio, mix gain,
  rumble row) and are otherwise unchanged by the PC work.

### Not in this release

- Vulkan performance work (renderer-scale).
- DLSS at all (removed while its renderer is not viable).
- A working per-draw motion attachment (no stable draw identity across frames; measured
  0 matches with two different keys).

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

