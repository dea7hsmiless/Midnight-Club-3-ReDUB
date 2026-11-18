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

## 0.2.4 — committed locally, not tagged or pushed

Branch `release/0.2.4`, commit `e56c836`, cut from `f3f34b2`. Nothing is pushed,
tagged or uploaded. The 0.2.3 tag and assets are untouched.

The Windows/Linux artifacts in `Desktop\ReDUB-0.2.4-release` were rebuilt from
`e56c836` (non-Windows assets in `%TEMP%\opencode\redub-appimage-work\release-out`,
AppImage manifest `source_commit` = `e56c836`). The iOS IPA is the fixed garage build
from the separate iOS tree, with `ReDUB-iOS-0.2.4-source.zip` attached for
reproducibility. `SHA256SUMS.txt` matches the folder:

| Asset | Size | SHA-256 |
|---|---|---|
| `ReDUB-0.2.4-Windows-x86_64.zip` | 73,648,584 | `b1b911c1a355bce18a189b9ff6ab9f22b6077200e4e7bb7fc99323a624a7cc78` |
| `ReDUB-0.2.4-x86_64.AppImage` | 22,133,240 | `d83379d70d1b52bc2d35ad7cffd73436252309fa67517352fd03c650d878ee44` |
| `ReDUB-0.2.4-x86_64.tar.gz` | 23,831,708 | `377e12ff8e986868324c9e9e787527108b07a2ef560d731ed23381d5ac3f8da4` |
| `mcdub3remix-0.2.4-garagefix-unsigned.ipa` | 16,393,246 | `ced12ed2b3a3655bbfc56db012133bea5ef07bdf85691f2e3cfe5147c7c1cd38` |
| `ReDUB-iOS-0.2.4-source.zip` | 4,934,967 | `7565c5369a28f51fd7ae2137cc6e01223947375975e25eec72239cf4e84a6f4e` |

Launcher: `bc6b0d046740618785dcd90f0bbfb14158d80a3d42b34cb8dfbfb3e66ee5a1d4`
(112,640 B, file version 0.2.4.0). Setup source: `f83817f2…` (12,705,590 B).
Game exe unchanged: `8b682c53…`.

## 0.2.4 code — on `release/0.2.4`

Branch `release/0.2.4` is cut from `f3f34b2`; commit `e56c836` is local and not
pushed. The 0.2.3 tag and assets are untouched. `config/*/app.env` and the
launcher say 0.2.4.

**Import without compiling**

- `.portable/Xdvdfs.cs` is the in-house Xbox disc reader, ported from
  `mcdub3-nx/src/ios_xdvdfs.c`. It detects the volume, walks the directory tree,
  checks extents, paths, cycles and duplicates, extracts with progress and
  cancellation, and verifies `default.xbe` against the region7/EUR/USA hashes.
- The reference C caps tree recursion at 128 levels; the Remix ISO's `flash`
  directory reaches 754. The port keeps the same left/current/right order but uses
  an explicit stack. Directory depth is still capped at 48, entries at 200k and
  tables at 16 MiB.
- Launcher **Add game files** accepts an ISO/XISO or an extracted folder. Import
  stages in `game-remix.importing`, verifies `default.xbe`, then swaps
  `game-remix`; a failed or cancelled import leaves the old folder, saves and
  settings alone. Replacing the game files asks first. Import has a **Cancel
  import** button and Esc shortcut; closing the launcher mid-import offers to
  cancel and exit.
- **Play** still starts the shipped exe directly. If `dist` is missing, the
  launcher says so and points at the full release zip or Settings → Advanced.
- Build/Repair moved to **Settings → Advanced → Rebuild from source** with the
  MSYS2/10 GB/20 min warning. Normal setup never compiles.
- `Build.ps1` gained `-Mode Import` (folder copy only, stops before MSYS2).
  `-Mode Doctor` no longer requires MSYS2; it reports it as optional for Advanced.
- Launcher CLI: `--import <folder-or-iso>`, `--check`, `--play`, `--build`.
  Output goes to `work-remix/logs/launcher-cli.log`; exit code 0/1/2.
- Deliberately deferred from the shared-standards list: portable-by-default on
  Linux (XDG remains the default; `--portable` is opt-in) and a flag per game
  setting (settings stay in `mcdub3remix_env.txt`; location and video/frame-rate
  flags remain).

**Verified locally**

- Synthetic XDVDFS images: normal and shifted partitions, cancellation, bad magic,
  unsafe names, directory cycles, truncated extents, duplicate files, wrong XBE.
- Real disc: 905 files / 4,555 MiB extracted in 4.3 s; all 905 match
  `D:\...\mcdub3-nx\game-remix` by SHA-256. The only differences are the 8 expected
  non-disc extras on disk (saves, cache files, `default_analysis.json`).
- Full CLI path `--import` → `--check` on the real ISO: exit 0 and the same 905-file
  match. Folder import, replacement, cancellation and `Build.ps1 -Mode Import`
  pass their fixtures.

**Independent review (2026-11-18)**

- The final shipped launcher (`bc6b0d04…`) was run with `--import` on
  `Desktop\Midnight Club 3 DUB Edition Remix.iso`: exit 0 in about 5 s (7-8 s cold);
  905 files / 4,777,234,382 B; every file matches `mcdub3-nx\game-remix` by SHA-256.
  The 8 reference-only files are the expected saves, cache files and
  `default_analysis.json`.
- `--check` exits 1 with the missing-runtime message and 0 once the shipped runtime is
  present. `--self-test` passes. Folder import and replacement leave no staging or
  `.old-*` leftovers.
