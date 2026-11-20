#!/usr/bin/env bash
# Build the Windows binary (mcdub3.exe), into work/build-windows-<arch>/dist/
# (natively) or work/build-wincross-<arch>/dist/ (cross) with the DLLs it
# needs and a launcher (run_mcdub3.bat).
#
# On Windows (MSYS2's CLANG64 or CLANGARM64 shell; make build there, or
# install_windows.bat for everything): MSYS2's clang and libraries, the
# shell's architecture.
#
# On macOS or Linux, a cross build with llvm-mingw (in work/tools/) and the
# same MSYS2 libraries, fetched by tools/win_deps.py:
#   tools/build_windows.sh               x86-64
#   WIN_ARCH=aarch64 tools/build_windows.sh  Windows on ARM
#
#   SCALE=2 UI_SCALE=3 SETTINGS="K=V ..."
#                                        defaults for the installed game (SCALE
#                                        and UI_SCALE 5 unless given), as
#                                        make mac-app: written to
#                                        dist/mcdub3_defaults.txt; the player's
#                                        dist/mcdub3_env.txt still wins
#   DEBUG=1 ...                          -O0 for the hand-written code
#   TARGET="xbox_kernel ..."             only these ninja targets (no dist/)
set -euo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/env.sh"
mc3_mac_native "$0" "$@"
mc3_check_lift_fresh

ARGS=()
if [ "$MC3_HOST" = windows ]; then
    ARCH="$MC3_WIN_ARCH"
    DLL_DIRS=("$MSYSTEM_PREFIX/bin")
    PY=python3
else
    ARCH="${WIN_ARCH:-x86_64}"
    case "$ARCH" in x86_64|aarch64) ;; *) mc3_fail "WIN_ARCH must be x86_64 or aarch64";; esac
    LLVM_MINGW="${MC3_LLVM_MINGW:-$(ls -d "$MC3_WORK"/tools/llvm-mingw-*/ 2>/dev/null | tail -1)}"
    LLVM_MINGW="${LLVM_MINGW%/}"
    [ -x "$LLVM_MINGW/bin/$ARCH-w64-mingw32-clang" ] \
        || mc3_fail "no llvm-mingw in work-<game>/tools: download a release (github.com/mstorsjo/llvm-mingw, *-ucrt-macos-universal or *-ucrt-ubuntu-*) and unpack it there"
    SYSROOT="$MC3_WORK/win-sysroot/$ARCH"
    PY=/opt/homebrew/bin/python3; [ -x "$PY" ] || PY=python3
    if [ ! -f "$SYSROOT/lib/pkgconfig/sdl2.pc" ]; then
        echo "== Windows libraries (MSYS2) into $SYSROOT"
        "$PY" "$MC3_REPO/tools/win_deps.py" "$ARCH" "$SYSROOT"
    fi
    export MC3_WIN_ARCH="$ARCH" MC3_LLVM_MINGW="$LLVM_MINGW" MC3_WIN_SYSROOT="$SYSROOT"
    ARGS+=(-DCMAKE_TOOLCHAIN_FILE="$MC3_REPO/tools/windows/toolchain.cmake")
    DLL_DIRS=("$SYSROOT/bin" "$LLVM_MINGW/$ARCH-w64-mingw32/bin")
fi

TYPE=RelWithDebInfo
[ "${DEBUG:-0}" = "1" ] && TYPE=Debug

# The icon (the game's artwork, config/<game>/app.env APP_ICON, rounded like
# a Windows 11 app icon) and the release version, both embedded in the exe.
# No artwork or no Pillow: no icon, still a build.
mc3_app_env
ART=$(mc3_app_artwork)
ICON="$MC3_WORK/$MC3_APP.ico"
if [ -z "$ART" ]; then
    echo "  (no icon: no artwork at ${APP_ICON:-APP_ICON})"; ICON=""
elif [ ! -f "$ICON" ] || [ "$ART" -nt "$ICON" ]; then
    ICON_OK=""
    for IPY in "$(mc3_pillow_python)" "$PY"; do
        [ -n "$IPY" ] || continue
        [ -x "$IPY" ] || command -v "$IPY" >/dev/null || continue
        "$IPY" "$MC3_REPO/tools/windows/win_icon.py" "$ART" "$ICON" 2>/dev/null \
            && { ICON_OK=1; break; }
    done
    [ -n "$ICON_OK" ] || { echo "  (no icon: no Python with Pillow)"; ICON=""; }
