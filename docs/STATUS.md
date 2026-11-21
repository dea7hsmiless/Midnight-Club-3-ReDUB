# ReDUB status

_Updated 2026-11-19. This file is the single current state. Overwrite it; do not append._

## Released

**v0.2.4a** — https://github.com/dea7hsmiless/Midnight-Club-3-ReDUB/releases/tag/v0.2.4a
Tag `v0.2.4a` = `3a64190`; published 2026-11-20. Adds RecompLauncher support (Windows and
Linux), the `KEY = value` settings reader, and Redump disc matching. Full release, so it is the
GitHub "latest" and what RecompLauncher installs.

**Windows zip replaced 2026-11-19** without moving the tag (`236266f`): published 0.2.4a drew the
Graphics-page internal-resolution hint over the "Advanced graphics" button, because the hint
reserved no vertical space and the button was placed at the same `y`. The outline cut through the
text and its label was hidden, leaving Frame queue unreachable. Windows only — the Android, iOS
and Linux front ends have no such page. `launcher-source.zip` was rebuilt too, so "Rebuild from
source" does not reproduce the bug.

| Asset | Bytes | SHA-256 |
|---|---|---|
| `ReDUB-0.2.4-Windows-x86_64.zip` | 81,061,686 | `9f4f94aefa6dd1540cd8e540c912e3f035e8e0eebc7d69d51bc892268b0abb97` |
| `ReDUB-0.2.4-x86_64.AppImage` | 22,145,528 | `f0080f18da540f878df3e300c061f34402c35eaffc9c918c326798b71b1c8aab` |
| `ReDUB-0.2.4-x86_64.tar.gz` | 23,850,799 | `557ace2f4b4f959fbf231f374ac5fb981143bd7b29d7e2412cbeca74dd4e7ffb` |

Launcher `0785afa43ae7dfff184f683af7cbdaabfec2bb6c2a32c7c129df856a611cfc1f` (111,104 B, was
`3d562073…`); `launcher-source.zip` `2164edee…` (17,425,575 B);
game exe `cd1e3a7e24ea98d666c39b58c0d6698c0a40f88fb9aff3ac34146c30a649d6c7` (34,111,488 B,
unchanged). Android and iOS assets are the unchanged 0.2.4 files.

**v0.2.4** — https://github.com/dea7hsmiless/Midnight-Club-3-ReDUB/releases/tag/v0.2.4
Tag `v0.2.4` = `d00c579`; published 2026-11-19. Branch `release/0.2.4` is merged into `main`.

| Asset | Bytes | SHA-256 |
|---|---|---|
| `ReDUB-0.2.4-Windows-x86_64.zip` | 81,057,319 | `cbcdcaf543c55084f3696775714c24b0b5a88ea6d57da46b158d757346965cbf` |
| `ReDUB-0.2.4-x86_64.AppImage` | 22,141,432 | `46bc6fdf6da48b456fbf1d1a95685b717f652a4e719254eb1909f839e2cac27d` |
| `ReDUB-0.2.4-x86_64.tar.gz` | 23,848,802 | `e7ff596d6184420acc3345bb5c87ab0c64d43b800fdf813b231b97b39b32f51e` |
| `ReDUB-0.2.4-Android-universal.apk` | 50,928,361 | `907e2c4229f064f8ea4ffa2bef4ba2071bb0a2cdecac1421171d013c61b5553a` |
| `ReDUB-0.2.4-iOS-mobile-unsigned.ipa` | 16,431,497 | `8a036e13e2570f5fedb58fb46554b0c0f305f4a372b68e27eb12db416f67e841` |
| `ReDUB-iOS-0.2.4-source.zip` | 5,071,240 | `fc06238e48006e55205fe739aeb85d41cb56098e3b689a67b5dece8e3a064d4b` |

Launcher inside the zip: `c32713e31177019d11dc7a13b536dc2f451babff8056386077df41d4df6e084a`
(110,592 B, unsigned). The draft had been uploaded earlier with a different asset set; all
eight assets were replaced before publishing, and the published digests match `SHA256SUMS.txt`.

**Previous:** v0.2.3 — tag `a6f14fe`, launcher `6e2466f6…` (90,112 B).

## Planned for 0.2.5

Scope: split-screen, a Mods menu, System Link, online, free roam and UX. Plan:
`Desktop\ReDUB-0.2.5-work\ROADMAP-0.2.5.md`.

Landed so far (built and run on Windows, 2026-11-19):

