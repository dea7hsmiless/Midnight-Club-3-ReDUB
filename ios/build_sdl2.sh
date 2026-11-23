#!/usr/bin/env bash
# Build SDL2 as a static library for arm64 iPhoneOS.
#
# The 0.2.4 IPA linked against one built this way; it lived in %TEMP%\iossdl and
# went with the temp folder, so it is rebuilt here. Static, because an iOS app
# may not load a dylib from outside the bundle and SDL is not a system library.
#
#   bash ios/build_sdl2.sh
set -euo pipefail
cd "$(dirname "$0")/.." || exit 2

SRC=${IOS_SDL:-/c/Users/Donte/Documents/Codex/ios-toolchain-references/SDL2}
BUILD=${IOS_SDL_BUILD:-work-remix/iossdl}
CLANG=${IOS_CLANG:-/c/Users/Donte/AppData/Local/Programs/Swift/Toolchains/6.4.1+Asserts/usr/bin/clang.exe}
SDK=${IOS_SDK:-/e/AI/iosc/xcode/Platforms/iPhoneOS.platform/Developer/SDKs/iPhoneOS.sdk}
LD64=${IOS_LD64:-/e/AI/iosc/tools}
DEPLOY=${IOS_DEPLOY:-15.0}

win_path() {
    case "$1" in
        /?/*) printf '%s:/%s\n' "$(echo "${1:1:1}" | tr 'a-z' 'A-Z')" "${1:3}" ;;
        *) printf '%s\n' "$1" ;;
    esac
}
SDK_W=$(win_path "$SDK")
LD64_W=$(win_path "$LD64")
SRC_W=$(win_path "$SRC")

echo "SDL2 source: $SRC_W"
echo "building in: $BUILD (iOS $DEPLOY, static)"

# SDL's iOS scene support needs the patch the 0.2.4 build used (the launcher and
# the game window must belong to the same UIScene). Applied if not already.
if [ -f ios/sdl-scenes.patch ] && ! grep -q "redub-before-scenes" "$SRC/.redub-patched" 2>/dev/null; then
    if (cd "$SRC" && git apply --check "$(pwd)/../../../../ios/sdl-scenes.patch" 2>/dev/null); then
        echo "applying ios/sdl-scenes.patch"
        (cd "$SRC" && git apply "$(pwd)/../../../../ios/sdl-scenes.patch") || true
        : > "$SRC/.redub-patched"
    fi
fi

cmake -S "$SRC_W" -B "$BUILD" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$(win_path "$(pwd)/ios/ios.toolchain.cmake")" \
    -DIOS_SDK="$SDK_W" \
    -DIOS_LLVM="C:/Users/Donte/AppData/Local/Programs/Swift/Toolchains/6.4.1+Asserts/usr" \
    -DIOS_LD64="$LD64_W" \
    -DIOS_STUB_BIN="E:/AI/iosc/tools" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET="$DEPLOY" \
    -DCMAKE_BUILD_TYPE=Release \
    -DSDL_SHARED=OFF -DSDL_STATIC=ON -DSDL_TEST=OFF \
    -DSDL_OPENGL=ON -DSDL_OPENGLES=ON -DSDL_METAL=ON \
    -DSDL_JOYSTICK=ON -DSDL_HAPTIC=ON -DSDL_SENSOR=ON -DSDL_POWER=ON \
    -DSDL_AUDIO=ON -DSDL_LOADSO=OFF -DSDL_FILESYSTEM=ON -DSDL_TIMERS=ON \
    -DSDL_THREADS=ON \
    2>&1 | tail -15

cmake --build "$BUILD" --target SDL2-static 2>&1 | tail -20

ls -la "$BUILD"/libSDL2*.a 2>/dev/null || { echo "no libSDL2.a produced"; exit 1; }
