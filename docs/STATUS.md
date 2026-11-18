# ReDUB status

_Updated 2026-11-18. This file is the single current state. Overwrite it; do not append._

## Released

**v0.2.3** — https://github.com/dea7hsmiless/Midnight-Club-3-ReDUB/releases/tag/v0.2.3
Tag `v0.2.3` = `a6f14fe` on `main`; CI green (Tests, AppImage packaging checks).

| Asset | SHA-256 |
|---|---|
| `ReDUB-0.2.3-Windows-x86_64.zip` | `494e66554e3834963906e17024a687cfa77511a51ba75f80e0c57635134c6997` |
| `ReDUB-0.2.3-x86_64.AppImage` | `898d84ee8a55eb9a05914bc829a507a688fee32882baf3e5424ffcd51d52d39a` |
| `mcdub3remix-beta-0.2.3-unsigned.ipa` | `7d17ee7be3ac498193af6a41089d709abecbabc7be3930dd7c58add2fd2ac373` |

Launcher inside the zip: `6e2466f6777cf3410b248b8414e2b5ba3ddaf243debf426b46c9d916323401bb`
(90,112 B). Game exe inside: `8b682c53…`.

The 0.2.3 release was hotfixed twice in place (first zip `dfce38a9…` had a
double-encoded launcher; the release body had the same bug). Same tag, same filenames.

## Open items

1. **SecureAge false positive** — launcher `6e2466f6…` is 1/71 (whitelist engine). Attachment
   ready in `Desktop\ReDUB-0.2.3-support\fp-submission\ReDUB-Launcher-6e2466f6.zip`; submit at
   secureage.com/contact-us. The zip needs a fresh VT scan too (`494e6655…`).
2. **iOS garage artifacts** — unexplained; v32 diagnostics exist. Needs a device screenshot
   and the trace run (launcher → Settings → Diagnostics → Renderer + Texgen + Feedback).
   Audit notes: `Desktop\ReDUB-iOS-graphics-audit-handoff.md`.
3. **Unify iOS into this repo at 0.2.4** — move `ios/` + `src/ios_*` onto `main`, build from
   the same tag. Until then 0.2.3 iOS source is archived at
   `Desktop\ReDUB-0.2.3-support\ReDUB-iOS-0.2.3-source.zip`.
4. **Reconcile `ReDUB-latest-frame-test`** with the new `main` before more 60 FPS/120 work.
5. **Optional:** SignPath Foundation signing (free for OSS) to end SecureAge/SmartScreen
   per-build verdicts.
6. A leftover empty directory inside the `ReDUB-github-frame-test` worktree
   (`work-remix\build-frame-rate-test`) could not be removed (locked). Nothing of value inside.

## Scans

| File | VirusTotal |
|---|---|
| Windows zip `494e6655…` | not scanned (previous `dfce38a9…` was 0/66) |
| AppImage `898d84ee…` | 0/61 |
| launcher `6e2466f6…` | not scanned (previous `a22b74fe…` was 1/71) |
| iOS beta IPA `7d17ee7b…` | not scanned |