1. **Mods page + `MC3_UNLOCK_ALL`.** Every car, part, rim, paint and driving
   ability is available and every race and city is open (the game's own five
   flags, re-asserted each frame; the two reward paints through the generic
   check `sub_0028B4E0`). Career money and saves are untouched. Launcher Mods
   page, both `recomplauncher.json` manifests. Restart to apply. Verified:
   flags read back 1 and the game boots to Cruise; the showroom report reads
   94 of 94 cars buyable with the toggle, 10 of 94 without it.
   The Mods page holds gameplay mods only; autostart and skip-movies moved to
   **Settings -> Startup** so "Mods" keeps its meaning (see `AGENTS.md`,
   "Features, options and mods").
2. **`MC3_AUTOSTART=cruise`.** Cold boot to Arcade, Cruise through the game's
   own menus (kernel path hook + pad 1 presses); any input of yours stops it.
   Verified end to end: title, profile, main menu, Arcade, "in Cruise".
3. **`MC3_SKIP_MOVIES=hard`.** Opens each boot movie and ends it at once with
   nothing decoded; the Rockstar logos no longer play. Verified: all three
   boot movies skipped, game reached the title.
4. **Diff report** for the three mc3-web shared files:
   `Desktop\ReDUB-0.2.5-work\diff-0.2.5\DIFF-FINDINGS.md`. `kernel_xbox.c` is
   byte-identical (the 20 s XNET screen is not fixed there); `main.c` has one
   portable hook; `recomp_manual.c` needs per-feature ports. Their System Link
   thunk addresses and Online session hooks are recorded there.
5. **Split-screen (M0) — validated 2026-11-19.** Arcade setup offers
   `PLAYERS: 1/2`; with a second controller (a pad script standing in) it adds
   `P2 STOCK`, walks `PLAYER 1 PRESS START` / `PLAYER 2 PRESS START`, and the
   race renders **two live viewports, each with its own HUD and camera**.
   Screenshot: `Desktop\ReDUB-0.2.5-work\test\shots\split-live.png`. The store
   claim is now true on this machine.
6. **System Link (M2) — validated 2026-11-19.** `xboxrecomp/src/net/xnet_hle.c`
   (host UDP) and 21 thunks in `recomp_manual.c`, all behind `MC3_NETPLAY=1`.
   Two instances on one machine (`RECOMP_NET_ADDR=127.0.0.1` / `127.0.0.2`,
   peers pointing at each other): the host creates a match, the second
   instance's **Join Game lists it** (Players 1/4), joining shows "Player 1
   joined game." on both lobbies, and starting the race puts **both players in
   the same San Diego cruise with name tags**. Screenshots:
   `test\shots\net8.png`, `jb2.png`, `ja5.png`, `ja6.png`, `jb4.png`.
   The launcher exposes it as **Settings -> Network -> LAN Play (System Link)**
   (both manifests, documented in the env template). Next: a real LAN, then
   the same over a VPN (no server needed).
7. **Linux ISO import — implemented 2026-11-19.** Native reader
   `xboxrecomp/tools/xiso/xdvdfs_extract.c` (port of `Xdvdfs.cs`), bundled as
   `usr/bin/redub-xdvdfs`; `AppRun --import` / `--game-dir <iso>` unpacks into
   the data folder with progress; the Linux manifest is `iso_or_folder` with a
   `prepare` step. Tested on a real Xbox image (2.87 GB: full tree in 22 s, a
   foreign `default.xbe` correctly refused). The AppImage still needs a rebuild.

Still to do:

8. **System Link on real hardware.** Two PCs on a LAN, then the same across a
   VPN (no server needed). The local two-instance test passes end to end;
   what remains is addressing (`RECOMP_NET_ADDR` = the machine's LAN address,
   `RECOMP_NET_PEERS` = the other machine) and a firewall rule for UDP
   `20000 + guest port` (20000-21000 covers MC3).
