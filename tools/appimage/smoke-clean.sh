#!/bin/bash
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq --no-install-recommends libgbm1 libdrm2 libgl1-mesa-dri libglx-mesa0 libgl1 xvfb xauth x11-utils imagemagick python3 procps > /tmp/apt.log 2>&1
mkdir -p /tmp/redub-test; cd /tmp/redub-test
cp /work/release-out/ReDUB-0.2.2-x86_64.AppImage ./ReDUB.AppImage; chmod +x ReDUB.AppImage
./ReDUB.AppImage --appimage-extract > /tmp/extract.log
export APPIMAGE=/tmp/redub-test/ReDUB.AppImage
export HOME=/tmp/redub-profile
mkdir -p "$HOME"
APPIMAGE_EXTRACT_AND_RUN=1 ./ReDUB.AppImage --version
APPIMAGE_EXTRACT_AND_RUN=1 ./ReDUB.AppImage --help | head -4
squashfs-root/AppRun --diagnose > /work/clean-diagnose-${TEST_DISTRO}.log
if grep -q 'not found' /work/clean-diagnose-${TEST_DISTRO}.log; then cat /work/clean-diagnose-${TEST_DISTRO}.log; exit 1; fi
# Verify actual decoder registry instead of trusting build options.
python3 - <<'PY'
import ctypes
from pathlib import Path
import os
lib=ctypes.CDLL('/tmp/redub-test/squashfs-root/usr/lib/libavcodec.so.60')
lib.avcodec_find_decoder_by_name.argtypes=[ctypes.c_char_p];lib.avcodec_find_decoder_by_name.restype=ctypes.c_void_p
for name in (b'binkvideo',b'binkaudio_dct',b'binkaudio_rdft'):
 assert lib.avcodec_find_decoder_by_name(name),name
print('BINK_DECODERS_OK')
PY
export MC3_GAME_DIR=/game SDL_AUDIODRIVER=dummy LIBGL_ALWAYS_SOFTWARE=1
export MC3_SETTINGS_FILE="$HOME/.config/redub/mcdub3remix_env.txt"
mkdir -p "$(dirname "$MC3_SETTINGS_FILE")"
printf 'MC3_FULLSCREEN=0\nRECOMP_GL_SCALE=1\nRECOMP_GL_VSYNC=0\n' > "$MC3_SETTINGS_FILE"
Xvfb :99 -screen 0 1280x720x24 -nolisten tcp > /tmp/xvfb.log 2>&1 &
export DISPLAY=:99
sleep 1
squashfs-root/AppRun &
launcher=$!
sleep 35
if ! kill -0 "$launcher" 2>/dev/null; then cat "$HOME/.local/state/redub/latest.log"; exit 1; fi
cp "$HOME/.local/state/redub/latest.log" /work/clean-startup-${TEST_DISTRO}.log
child=$(pgrep -P "$launcher" -x mcdub3remix)
if grep -q "\[CRASH\]" /work/clean-startup-${TEST_DISTRO}.log; then exit 1; fi
import -window root /work/clean-${TEST_DISTRO}.png
# Stop only this isolated test's game process and wrapper.
kill -TERM "$launcher" "$child"
wait "$launcher" || :
printf 'CLEAN_SMOKE_OK %s\n' "$TEST_DISTRO"
