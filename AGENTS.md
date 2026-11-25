# ReDUB — notes for AI agents

Midnight Club 3: DUB Edition Remix, statically recompiled for Windows, Linux, iOS and Android.

**Read `docs/STATUS.md` first.** It is the only current state; Desktop handoffs are
snapshots and may be stale. Handoff notes are gitignored (`*HANDOFF*.md`): they stay on
the machine, never in the published repository.

## Working trees

| Path | What it is | Who edits it |
|---|---|---|
| `<repo-root>` | the Git repository (object store) and a worktree on a release branch | nobody, unless merging |
| `%TEMP%\opencode\redub-linux-port` | release worktree for `main` / `release/*` | PC release work |
| `<windows-dev-worktree>` | Windows dev worktree, dated `main` + local experiments (60 FPS metrics build) | PC experiments only |
| `<ios-tree>` | iOS tree (not a Git repo) and the shared game data in `game-remix/` | iOS work only |

Paths are placeholders on purpose: use the copy you are working in.

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
- **No game data in any artifact.** No ISOs, XBEs, extracted discs or `hd/` packs.
- **No machine-local paths in tracked files.** Never write `C:\Users\<name>`, `/Users/<name>`,
  `/home/<name>`, `%USERPROFILE%`, drive letters (`D:\`, `E:\`) or other absolute paths from a
  developer machine into a committed file. Use placeholders (`<repo-root>`, `<game-dir>`) and
  relative paths; this is a public repository.
- **Features, options and mods are different things.**
  - *Port feature*: something the Xbox game already did and the port had to rebuild (System
    Link/LAN Play, split-screen, controller and save handling). Always available or a plain
    setting; never presented as a mod, and the only kind a store claim may lean on.
  - *Port option*: presentation, performance, input or startup behaviour (resolution, FXAA,
    aspect, frame rate, PC music, autostart, skip movies). Lives under Settings.
  - *Mod*: changes what the game does — its rules, progression or content (unlock
    everything, drift scoring, extra cars, free-roam multiplayer). Off by default, labelled
    "(restart)", tested on a throwaway profile, and never part of a store claim.
  - A mod must not change the simulation while networked: two peers with different mods
    desync. Keep the mod layer out of anything System Link runs.

## Before saying "done"

Run the check and paste its output:

```
bash tools/release-check.sh
```

Claim → verify. It checks version parity, the notes section, mojibake in tracked text, the
launcher's BOM/codepage, and whether the tree is dirty.

## Build entry points

```
# Windows package (MSYS2 CLANG64; set MSYSTEM/CHERE_INVOKING before the login shell)
GAME=remix MC3_GAME_DIR=/d/<game-dir>/game-remix \
  MC3_NO_DESKTOP_SHORTCUT=1 bash tools/build_windows.sh

# Launcher (Windows host, .NET Framework csc)
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
- VirusTotal whitelisting is per hash: any rebuild invalidates a pending false-positive
  report.
