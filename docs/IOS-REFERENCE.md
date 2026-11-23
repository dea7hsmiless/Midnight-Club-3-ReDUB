# The iOS reference tree

The tree the last known-good iOS IPA was built from is:

```
C:\Users\Donte\Documents\Codex\2026-11-18\read-this-handoff-file-take-over\work\ios
```

It produced `ReDUB-0.2.4a-release\ReDUB-0.2.4-iOS-mobile-unsigned.ipa`, which
launches on the test iPhone (A18 Pro, iOS 26/27 SDK build) and reaches the
title. Treat it as the reference for anything iOS-specific: when this tree and
that one disagree about how iOS behaves, that one is right until a change is
proven on the device.

It is not a git repository, so it cannot be diffed as a commit. Compare files
directly, ignoring `ios/dist`, `game`, build output and artwork.

## The rule

Before shipping an iOS build, diff every file the iOS build compiles that this
tree touched as part of the iOS port:

```
src/ios_launcher.m        src/ios_host.c         src/main.c
src/ios_scene.m           ios/*.py               ios/*.sh
xboxrecomp/src/platform/win32_compat.c
xboxrecomp/src/kernel/xbox_memory_layout.c
xboxrecomp/src/input/touch_pad.c
xboxrecomp/src/d3d/d3d8_gl.c
xboxrecomp/src/nv2a_gl/{nv2a_gl.c,gl_api.h}
src/games/remix/{recomp_manual.c,roam_net.c}
```

Keep the reference behaviour where the difference is not a deliberate 0.2.5
feature. Features (relay, free roam, System Link over the relay) are additive;
they should not rewrite an existing iOS path.

## What that cost once

`anon_map_fd()` in `win32_compat.c` had the reference's iOS branch removed
during the port, so iOS fell into the macOS path and called `shm_open` for the
64 MB Xbox RAM mapping. `shm_open` answers `ENOMEM` in the app sandbox, and the
title stopped the moment Play was pressed:

```
xbox_MemoryLayoutInit: CreateFileMapping failed (error 8)
[FATAL] Failed to initialise the Xbox memory layout
```

The reference uses `mkstemp` in `TMPDIR` and unlinks it: same `MAP_SHARED`
aliasing for the 28 RAM mirrors, no shared-memory namespace. The shipped 0.2.4
binary imports `_mkstemp` and not `_shm_open`; the broken 0.2.5 build imported
`_shm_open` and not `_mkstemp`. That symbol check is a fast regression test:

```
llvm-nm -u <binary> | grep -E '_mkstemp|_shm_open'
```

Expected: `_mkstemp` present, `_shm_open` absent.

## Other differences that are deliberate

- `main.c` renames `main` to `SDL_main` on iOS and links `libSDL2main`; the
  reference asserts the same thing at build time ("main.o exports SDL_main").
- `ios/package_ipa.py` emits `UIApplicationSceneManifest` (delegate
  `ReDUBSceneDelegate`) and `UILaunchScreen`, and ships `Assets/hero.jpg`,
  `Assets/logo.png` and `hd/textures/`. The reference package contains all
  three; a package without them has a blank launcher hero and stock menus.
- `nv2a_gl.c` takes a feedback snapshot of a self-sampled render target once
  per frame before the draw. The reference has the 0.2.4 per-draw version; the
  per-frame one is the 0.2.5 form and is verified on Windows.
