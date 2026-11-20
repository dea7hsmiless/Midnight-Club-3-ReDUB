# ReDUB status

_Updated 2026-11-19. This file is the single current state. Overwrite it; do not append._

## Released

**v0.2.4a** — https://github.com/dea7hsmiless/Midnight-Club-3-ReDUB/releases/tag/v0.2.4a
Tag `v0.2.4a` = `3a64190`; published 2026-11-20. Adds RecompLauncher support (Windows and
Linux), the `KEY = value` settings reader, and Redump disc matching. Full release, so it is the
GitHub "latest" and what RecompLauncher installs.

| Asset | Bytes | SHA-256 |
|---|---|---|
| `ReDUB-0.2.4-Windows-x86_64.zip` | 81,060,490 | `e9abdea5538df9a55cf178f247de7e2e558ed35d29f3a69d16e2a412cd97a62f` |
| `ReDUB-0.2.4-x86_64.AppImage` | 22,145,528 | `f0080f18da540f878df3e300c061f34402c35eaffc9c918c326798b71b1c8aab` |
| `ReDUB-0.2.4-x86_64.tar.gz` | 23,850,799 | `557ace2f4b4f959fbf231f374ac5fb981143bd7b29d7e2412cbeca74dd4e7ffb` |

Launcher `3d56207303fc683269712f894c7ada1e0103d2df2c4052bbe767ed3d6f3584ae` (111,104 B);
game exe `cd1e3a7e24ea98d666c39b58c0d6698c0a40f88fb9aff3ac34146c30a649d6c7` (34,111,488 B).
Android and iOS assets are the unchanged 0.2.4 files.

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

## Open items

1. **SecureAge false positive** — launcher `c32713e3…` (110,592 B, unsigned .NET). Submission
   draft at `Desktop\ReDUB-0.2.4-release\SECUREAGE-SUBMISSION.md`; the detection name, engine
   version and timestamp still need filling in from the quarantine log. Submit at
   secureage.com/contact-us. The published zip has not had a fresh VT scan.
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
| Windows zip `cbcdcaf5…` | not scanned |
| AppImage `46bc6fdf…` | not scanned |
| launcher `c32713e3…` | not scanned (0.2.3 launcher was 1/71, whitelist engine) |
| Android APK `907e2c42…` | not scanned |
| iOS IPA `8a036e13…` | not scanned |
