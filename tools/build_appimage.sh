#!/usr/bin/env bash
# Build the supported Remix AppImage in a disposable Ubuntu container.
set -euo pipefail
repo=$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
gen=${MC3_GEN_DIR:-$repo/work-remix/gen}
work=${REDUB_APPIMAGE_WORK_DIR:-$repo/work-remix/appimage}
[[ -f "$gen/recomp_funcs.h" ]] || { echo 'Generate Remix game code first: GAME=remix make lift' >&2; exit 2; }
mkdir -p "$work"
gen=$(CDPATH= cd -- "$gen" && pwd)
work=$(CDPATH= cd -- "$work" && pwd)
commit=$(git -C "$repo" rev-parse HEAD)
epoch=$(git -C "$repo" show -s --format=%ct HEAD)
printf '%s\n' "$commit" > "$work/source-commit.txt"
docker run --rm \
 --mount "type=bind,source=$work,target=/work" \
 --mount "type=bind,source=$repo,target=/work/release-source,readonly" \
 --mount "type=bind,source=$gen,target=/work/release-gen,readonly" \
 --mount "type=bind,source=$repo/tools/appimage,target=/work/packaging,readonly" \
 -e "REDUB_SOURCE_COMMIT=$commit" -e "SOURCE_DATE_EPOCH=$epoch" \
 ubuntu@sha256:fe81560990fae86dc4743202b2cb4d3df98d0bcf949173cecef15ce1127ef852 \
 bash -c 'bash /work/packaging/build-release.sh && python3 /work/packaging/package.py && bash /work/packaging/test-launcher.sh && bash /work/packaging/make-appimage.sh && python3 /work/packaging/collect-sources.py'
echo "Built AppImage: $work/release-out/ReDUB-0.2.2-x86_64.AppImage"
echo "Matching library sources: $work/dependency-sources"
