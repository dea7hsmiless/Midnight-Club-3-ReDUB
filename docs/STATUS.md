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

1. **Linux ISO import.** The AppImage has never read a disc image (verified: `AppRun` is
   byte-identical to 0.2.4's and no C code ever had an XDVDFS reader). To match Windows: bundle
   an extractor in the AppImage, unpack into `redub-data/game-remix`, add a Linux `prepare` step
   that prints `progress` lines, then set `iso_or_folder` in the Linux manifest. Port
   `Xdvdfs.cs` or reuse `xboxrecomp/tools/xiso/xdvdfs.py`.
2. **Say what an ISO is on Linux.** Until that lands, `AppRun` should answer an ISO given to
   `--game-dir` with "this is a disc image; extract it" instead of the generic `default.xbe`
   message. Two lines, bundled with the rebuild above.
3. **README platform split.** The top-level line "ISO/XISO disc dump, or the complete extracted
   folder" reads as cross-platform. Say Windows takes either and Linux takes a folder.
4. **Stop shipping `mcdub3remix_env.txt`.** It sits inside the release, so an update replaces
   player settings. Ship `_defaults.txt` only and let the game create the env file.
5. **Windows run log.** `launcher-cli.log` records CLI actions only; the game's output is lost
   when it is started directly, so RecompLauncher reports arrive empty.
6. **Aspect ratio in the UI (issue #6).** The renderer already implements `RECOMP_GL_ASPECT`
   (`auto` / `4:3` / `fill`) and both `nv2a_gl` and `nv2a_vk` honour it. Mobile already ships a
   control — Android in its launcher (`LauncherActivity.java:993`), iOS in both its launcher
   (`src/ios_launcher.m:171`) and in-game Port Options (`src/ios_host.c:252`). **Windows and
   Linux expose nothing**: no launcher row, no Port Options row, nothing in either
   `recomplauncher.json`. Add the row on both desktops, reusing the mobile wording
   "Auto (16:9) / 4:3 / Stretch". Pair it with `RECOMP_WIDESCREEN`, `RECOMP_GL_SQUARE` and
   `RECOMP_GL_FILL`, which are invisible everywhere. Needs a relaunch: the mode is cached in a
   static.

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
