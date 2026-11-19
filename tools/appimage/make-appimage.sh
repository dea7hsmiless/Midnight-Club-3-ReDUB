#!/bin/bash
set -euo pipefail
cd /work/downloads
[ -f appimagetool-x86_64.AppImage ] || curl -fL --retry 3 https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage -o appimagetool-x86_64.AppImage
printf '%s  %s\n' e8c1169c4af66c3184d7ee110713d4be04befe671fa358e2c1591ee840553b03 appimagetool-x86_64.AppImage | sha256sum -c -
chmod +x appimagetool-x86_64.AppImage
./appimagetool-x86_64.AppImage --appimage-extract > /work/appimagetool-extract.log
./squashfs-root/AppRun --help > /work/appimagetool-help.txt 2>&1
[ -f runtime-x86_64 ] || curl -fL --retry 3 https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64 -o runtime-x86_64
printf '%s  %s\n' 617aa77314d57c8b8e4fae9c3f0b35af7b30d5529cb69e98f64f92510ed98a46 runtime-x86_64 | sha256sum -c -
mkdir -p /work/release-out
VERSION=${REDUB_VERSION:-0.2.4}
ARCH=x86_64 VERSION=$VERSION SOURCE_DATE_EPOCH=${SOURCE_DATE_EPOCH:-1794810395} ./squashfs-root/AppRun --runtime-file /work/downloads/runtime-x86_64 --comp zstd /work/AppDir "/work/release-out/ReDUB-$VERSION-x86_64.AppImage"
cd /work/release-out
# A plain tarball of the same AppDir, for launchers that cannot run an AppImage
# (Flatpak and similar cannot execute one from inside their sandbox).
tar --sort=name --mtime="@${SOURCE_DATE_EPOCH:-1794810395}" --owner=0 --group=0 --numeric-owner \
    -czf "ReDUB-$VERSION-x86_64.tar.gz" -C /work/AppDir .
sha256sum "ReDUB-$VERSION-x86_64.AppImage" "ReDUB-$VERSION-x86_64.tar.gz" > SHA256SUMS.txt
