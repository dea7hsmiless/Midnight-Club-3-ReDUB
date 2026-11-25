# ReDUB project handoff — after 0.2.5

Written 2026-11-25, the day 0.2.5 was published. Read this first; then
`docs/0.2.5-HANDOFF.md` for the crash/freeze investigation in forensic detail,
and `docs/0.2.6-PLAN.md` for the next release.

## 1. Where 0.2.5 stands

| | |
|---|---|
| Release | https://github.com/dea7hsmiless/Midnight-Club-3-ReDUB/releases/tag/v0.2.5 (published, latest, not a draft) |
| Tag `v0.2.5` | `f839442` |
| Remote `main` | later than the tag by documentation commits only (the relay-server note and this handoff). That is normal; the tag is what the release was built from. |
| Assets | Windows zip, Linux AppImage, Linux tar.gz, Android APK, iOS IPA, relay server zip, `SHA256SUMS.txt` |
| Verified | 7/7 asset digests match GitHub's stored `sha256:` digests; release body fetched back and byte-compared |

The binaries in the release were built from the **merged** tree (`github/main`'s
seven 2026-11-20 commits integrated on 2026-11-24), not from the pre-merge
branch. Game exe hash in the Windows zip: `6886431f…`.

## 2. What 0.2.5 contains

`RELEASE_NOTES.md` is the source of truth (the release body is generated from
its `## v0.2.5` section plus the standard intro paragraph). Headlines:

- Boot no longer freezes on the Rockstar logo (quiet logging by default).
- The GPU walker's lost-stream stall and the fence that never arrived.
- Aspect ratio: Auto 16:9 / 4:3 / Stretch, launcher and in-game, plus the 1.6:1
  framing fix.
- Online stays in the room (non-blocking socket; a relay datagram counts as the
  hub being alive); room numbers as invites; your name on the Online page.
- Saves complete reliably (APC delivery preserves guest CPU state); non-ASCII
  install paths work (wide-character host paths).
- Mods / unlock-everything, quit watchdog, package guards.

## 3. Repository state

- `main` is the only branch that matters; `github` is the public remote.
- The local working copy is `C:\Users\Donte\Desktop\ReDUB-0.2.4-build-kit\source`
  (the folder name says 0.2.4; it is current). It holds the only full history
  before publishing — after the push, GitHub is the backup.
- `work-remix/` (generated code, build trees, `gen/`) is gitignored. Nothing in
  there ships except what the packaging scripts copy into `dist/`.

## 4. Building

Windows (MSYS2 CLANG64):

```powershell
cd work-remix\build-windows-x86_64
$env:PATH='C:\msys64\clang64\bin;' + $env:PATH
ninja mcdub3remix.exe
# dist\next to the build carries the exe + 98 DLLs; the zip is built from there
```

Android (from MSYS2 bash):

```bash
MC3_ABI=universal bash /c/Users/Donte/Documents/android_build_runner.sh
# -> work-remix/android/mcdub3remix-0.2.5-universal.apk
```

iOS:

```bash
bash ios/build_ios.sh 12
python ios/package_ipa.py --binary work-remix/ios-build/mcdub3remix \
  --disc game-remix --no-disc --no-env --out work-remix/ios-package
```

Linux (Docker Desktop must be **running**; it tends to shut itself down):

```powershell
powershell -File %TEMP%\opencode\build_appimage_win.ps1   # rc=0 on success
# -> work-remix/appimage/release-out/ReDUB-0.2.5-x86_64.{AppImage,tar.gz}
```

The Linux and AppImage packaging runs `tools/release-check.sh`,
`tools/check-release-package.py`, the launcher test and the compliance source
collector inside the pinned Ubuntu container.

## 5. Verifying a build before it ships

Windows, in this order (each is a real gate; do not skip because "it built"):

1. `powershell -File tools/soak-windows.ps1 -Runs 3 -WatchdogSecs 240 -Disc <disc>`
   — three boots to Cruise with online up. Must print `all 3 runs clean`.
2. `powershell -File tools/windows/Test-Release-Stability.ps1 -Minutes 6 -GameDir <disc>`
   — idle, memory steady-state, handles, close, event log. `Problems: none`.
3. `powershell -File tools/windows/Test-Release-Idle.ps1 -Seconds 420 -GameDir <disc>`
   — the memory curve, when you need to know *when* it grows.
4. `cmake -S xboxrecomp/tests/stability -B work/native-stability -G Ninja -DCMAKE_BUILD_TYPE=Debug`
   then `cmake --build work/native-stability -- -k 0` and
   `ctest --test-dir work/native-stability -E pushbuffer`. Expect 8/8.

Reference results for the 0.2.5 binary: boots 142.5/142.3/143.7 s to Cruise;
7 min idle flat at 441 MB with handles flat; close 0.14 s; native tests 8/8.

## 6. Traps that cost real time (do not rediscover these)

