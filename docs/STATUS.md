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

Branch `release/0.2.4`, cut from `f3f34b2`. The code and docs are committed locally;
nothing is pushed, tagged or uploaded. The 0.2.3 tag and assets are untouched.

Candidate artifacts are in `Desktop\ReDUB-0.2.4-release` (build staging:
`%TEMP%\opencode\redub-0.2.4-package`). They were built from the dirty worktree
before the final commit, so rebuild the Windows zip, AppImage and tarball from the
committed tree before publishing (the AppImage manifest embeds the source commit).
The table below is the candidate set, not the publish set.

| Asset | Size | SHA-256 |
|---|---|---|
| `ReDUB-0.2.4-Windows-x86_64.zip` | 76,299,756 | `4df83c4579e0485c945c02cfdd03d21fb857fe4e9ea17dc81ec1854a2ddf7b3f` |
| `ReDUB-0.2.4-x86_64.AppImage` | 22,133,240 | `2f93a828d98738bb973f2253958703c0c2c8eb73bfae11a2256b052de8195257` |
| `ReDUB-0.2.4-x86_64.tar.gz` | 23,831,771 | `6b2da30c9ed8717538d99023d7f35dd927cbeb47a8711622cc3402957a17cd2d` |
| `mcdub3remix-beta-0.2.4-unsigned.ipa` | 16,392,674 | `0b58f94b6049520c14b39c9fd0810c9c52824bf1af0e1e47f441b40a0fe83f0a` |

Launcher: `d371cc823896874311f26be8a25600a2932758fb6ddba7eba7c4330ad8a31e7a`
(110,592 B, file version 0.2.4.0). Setup source: `1566c909…` (12,703,011 B).
Game exe unchanged: `8b682c53…`.

## 0.2.4 code — on `release/0.2.4`

Branch `release/0.2.4` is cut from `f3f34b2`; it is committed locally and not pushed.
The 0.2.3 tag and assets are untouched. `config/*/app.env` and the launcher say
0.2.4.

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
  settings alone. Replacing the game files asks first.
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

- The shipped launcher (`d371cc82…`) was run with `--import` on
  `Desktop\Midnight Club 3 DUB Edition Remix.iso`: exit 0 in 7.1 s; 905 files /
  4,777,234,382 B; every file matches `mcdub3-nx\game-remix` by SHA-256. The 8
  reference-only files are the expected saves, cache files and `default_analysis.json`.
- `--check` exits 1 with the missing-runtime message and 0 once the shipped runtime is
  present. `--self-test` passes. Folder import and replacement leave no staging or
  `.old-*` leftovers.
- Windows zip launcher is `d371cc82…`, version 0.2.4.0; all four asset hashes match
  `SHA256SUMS.txt`; AppImage `--version` prints `ReDUB 0.2.4`; the tarball `version`
  is `0.2.4`; the IPA differs from 0.2.3 only in `Info.plist` (0.2.4, same beta id).
- Fixed during review: `AppRun --game-dir` is remembered in the active portable
  config even when `--portable`/`REDUB_DATA_DIR` is applied later, and
  `Build.ps1 -Mode Import -Disc` now re-copies when game files already exist.
  `test-launcher.sh` covers both flag orders.

**Known limitation:** the launcher import has no Cancel button and blocks closing
while it runs; the reader supports cancellation but the UI does not expose it yet.

**AppImage and tarball**

- `tools/appimage/` carries `--game-dir`, `--portable`, save/settings/fullscreen/
  resolution/frame-rate flags, a version file, deterministic tarball packaging and
  launcher tests. The 0.2.4 AppImage and tarball were rebuilt in the pinned Ubuntu
  container; `AppRun --version` prints `ReDUB 0.2.4` and the tarball `version` is
  `0.2.4`. Launcher tests and metadata validation passed.
- The iOS beta IPA was repackaged at 0.2.4 from the existing 0.2.3 binary; no iOS
  source changed in this release.

## Trees

| Path | State |
|---|---|
| `...\work\publish-source` | Git repo (object store); worktree on `release/linux-appimage-0.2.2` at `b10438d` |
| `%TEMP%\opencode\redub-linux-port` | release worktree, `release/0.2.4` from `f3f34b2`, dirty (0.2.4 work above) |
| `...\Not Done\ReDUB-latest-frame-test` | Windows dev worktree on an older `main`, uncommitted 60 FPS experiments; tests/launchers live here |
| `D:\...\mcdub3-nx` | iOS tree (not Git) at 0.2.4 config with the unchanged 0.2.3 binary + shared `game-remix/` disc data |

## Open items

1. **Publish 0.2.4 on instruction** — the four assets, README, iOS README,
   entitlements, release body and `SHA256SUMS.txt` are ready in
   `Desktop\ReDUB-0.2.4-release`. Nothing has been tagged, pushed or uploaded.
2. **SecureAge false positive** — the 0.2.3 launcher `6e2466f6…` is 1/71 (whitelist
   engine); the 0.2.4 launcher is a new hash (`d371cc82…`), so the existing report
   does not cover it. Scan the new launcher/zip and submit a fresh FP report at
   secureage.com/contact-us.
3. **iOS garage artifacts** — unexplained; v32 diagnostics exist. Needs a device
   screenshot and the trace run (launcher → Settings → Diagnostics → Renderer +
   Texgen + Feedback). Audit notes: `Desktop\ReDUB-iOS-graphics-audit-handoff.md`.
4. **Unify iOS into this repo** — the 0.2.4 IPA is the unchanged 0.2.3 beta binary
   repackaged with the 0.2.4 version, not a rebuilt iOS. Move `ios/` + `src/ios_*`
   onto `main` before iOS inherits the launcher changes. The 0.2.3 iOS source
   snapshot is `Desktop\ReDUB-0.2.3-support\ReDUB-iOS-0.2.3-source.zip`.
5. **Reconcile `ReDUB-latest-frame-test`** with the new `main` before more 60 FPS/120 work.
6. **Optional:** SignPath Foundation signing (free for OSS) to end SecureAge/SmartScreen
   per-build verdicts.
7. A leftover empty directory `D:\...\Not Done\ReDUB-github-frame-test\work-remix\build-frame-rate-test`
   could not be removed (locked). Nothing of value inside.

## Scans

| File | VirusTotal |
|---|---|
| Windows zip `4df83c45…` | not scanned |
| AppImage `2f93a828…` | not scanned (0.2.3 `898d84ee…` was 0/61) |
| tarball `6b2da30c…` | not scanned |
| launcher `d371cc82…` | not scanned (0.2.3 `6e2466f6…` was 1/71) |
| iOS beta IPA `0b58f94b…` | not scanned |

## Environment

- iOS toolchain: `IOS_SDK=E:\AI\iosc\xcode\...\iPhoneOS.sdk`,
  `IOS_CLANG=...\Swift\Toolchains\6.4.1+Asserts\usr\bin\clang.exe`, `IOS_LINK_TOOLS=E:\AI\iosc\tools`,
  `IOS_SDL_SOURCE=C:\Users\Donte\Documents\Codex\ios-toolchain-references\SDL2`. Build objects
  live in `%TEMP%\iosbuild` and `%TEMP%\iossdl`.
- AppImage: Docker 29.9.3; ffmpeg cached in `%TEMP%\opencode\redub-appimage-work`.
- Display 1920x1080 @ 60 Hz — 120 FPS cannot be validated here.