- Windows zip launcher is `bc6b0d04…`, version 0.2.4.0; all asset hashes match
  `SHA256SUMS.txt`; AppImage `--version` prints `ReDUB 0.2.4`; the tarball `version`
  is `0.2.4`; the iOS IPA is the rebuilt official-bundle-id garagefix build
  (`ced12ed2…`), not the old 0.2.3 repackage.
- Fixed during review: `AppRun --game-dir` is remembered in the active portable
  config even when `--portable`/`REDUB_DATA_DIR` is applied later, and
  `Build.ps1 -Mode Import -Disc` now re-copies when game files already exist.
  `test-launcher.sh` covers both flag orders.

**Cancellation:** the Add game files page has a **Cancel import** button and an Esc
shortcut; closing the launcher mid-import offers to cancel and exit. Cancelling
deletes staging and leaves `game-remix` untouched. Verified with an off-screen
launcher harness (ISO import cancelled at 1.5 s: no game folder, no staging) and an
engine harness on an existing install (marker preserved, 2 files, staging removed).

**AppImage and tarball**

- `tools/appimage/` carries `--game-dir`, `--portable`, save/settings/fullscreen/
  resolution/frame-rate flags, a version file, deterministic tarball packaging and
  launcher tests. The 0.2.4 AppImage and tarball were rebuilt from `e56c836` in the
  pinned Ubuntu container; `LAUNCHER_TESTS_OK` and `SOURCES_OK` passed, `AppRun
  --version` prints `ReDUB 0.2.4`, the tarball `version` is `0.2.4`, and the build
  manifest `source_commit` is `e56c836`.
- The iOS IPA is the official-bundle-id 0.2.4 build with the garage render-target
  feedback fix (`glCopyTexSubImage2D` snapshot, `RECOMP_GL_FEEDBACK=0` revert),
  user-confirmed on device. 16 reader tests and `verify_packages.py` pass. The
  `ReDUB-iOS-0.2.4-source.zip` source snapshot is attached for reproducibility;
  the side-by-side beta IPA is not shipped.

## Trees

| Path | State |
|---|---|
| `...\work\publish-source` | Git repo (object store); worktree on `release/linux-appimage-0.2.2` at `b10438d` |
| `%TEMP%\opencode\redub-linux-port` | release worktree, `release/0.2.4` from `f3f34b2`; clean, code commit `e56c836` (0.2.4 above) |
| `...\Not Done\ReDUB-latest-frame-test` | Windows dev worktree on an older `main`, uncommitted 60 FPS experiments; tests/launchers live here |
| `D:\...\mcdub3-nx` | iOS tree (not Git) at 0.2.4 config with the fixed official-bundle-id IPA + shared `game-remix/` disc data |

## Open items

1. **Publish 0.2.4 on instruction** — the five assets (Windows zip, AppImage,
   tarball, official iOS IPA, iOS source snapshot), README, iOS README,
   entitlements, release body and `SHA256SUMS.txt` are ready in
   `Desktop\ReDUB-0.2.4-release`. Nothing has been tagged, pushed or uploaded.
2. **SecureAge false positive** — the 0.2.3 launcher `6e2466f6…` is 1/71 (whitelist
   engine); the 0.2.4 launcher is a new hash (`bc6b0d04…`), so the existing report
   does not cover it. Scan the new launcher/zip and submit a fresh FP report at
   secureage.com/contact-us.
3. **Other iOS renderer gaps** — the garage feedback loop is fixed; BUMPENVMAP,
   cube maps, window clip and polygon offset remain unimplemented/ranked in the
   audit. `Desktop\ReDUB-iOS-graphics-audit-handoff.md` has the list. The 0.2.4
   garage fix itself needs no further diagnosis.
4. **Unify iOS into this repo** — the 0.2.4 IPA is a rebuilt garagefix build from
   the iOS tree, not the old 0.2.3 repackage. `ReDUB-iOS-0.2.4-source.zip` is
   attached for reproducibility, but the repo still lacks `ios/` + `src/ios_*`.
   Move them onto `main` before the next iOS release; the 0.2.3 snapshot remains
   at `Desktop\ReDUB-0.2.3-support\ReDUB-iOS-0.2.3-source.zip`.
5. **Reconcile `ReDUB-latest-frame-test`** with the new `main` before more 60 FPS/120 work.
6. **Optional:** SignPath Foundation signing (free for OSS) to end SecureAge/SmartScreen
   per-build verdicts.
7. A leftover empty directory `D:\...\Not Done\ReDUB-github-frame-test\work-remix\build-frame-rate-test`
   could not be removed (locked). Nothing of value inside.

## Scans

| File | VirusTotal |
|---|---|
| Windows zip `b1b911c1…` | not scanned |
| AppImage `d83379d7…` | not scanned (0.2.3 `898d84ee…` was 0/61) |
| tarball `377e12ff…` | not scanned |
| launcher `bc6b0d04…` | not scanned (0.2.3 `6e2466f6…` was 1/71) |
| iOS IPA `ced12ed2…` | not scanned (new garagefix build; old beta `0b58f94b…` superseded) |
| iOS source snapshot `7565c536…` | not a scan target |

## Environment

- iOS toolchain: `IOS_SDK=E:\AI\iosc\xcode\...\iPhoneOS.sdk`,
  `IOS_CLANG=...\Swift\Toolchains\6.4.1+Asserts\usr\bin\clang.exe`, `IOS_LINK_TOOLS=E:\AI\iosc\tools`,
  `IOS_SDL_SOURCE=C:\Users\Donte\Documents\Codex\ios-toolchain-references\SDL2`. Build objects
  live in `%TEMP%\iosbuild` and `%TEMP%\iossdl`.
- AppImage: Docker 29.9.3; ffmpeg cached in `%TEMP%\opencode\redub-appimage-work`.
- Display 1920x1080 @ 60 Hz — 120 FPS cannot be validated here.