fi
ARGS+=(-DMC3_WIN_ICON="$ICON" -DMC3_VERSION="${APP_VERSION:-dev}")
# On by default for Windows releases; MC3_ALLOW_120=0 turns it off. Windows
# only by decision -- the Linux and macOS builds must not set this.
if [ "${MC3_ALLOW_120:-1}" = "1" ]; then
    ARGS+=(-DMC3_ALLOW_120=ON)
fi
# Extra CFLAGS for measurement builds (e.g. MC3_C_FLAGS=-DRECOMP_FRAME_RATE_TEST).
if [ -n "${MC3_C_FLAGS:-}" ]; then
    ARGS+=(-DCMAKE_C_FLAGS="$MC3_C_FLAGS")
fi
# Native (MSYS2) and cross builds keep separate trees: work/ may be shared
# between the Mac and a Windows VM, and CMake refuses a tree configured at
# another path (the first install stopped there, 2026-10-23).
if [ "$MC3_HOST" = windows ]; then
    BUILD="${BUILD_DIR:-$MC3_WORK/build-windows-$ARCH}"
else
    BUILD="${BUILD_DIR:-$MC3_WORK/build-wincross-$ARCH}"
fi
cmake -S "$MC3_REPO" -B "$BUILD" -G Ninja -DCMAKE_BUILD_TYPE="$TYPE" ${ARGS[@]+"${ARGS[@]}"} \
    -DXBOXRECOMP_DIR="$XBOXRECOMP_DIR" -DMC3_GEN_DIR="$MC3_GEN_DIR" -DMC3_VULKAN=OFF \
    -DMC3_GEN_DEBUGINFO=OFF >/dev/null
# shellcheck disable=SC2086
ninja -C "$BUILD" -j"${JOBS:-$(mc3_default_jobs)}" ${TARGET:-}

[ -n "${TARGET:-}" ] && exit 0      # single targets: no exe to package

# dist/: the exe and every DLL it pulls in (Windows looks beside the exe).
DIST="$BUILD/dist"
[ -f "$DIST/${MC3_APP}_env.txt" ] && cp "$DIST/${MC3_APP}_env.txt" "$BUILD/${MC3_APP}_env.txt.keep"
rm -rf "$DIST"; mkdir -p "$DIST"
# The player's settings file (${MC3_APP}_env.txt) is never part of a build: an
# update unzips over the folder, so shipping it would replace what the player
# set. The example is shipped, and the game copies it to the real file on the
# first run. MC3_KEEP_ENV=1 puts this machine's copy back, for local runs.
if [ "${MC3_KEEP_ENV:-0}" = 1 ] && [ -f "$BUILD/${MC3_APP}_env.txt.keep" ]; then
    cp "$BUILD/${MC3_APP}_env.txt.keep" "$DIST/${MC3_APP}_env.txt"
fi
cp "$BUILD/$MC3_APP.exe" "$DIST/"
"$PY" "$MC3_REPO/tools/windows/copy_dlls.py" "$DIST/$MC3_APP.exe" "${DLL_DIRS[@]}" "$DIST"

# Prefer the tested native SDL2 runtime: sdl2-compat failed to enumerate the Xbox BLE controller on this PC.
# It matters for correctness too: MSYS2's SDL2.dll is the sdl2-compat shim and loads
# SDL3.dll at run time (not through its import table, so copy_dlls.py cannot see
# it). Without this file the dist starts with "Failed loading SDL3 library."
if [ -f "$MC3_WORK/deps/sdl2-native/SDL2.dll" ]; then
    cp "$MC3_WORK/deps/sdl2-native/SDL2.dll" "$DIST/SDL2.dll"
else
    echo "warning: no $MC3_WORK/deps/sdl2-native/SDL2.dll; the copied SDL2.dll may be the" >&2
    echo "         sdl2-compat shim, which needs SDL3.dll at run time. Put the tested" >&2
    echo "         native SDL2.dll there before shipping a release." >&2
fi

