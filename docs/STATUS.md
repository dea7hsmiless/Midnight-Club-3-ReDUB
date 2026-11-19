# ReDUB status

_Updated 2026-11-19. This file is the single current state. Overwrite it; do not append._

## Released

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
