#!/usr/bin/env bash
set -euo pipefail

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
GAME_DIR="${MC3_GAME_DIR:-$ROOT/game}"
if [ ! -f "$GAME_DIR/default.xbe" ]; then
  echo "ReDUB needs an extracted, legally owned Xbox Remix game folder." >&2
  echo "Set MC3_GAME_DIR to the folder containing default.xbe." >&2
  exit 2
fi

DATA_HOME="${XDG_DATA_HOME:-$HOME/.local/share}"
export MC3_GAME_DIR="$GAME_DIR"
export MC3_SAVE_DIR="${MC3_SAVE_DIR:-$DATA_HOME/redub/saves}"
export RECOMP_GL_HD_DIR="${RECOMP_GL_HD_DIR:-$ROOT/hd}"
mkdir -p "$MC3_SAVE_DIR"
exec "$ROOT/mcdub3remix" "$@"