9. **Online (M3)** — System Link over a VPN first (no code), then rooms.
10. **Free roam (M4) — the remote-car pool is in 2026-11-20.**
    `xboxrecomp/src/net/roam/` holds the game-agnostic core, its API and a new
    native UDP platform; `src/games/remix/roam_net.c` starts it under
    `MC3_ROAM=1`. With no hub the core discovers peers by broadcast (up to 8,
    a LAN mesh): two instances log `[roam] MC3 joined` on both sides.
    `src/games/remix/roam_caps.c` grows the vehicle registry once from 10 to
    22 (12 remote cars; `MC3_ROAM_CARS` sets it), verified in a cruise:
    `[ROAM] caps: vehicle registry 10 -> 22`, no crash.
    The game layer is now in too: `roam_game.c`, `roam_cars.c` and
    `roam_pool.c` from upstream, compiled against `roam_compat.h`, with the
    player-manager, player-update, physics-step and state-pass hooks in
    `recomp_manual.c` (`xbox_gil_atomic` added to `kernel_bridge.c`). Two
    instances on one machine (`RECOMP_NET_ADDR` 127.0.0.1/127.0.0.2, each in
    `RECOMP_NET_PEERS`; each needs its own writable `game-remix` UDATA) reach
    Cruise with one peer each; both log `[ROAM] slot 1: ... driven, drawn 1,
    in draw list 1, phys 1 ... (10 m from us)`. The cruise builds 3 slots
    (1 peer + 2 spares), about 741-779 KB per car.
    Name tags are native (2026-11-20): `roam_tagdraw.c` projects each driven
    peer's car through the camera (view matrix [0x559758], the main viewport's
    projection captured in the `sub_002D4AE0` override) and publishes
    screen-space tags; `port_overlay.h` draws them with the port's own glyph
    pass. B logs `[ROAM] name tags: 1 remotes, 1 on screen`; the "MC3" tag is
    visible over the peer's car position (`test\shots\roam-tag-B.png`).
    The spawn nudge now steps **backward** first (`cc81720`): at the Free
    Roam start spot the car faces the customs/garage wall, so the old +10 m
    "along the road" put the second player inside the building (its tag showed
    through, its car did not). Not yet re-verified visually: after several
    runs, a second copy on this machine consistently spins at boot
    (frame 1, XPP thunks) while the first is in Cruise, so the two-instance
    test needs a clean machine or two PCs. Remaining: verify the body in the
    open, map blips, particle-budget stress, radius tuning.
11. **Aspect ratio (issue #6) - done 2026-11-20.** `RECOMP_GL_ASPECT`
    (`auto` / `4:3` / `fill`, restart to apply) is on the launcher's Display
    page, in both `recomplauncher.json` manifests, and now in the in-game Port
    Options row id 8, replacing Restore Graphics Defaults. The preset row still
    covers Performance / Balanced / Quality.
12. **Rebuild the AppImage** so the ISO reader and the messages land, then
    repackage the Windows zip for the env-file/log changes.

Superseded notes on the old list: `AppRun` already answered an ISO with a clear
message in 0.2.4a; the README platform split landed in 0.2.4a; the env file is
no longer shipped; the Windows run log is written by the launcher.

## Open items

1. **SecureAge false positive** — resubmit the launcher from the re-uploaded 0.2.4a zip,
   `0785afa4…` (111,104 B, unsigned .NET). Earlier submissions covered `c32713e3…` (0.2.4) and
   `3d562073…` (first 0.2.4a upload). Draft at
   `Desktop\ReDUB-0.2.4-release\SECUREAGE-SUBMISSION.md`; the detection name, engine version and
   timestamp still need filling in from the quarantine log. Submit at secureage.com/contact-us.
   No 0.2.4a package has had a VirusTotal scan.
2. **iOS garage artifacts** — v32 diagnostics exist. Needs a device screenshot and the trace
   run (launcher → Settings → Diagnostics → Renderer + Texgen + Feedback).
   Audit notes: `Desktop\ReDUB-iOS-graphics-audit-handoff.md`.
3. **Unify iOS into this repo** — move `ios/` + `src/ios_*` onto `main` and build from the same
   tag. Until then the 0.2.4 iOS source snapshot is the reproducible artifact.
4. **Reconcile `ReDUB-latest-frame-test`** with the merged `main` before more 60 FPS/120 work.
5. **120 FPS and frame generation are unverified on 120 Hz hardware** — correctness, cost,
   pacing and HUD handling were measured; smoothness was not. The release notes say so.
6. **Optional:** SignPath Foundation signing (free for OSS) to end SecureAge/SmartScreen
   per-build verdicts. Code signing is the only durable fix for item 1.
7. A leftover empty directory inside the `ReDUB-github-frame-test` worktree
   (`work-remix\build-frame-rate-test`) could not be removed (locked). Nothing of value inside.

## Scans

| File | VirusTotal |
|---|---|
| Windows zip `9f4f94ae…` | not scanned |
| AppImage `f0080f18…` | not scanned |
| launcher `0785afa4…` | not scanned (0.2.3 launcher was 1/71, whitelist engine) |
| Android APK `907e2c42…` | not scanned |
| iOS IPA `8a036e13…` | not scanned |
