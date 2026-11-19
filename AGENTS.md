# ReDUB — notes for AI agents

Midnight Club 3: DUB Edition Remix, statically recompiled for Windows, Linux, iOS and Android.

**Read `docs/STATUS.md` first.** It is the only current state; Desktop handoffs are
snapshots and may be stale.

## Working trees

| Path | What it is | Who edits it |
|---|---|---|
| `C:\Users\Donte\Documents\Codex\2026-11-16\read-add-linux-and-appimage-support\work\publish-source` | the Git repository (object store) and a worktree on a release branch | nobody, unless merging |
| `%TEMP%\opencode\redub-linux-port` | release worktree for `main` / `release/*` | PC release work |
| `D:\Launchbox\Games\Recomps\Apps\Not Done\ReDUB-latest-frame-test` | Windows dev worktree, dated `main` + local experiments (60 FPS metrics build) | PC experiments only |
| `D:\Launchbox\Games\Recomps\Apps\mcdub3-nx` | iOS tree (not a Git repo) and the shared game data in `game-remix/` | iOS work only |

One session edits one tree. Never run builds from two trees into the same output directory.
Never modify `game-remix/` — it is the user's own disc data.

## Invariants

- **One version for every game.** `config/dub/app.env` and `config/remix/app.env` must carry
  the same `APP_VERSION`; CI fails otherwise (`Tests → Version and release notes`).
- **Never move a published tag.** If a release is broken, bump the patch and publish a new
  one. `gh release create --verify-tag` prevents publishing a tag that did not exist.
- **Source files are UTF-8 with BOM.** Never rewrite them with PowerShell
  `Get-Content`/`Set-Content`; that produced a double-encoded launcher once. Use an editor or
  `[IO.File]::ReadAllText/WriteAllText` with an explicit `UTF8Encoding`.
- **`csc` gets `/codepage:65001`.** Without it the launcher compiles UTF-8 text as ANSI.
- **Release notes are the release body.** `RELEASE_NOTES.md` section `## v<version>` is the
  source; do not hand-write a body and upload it.
- **No third-party game data in any artifact.** No ISOs, XBEs, extracted discs or third-party texture packs. Project-owned ReDUB launcher/logo artwork may be bundled.

## Before saying "done"

Run the check and paste its output:

```
bash tools/release-check.sh
```

Claim → verify. It checks version parity, the notes section, mojibake in tracked text, the
launcher's BOM/codepage, and whether the tree is dirty.

## Build entry points

```
# Normal install (copy-only, no compiler): launcher -> , or
ReDUB Launcher.exe --import <folder-or-iso>
ReDUB Launcher.exe --check
ReDUB Launcher.exe --play
ReDUB Launcher.exe --build            # advanced, MSYS2

# Folder import without the launcher UI
powershell -File .portable\Build.ps1 -Mode Import -Disc <extracted-folder>

# Windows package (MSYS2 CLANG64; set MSYSTEM/CHERE_INVOKING before the login shell)
GAME=remix MC3_GAME_DIR=/d/Launchbox/Games/Recomps/Apps/mcdub3-nx/game-remix \
  MC3_NO_DESKTOP_SHORTCUT=1 bash tools/build_windows.sh

# Launcher (Windows host, .NET Framework csc; includes Launcher.cs + Xdvdfs.cs)
powershell -File .portable\Build-Launcher.ps1

# AppImage (Docker; the script's `git -C` fails inside a linked worktree, see STATUS.md)
tools/build_appimage.sh

# iOS (needs the toolchain paths in docs/STATUS.md)
python ios/build_launcher.py && python ios/package_launcher.py --beta
```

## Traps seen in this project

- The launcher extracts `launcher-source.zip` beside itself on start: never run it from the
  release-asset folder.
- `git -C` inside a linked worktree fails under MSYS2 Git (Windows `gitdir:` path); pass the
  commit and epoch to Docker explicitly.
- `7z a` with `-w` does not set the working directory; `Push-Location` first.
- Building replaces `work-remix/build-windows-x86_64/dist` entirely — repackage the zip after.
- Import stages in `game-remix.importing`, verifies `default.xbe`, then swaps `game-remix`; a
  failed or cancelled import must leave the old folder and saves untouched.
- Normal Play and  must never run `Build.ps1`; a release already ships the
  prebuilt game. `launcher-source.zip` is for Advanced rebuild only.
- Linux portable mode is opt-in (`--portable` / `REDUB_DATA_DIR`); XDG stays the default.
  Do not flip that default without a product decision.
- Game settings stay in `mcdub3remix_env.txt`; the CLI exposes location and video/frame-rate
  flags, not one flag per setting.
- VirusTotal whitelisting is per hash: any rebuild invalidates a pending false-positive
  report.

## Android mobile parity (2026-11-18)

Active Android source: `C:\Users\Donte\Desktop\ReDUB-0.2.4-build-kit\source`. The user explicitly authorized switching from iOS to Android for parity. Edit Android only during this phase; do not write into the iOS tree. Build ARM64 first; avoid the unreliable Windows emulator for verification. See `docs/ANDROID-PARITY.md`. Java source is UTF-8 without BOM (javac rejects the BOM); preserve BOM for native and other source files.
