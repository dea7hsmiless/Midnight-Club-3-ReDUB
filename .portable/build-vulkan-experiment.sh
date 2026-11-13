#!/usr/bin/env bash
set -euo pipefail
if [[ ! -f /clang64/include/vulkan/vulkan.h ]]; then
    echo 'Install CLANG64 Vulkan headers: pacman -S mingw-w64-clang-x86_64-vulkan-headers'
    exit 1
fi
cmake -S . -B work-remix/build-windows-vulkan-experimental -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DMC3_GAME=remix -DMC3_VULKAN=ON \
    -DMC3_GEN_DEBUGINFO=OFF -DMC3_GEN_DIR="$PWD/work-remix/gen"
ninja -C work-remix/build-windows-vulkan-experimental -j8