- **DLLs.** An exe run from a folder without the 98 DLLs dies in the loader:
  no banner, no log, no crash event, "exited at 2s". A test folder must be a
  full copy of `dist`. The two harnesses above do this automatically, keyed by
  the exe's hash so a rebuilt exe is never tested stale.
- **`MC3_SKIP_MOVIES=1` is a broken path.** It stalls inside the
  `mc3intro.bik` open; two of three boots never reached the title. The soak
  plays the movies (what ships) and takes `-SkipMovies 1` only to reproduce.
- **Two game instances** (e.g. one from the release folder, one under test)
  share the room and the CPU, and make every timing number meaningless. Kill
  strays before a soak.
- **Memory growth is not a leak by default.** The main menu loads the world in
  a one-shot burst (+~190 MB) and then plateaus. Judge the second half of a run.
  A build that "stays flat" may simply be stalled and doing less work.
- **Never cap single-object INFINITE waits.** Doing so turned a freeze into a
  divide-by-zero 114 s after boot. Only Bink's two-object wait is bounded
  (`kernel_sync.c` explains why).
- **A stale `dist`.** The soak runs `dist\mcdub3remix.exe`. After a rebuild,
  copy it there and compare hashes, or you are testing yesterday's binary.
- **Stale remote view.** A week-old fetch makes "our branch" look complete.
  `git fetch` before any release decision — 0.2.5 nearly shipped without seven
  upstream commits.

## 7. Open items

Product:

- **Online Race is transport-verified, not two-device verified.** A loopback
  relay and a synthetic peer prove packets arrive; two real machines clicking
  Create Match / Join Game has not been done.
- **Android and iOS packages were built and version-checked, not run** on a
  device in this cycle.
- **`pushbuffer_test` does not link on MSYS2 CLANG64** (`clock_gettime64` in a
  bare test exe; the game links fine). `file_concurrency` is POSIX-only and is
  not registered on Windows.
- **macOS is not built or tested.** The port code is there
  (`tools/setup_mac.sh`, `make mac-app`, `if(APPLE)` branches, `Darwin` host).
  Cheapest useful step: a `macos-latest` CI job that only builds. A shippable
  mac app also needs an Apple Developer account, notarization, and one real
  play-test on a Mac.
- Free Roam and adaptive resolution are 0.2.6 (see `docs/0.2.6-PLAN.md`).

Housekeeping:

- The relay server source exists **only** in the release zip — `relay/` is not
  tracked in git. If it should be maintained, it belongs in the repository.
- The release folder (`C:\Users\Donte\Desktop\ReDUB-0.2.5-release`) holds the
  deliverables and a 9-entry `SHA256SUMS.txt` keyed to its subfolders. The
  release page sums file uses basenames, as 0.2.4a did.
- The extracted disc used by all tests and the launcher lives at
  `C:\Users\Donte\Documents\ReDUB-game\game-remix` (4.4 GB). `ReDUB-Archive`
  (old releases, backups) is in the Recycle Bin; `ReDUB-0.2.5-work` (planning
  notes) is also there.

## 8. Publishing a release

The 0.2.5 procedure, reusable:

1. `git fetch github`; `git rev-list --left-right --count github/main...HEAD`
   must be `0 <n>`. Merge or rebase first if it is not.
2. Build and verify (section 5). Rebuild **every** platform after any runtime
   change; a Windows-only rebuild makes the packages disagree.
3. `tools/release-check.sh` must print PASS and the tree must be clean.
4. Tag: `git tag -a v0.2.5 -m "..."` on the commit the artifacts were built from.
5. Stage assets under the names this repo uses (`ReDUB-<ver>-Android-universal.apk`,
   `ReDUB-<ver>-iOS-mobile-unsigned.ipa`), with `SHA256SUMS.txt` in **basename**
   form (that is what 0.2.4a publishes).
6. Body: the `## v0.2.5` section of `RELEASE_NOTES.md` behind the standard intro
   paragraph. Write it to a **UTF-8 file** and publish with `--notes-file`.
7. `gh release create v0.2.5 --title 'ReDUB 0.2.5' --notes-file <body> --latest <assets>`
8. Verify, do not assume: fetch the release JSON as raw bytes
   (`cmd /c "gh api ... > file.json"` — PowerShell redirection re-encodes),
   decode UTF-8 strictly, compare `body` to the file **byte for byte**, check
   `U+FFFD` is 0, and compare every asset digest against the local hash.
   Keep the JSON, body and sums as a backup with the tool and timestamp.

Encoding rules (non-negotiable, from AGENTS.md): never capture GitHub JSON
through PowerShell's text decoding; UTF-8 in, UTF-8 out, byte-compare on read
back; check em dashes, arrows and any non-ASCII explicitly.

## 9. Reaching the project

- Repo: `dea7hsmiless/Midnight-Club-3-ReDUB` (public). `gh` is authenticated as
  that account with `repo` + `workflow` scopes.
- The game needs the user's own disc; nothing ships game data.
- Version lives in `config/dub/app.env`, `config/remix/app.env`, `RELEASE_NOTES.md`
  and the package names. `tools/release-check.sh` enforces that they agree.
