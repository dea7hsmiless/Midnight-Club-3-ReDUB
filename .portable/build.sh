#!/usr/bin/env bash
set -euo pipefail
source tools/env.sh
if [[ ! -f "$MC3_GAME_DIR/default.xbe" ]]; then
    if [[ -n "${MC3_SOURCE_ISO:-}" ]]; then
        bash tools/extract_disc.sh "$(cygpath -u "$MC3_SOURCE_ISO")"
    else
        mc3_need_game
    fi
fi
if ! command -v clang >/dev/null || ! command -v cmake >/dev/null || ! command -v ninja >/dev/null \
   || ! python3 -c 'import capstone' >/dev/null 2>&1; then
    bash tools/setup_windows.sh
fi
if [[ "${MC3_BUILD_MODE:-Build}" == Rebuild || ! -f "$MC3_GEN_DIR/recomp_funcs.h" ]]; then
    bash tools/regen.sh
elif ! (mc3_check_lift_fresh) >/dev/null 2>&1; then
    # Seeds and function boundaries require analysis; wrapper-only changes do not.
    if [[ "$MC3_SEEDS" -nt "$MC3_GEN_DIR/.manual_overrides" \
       || "$MC3_CONFIG/not_functions.json" -nt "$MC3_GEN_DIR/.manual_overrides" ]]; then
        bash tools/regen.sh
    else
        LIFT_ONLY=1 bash tools/regen.sh
    fi
else
    echo 'Using cached game-code conversion.'
fi
bash tools/build_windows.sh
