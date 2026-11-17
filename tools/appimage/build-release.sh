#!/bin/bash
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
source_commit=${REDUB_SOURCE_COMMIT:-$(cat /work/source-commit.txt 2>/dev/null || printf unknown)}
apt-get update -qq
apt-get install -y -qq --no-install-recommends build-essential cmake ninja-build pkg-config libsdl2-dev libdbus-1-dev libgl-dev libglew-dev libepoxy-dev libssl-dev nasm curl ca-certificates xz-utils patchelf file python3 desktop-file-utils appstream dpkg-dev > /work/apt-build.log 2>&1
mkdir -p /work/downloads /work/ffmpeg-build /work/ffmpeg-install
cd /work/downloads
if [ ! -f ffmpeg-6.1.4.tar.xz ]; then curl -fL --retry 3 https://ffmpeg.org/releases/ffmpeg-6.1.4.tar.xz -o ffmpeg-6.1.4.tar.xz; fi
printf '%s  %s\n' a231e3d5742c44b1cdaebfb98ad7b6200d12763e0b6db9e1e2c5891f2c083a18 ffmpeg-6.1.4.tar.xz | sha256sum -c -
sha256sum ffmpeg-6.1.4.tar.xz > ffmpeg-source.sha256
if [ ! -d /work/ffmpeg-6.1.4 ]; then tar -xJf ffmpeg-6.1.4.tar.xz -C /work; fi
cd /work/ffmpeg-build
if [ ! -f ffbuild/config.mak ] || ! grep -q "^CONFIG_BINK_DECODER=yes$" ffbuild/config.mak; then
 /work/ffmpeg-6.1.4/configure --prefix=/work/ffmpeg-install --disable-everything --disable-autodetect --disable-programs --disable-doc --disable-static --enable-shared --enable-pic --disable-network --disable-avdevice --disable-avfilter --disable-swscale --disable-swresample --disable-postproc --enable-avformat --enable-avcodec --enable-avutil --enable-demuxer=bink --enable-decoder=bink,binkaudio_dct,binkaudio_rdft --enable-protocol=file > /work/ffmpeg-configure.log
fi
make -j8 > /work/ffmpeg-build.log 2>&1
make install >> /work/ffmpeg-build.log 2>&1
export PKG_CONFIG_PATH=/work/ffmpeg-install/lib/pkgconfig
cmake -S /work/release-source -B /work/linux-build -G Ninja -DMC3_GAME=remix -DMC3_GEN_DIR=/work/release-gen -DCMAKE_BUILD_TYPE=Release -DMC3_VERSION=0.2.2 -DMC3_BUILD_ID="$source_commit" -DMC3_VULKAN=OFF > /work/cmake-configure.log 2>&1
cmake --build /work/linux-build -j8 > /work/linux-build.log 2>&1
ldd /work/linux-build/mcdub3remix > /work/binary-dependencies.txt
printf 'BUILD_OK\n'