# Settings, later wins: mcdub3_defaults.txt (this build's SCALE / SETTINGS,
# rewritten every build), then mcdub3_env.txt (the player's: never shipped,
# made from the documented example on the first run, every option commented).
{
    echo "# Defaults of this build (make build SCALE=... UI_SCALE=... SETTINGS=...)."
    echo "# Rewritten by every build: change settings in ${MC3_APP}_env.txt instead."
    echo "RECOMP_GL_SCALE=${SCALE:-2}"        # 2 by default: the published Windows release value (5 is the Mac app default and is much heavier)
    echo "RECOMP_GL_UI_SCALE=${UI_SCALE:-5}"  # 5 by default; SETTINGS below can override
    [ -n "${EDITION:-}" ] && echo "EDITION=$EDITION"   # the disc in game-<edition>\ (windows_host.c)
    for kv in ${SETTINGS:-}; do echo "$kv"; done
} | sed 's/$/\r/' > "$DIST/${MC3_APP}_defaults.txt"
if [ ! -f "$DIST/${MC3_APP}_env.example.txt" ]; then
    sed 's/$/\r/' "$MC3_REPO/switch/mcdub3_env.example.txt" > "$DIST/${MC3_APP}_env.example.txt"
fi

# The launcher, optional (the exe finds the disc and saves itself,
# src/windows_host.c): the settings files, a log per run in
# work\logs\windows_run_<time>.log (else mcdub3_log.txt beside the exe), and
# it refuses a second start.
printf '%s\r\n' \
  '@echo off' \
  'rem mcdub3-nx on Windows.' \
  'pushd "%~dp0"' \
  'rem Settings: NAME=value lines (# comments), as on the Switch. First the defaults of the' \
  'rem build (mcdub3_defaults.txt), then mcdub3_env.txt, which wins.' \
  'if exist "%~dp0mcdub3_defaults.txt" for /f "usebackq eol=# tokens=* delims=" %%l in ("%~dp0mcdub3_defaults.txt") do set "%%l"' \
  'if exist "%~dp0mcdub3_env.txt" for /f "usebackq eol=# tokens=* delims=" %%l in ("%~dp0mcdub3_env.txt") do set "%%l"' \
  'tasklist /fi "imagename eq mcdub3.exe" | find /i "mcdub3.exe" >nul && (echo The game is already running: close it first, or end mcdub3.exe in Task Manager & pause & exit /b 1)' \
  'set "LOG=%~dp0mcdub3_log.txt"' \
  'if exist "%~dp0..\..\logs\" set "LOG=%~dp0..\..\logs\windows_run.log"' \
  'if exist "%LOG%" move /y "%LOG%" "%LOG%.previous" >nul' \
  'echo Log:  %LOG%' \
  'mcdub3.exe > "%LOG%" 2>&1' \
  'set "RC=%ERRORLEVEL%"' \
  'popd' \
  'if not "%RC%"=="0" (echo The game stopped with code %RC%: see %LOG% & pause)' \
  | sed -e "s/mcdub3-nx/$MC3_SD_DIR/g" -e "s/mcdub3\([._]\)/$MC3_APP\1/g" > "$DIST/run_$MC3_APP.bat"   # the Remix: mcdub3remix
# On Windows: a desktop shortcut to the exe (its own icon), made or
# refreshed by every build -- the exe needs no launcher (src/windows_host.c).
if [ "$MC3_HOST" = windows ] && [ "${MC3_NO_DESKTOP_SHORTCUT:-0}" != 1 ]; then
    EXE_W="$(cygpath -w "$DIST/$MC3_APP.exe")"; DIST_W="$(cygpath -w "$DIST")"
    powershell.exe -NoProfile -Command "\$s = (New-Object -ComObject WScript.Shell).CreateShortcut([Environment]::GetFolderPath('Desktop') + '\\$MC3_SHORT_NAME.lnk'); \$s.TargetPath = '$EXE_W'; \$s.WorkingDirectory = '$DIST_W'; \$s.IconLocation = '$EXE_W'; \$s.Save()" \
        && echo "desktop shortcut: $MC3_SHORT_NAME -> $EXE_W" || echo "(no desktop shortcut)"
fi
cp "$MC3_REPO/tools/windows/now_playing.ps1" "$DIST/now_playing.ps1"
# Release hygiene: the package may not carry this machine's settings. The
# 0.2.4a release did (MC3_PC_MUSIC=1), so a fresh install started with PC
# music on. Dist is checked here; release-check.sh checks the finished zips.
if [ "${MC3_KEEP_ENV:-0}" = 1 ]; then
    echo "warning: MC3_KEEP_ENV=1: $DIST/${MC3_APP}_env.txt holds this machine's settings; do not package this dist." >&2
else
    "$PY" "$MC3_REPO/tools/check-release-package.py" "$DIST" || {
        echo "error: dist failed the release-package check" >&2
        exit 1
    }
fi
echo "built $DIST/$MC3_APP.exe   (double-click it, the desktop shortcut, or make run)"
