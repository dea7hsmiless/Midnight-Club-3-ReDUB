#!/usr/bin/env bash
# Build the iOS binary (not the .app yet): every object, then link.
#
#   bash ios/build_ios.sh            # compile everything, report failures
#   bash ios/build_ios.sh -k 12      # keep going, show 12 errors per file
#
# Runs in MSYS2 CLANG64 on Windows with the Swift toolchain's clang targeting
# arm64-apple-ios, ld64.lld for the link, and the iPhoneOS SDK from Xcode.
set -uo pipefail
cd "$(dirname "$0")/.." || exit 2

CLANG=${IOS_CLANG:-/c/Users/Donte/AppData/Local/Programs/Swift/Toolchains/6.4.1+Asserts/usr/bin/clang.exe}
SDK=${IOS_SDK:-/e/AI/iosc/xcode/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk}
SDLDIR=${IOS_SDL:-/c/Users/Donte/Documents/Codex/ios-toolchain-references/SDL2}

# These are Windows programs: a POSIX-style -isysroot path is silently rejected
# (clang then uses the Windows sysroot and every standard header vanishes), so
# hand them C:/... / E:/... instead. The relative source paths stay POSIX.
win_path() {
    local p=$1
    case "$p" in
        /?/*) printf '%s:/%s\n' "$(echo "${p:1:1}" | tr 'a-z' 'A-Z')" "${p:3}" ;;
        *) printf '%s\n' "$p" ;;
    esac
}
SDK=$(win_path "$SDK")
SDLDIR=$(win_path "$SDLDIR")
OBJ=${IOS_OBJ:-work-remix/ios-build}
LIST=${IOS_LIST:-$OBJ/sources.txt}
ERRMAX=${1:-5}

mkdir -p "$OBJ"
[ -f "$LIST" ] || { echo "no source list: $LIST (ios/make_ios_sources.py writes it)"; exit 2; }

INC=(-I src -I config/remix -I work-remix/gen -I xboxrecomp/include -I xboxrecomp/src
     -I xboxrecomp/src/platform -I xboxrecomp/src/kernel -I xboxrecomp/src/d3d
     -I xboxrecomp/src/nv2a -I xboxrecomp/src/audio -I xboxrecomp/src/input
     -I xboxrecomp/src/video -I xboxrecomp/src/apu -I xboxrecomp/src/usb
     -I xboxrecomp/src/nv2a_gl -I xboxrecomp/src/net -I xboxrecomp/src/net/roam
     -I "$SDLDIR/include")

# String defines, built so clang receives the quotes. Writing -DX=\"text\" in a
# bash array loses the quoting (the backslashes survive, the quotes do not, and
# the value expands to an identifier); printf keeps them as literal argv text.
DEFS=(-D_GNU_SOURCE)
DEFS+=("$(printf -- '-DMC3_APP=%s' '"mcdub3remix"')")
DEFS+=("$(printf -- '-DMC3_SD_DIR=%s' '"mcdub3remix-nx"')")
DEFS+=("$(printf -- '-DMC3_GAME_TITLE=%s' '"Midnight Club 3: DUB Edition Remix"')")
DEFS+=("$(printf -- '-DMC3_VERSION=%s' '"0.2.5"')")
DEFS+=("$(printf -- '-DMC3_BUILD_ID=%s' '"ios-v0.2.5"')")
DEFS+=("$(printf -- '-DMC3_PACKAGE_EDITION=%s' '"eur"')")
DEFS+=("$(printf -- '-DMC3_EDITION=%s' '"eur"')")

BASE=(-target arm64-apple-ios16.0 -isysroot "$SDK" -O2 -w -fno-strict-aliasing -fwrapv
      "${INC[@]}" "${DEFS[@]}")

fail=0
n=0
# A header newer than the last full compile invalidates every object: the
# per-file check below only compares source timestamps, so without this a header
# edit silently leaves stale objects (it did: the GL entry-point table grew but
# gl_api.o still had the old pointers, and the link reported them undefined).
FORCE=0
if [ -f "$OBJ/.headers" ]; then
    stale=$(find src config work-remix/gen xboxrecomp -name '*.h' -newer "$OBJ/.headers" -print -quit 2>/dev/null)
    [ -n "$stale" ] && FORCE=1
else
    FORCE=1
fi
[ "$FORCE" = 1 ] && echo "a header changed: recompiling everything"
while IFS= read -r src <&3; do
    src=${src%$'\r'}                       # the list may have CRLF endings
    [ -n "$src" ] || continue
    n=$((n + 1))
    obj="$OBJ/$(echo "$src" | tr '/' '_' | sed 's/\.c$/.o/;s/\.m$/.o/')"
    [ "$FORCE" = 0 ] && [ -f "$obj" ] && [ "$obj" -nt "$src" ] && continue
    extra=()
    case "$src" in *.m) extra=(-fobjc-arc -fblocks);; esac
    if ! out=$("$CLANG" "${BASE[@]}" "${extra[@]}" -c "$src" -o "$obj" 2>&1); then
        fail=$((fail + 1))
        echo "FAIL $src"
        echo "$out" | grep -m "$ERRMAX" 'error:' | sed 's/^/     /'
    fi
done 3< "$LIST"

echo "---"
echo "compiled $n, failed $fail"
[ "$fail" -eq 0 ] || exit 1
touch "$OBJ/.headers"
echo "linking..."

# The Swift toolchain's own ld64.lld refuses "-platform_version ios" (LLD 21 is
# too old); the LLVM 23 linker in the iOS tool folder accepts it, and is what the
# 0.2.4 IPA was linked with.
LD=${IOS_LD:-/e/AI/iosc/tools/ld64.lld.exe}
# ios/build_sdl2.sh produces this; it is not inside the SDL source tree.
SDLLIB=${IOS_SDL_LIB:-work-remix/iossdl}
LIBS=("$SDLLIB/libSDL2.a")
[ -f "$SDLLIB/libSDL2main.a" ] && LIBS+=("$SDLLIB/libSDL2main.a")
[ -f "$SDLLIB/libSDL2.a" ] || { echo "no $SDLLIB/libSDL2.a (run ios/build_sdl2.sh)"; exit 2; }
objs=$(find "$OBJ" -name '*.o' | tr '\n' ' ')
"$LD" -arch arm64 -platform_version ios 16.0 16.0 -syslibroot "$SDK" -o "$OBJ/mcdub3remix" \
    $objs "${LIBS[@]}" \
    -framework UIKit -framework Foundation -framework CoreGraphics -framework QuartzCore \
    -framework OpenGLES -framework AudioToolbox -framework AVFoundation -framework CoreAudio \
    -framework CoreVideo -framework GameController -framework Metal -framework MediaPlayer \
    -framework CoreMotion -framework Security -framework SystemConfiguration \
    -framework CoreHaptics -framework ImageIO -framework CoreText -framework UniformTypeIdentifiers \
    -framework CoreBluetooth \
    -lSystem -lobjc -lc++ -lz -liconv 2>&1 | head -40
[ -f "$OBJ/mcdub3remix" ] && echo "binary: $OBJ/mcdub3remix ($(stat -c %s "$OBJ/mcdub3remix") bytes)"
