#!/bin/bash
set -euo pipefail
root=$(mktemp -d)
trap 'rm -rf -- "$root"' EXIT
export HOME="$root/home" XDG_CONFIG_HOME="$root/config" XDG_DATA_HOME="$root/data" XDG_CACHE_HOME="$root/cache" XDG_STATE_HOME="$root/state"
unset DISPLAY WAYLAND_DISPLAY APPIMAGE MC3_GAME_DIR MC3_SAVE_DIR MC3_SETTINGS_FILE RECOMP_GL_HD_DIR REDUB_DATA_DIR REDUB_HOME MC3_FRAME_RATE RECOMP_GL_SCALE MC3_FULLSCREEN MC3_RESOLUTION
app="$root/App Dir"
# Portable by default: the root is redub-data beside the package.
rb="$app/redub-data"
mkdir -p "$app/usr/bin" "$root/Game # One" "$root/Game Two" "$HOME"
cp /work/packaging/AppRun "$app/AppRun"; chmod +x "$app/AppRun"
printf '0.2.4-test\n' > "$app/version"
touch "$root/Game # One/default.xbe" "$root/Game Two/default.xbe"
cat > "$app/usr/bin/mcdub3remix" <<'STUB'
#!/bin/sh
printf 'GAME=%s\nSAVE=%s\nSETTINGS=%s\nCWD=%s\nHD=%s\nFRAME=%s\nSCALE=%s\nFULLSCREEN=%s\nRES=%s\n' \
  "$MC3_GAME_DIR" "$MC3_SAVE_DIR" "$MC3_SETTINGS_FILE" "$PWD" "$RECOMP_GL_HD_DIR" \
  "${MC3_FRAME_RATE:-}" "${RECOMP_GL_SCALE:-}" "${MC3_FULLSCREEN:-}" "${MC3_RESOLUTION:-}"
printf 'MC3_FULLSCREEN=0\n' > "$MC3_SETTINGS_FILE"
STUB
chmod +x "$app/usr/bin/mcdub3remix"
"$app/AppRun" --help | grep -q 'Usage:'
"$app/AppRun" --version | grep -q 0.2.4-test
if "$app/AppRun" 2>"$root/error"; then echo 'Missing data accepted unexpectedly'; exit 1; else [ "$?" = 2 ]; fi
grep -q 'No game files are included' "$root/error"
if "$app/AppRun" --frame-rate 144 2>/dev/null; then echo 'Bad frame rate accepted'; exit 1; else [ "$?" = 2 ]; fi
if "$app/AppRun" --bogus 2>/dev/null; then echo 'Unknown option accepted'; exit 1; else [ "$?" = 2 ]; fi
printf '# Comment\r\n"../Game # One"\r\n' > "$app/redub.game-location.txt"
"$app/AppRun"
grep -Fx "GAME=$root/Game # One" "$rb/state/latest.log"
grep -Fx "SETTINGS=$rb/config/mcdub3remix_env.txt" "$rb/state/latest.log"
grep -Fx "HD=$app/hd" "$rb/state/latest.log"
MC3_GAME_DIR="$root/Game Two" "$app/AppRun"
grep -Fx "GAME=$root/Game Two" "$rb/state/latest.log"
"$app/AppRun" --game-dir "$root/Game Two"
grep -Fx "$root/Game Two" "$rb/config/game-location.txt"
"$app/AppRun"
grep -Fx "GAME=$root/Game Two" "$rb/state/latest.log"
[ -f "$rb/state/previous.log" ]
[ ! -f "$app/usr/bin/mcdub3remix_env.txt" ]
# Video and frame-rate flags map onto the game's environment.
"$app/AppRun" --game-dir "$root/Game Two" --frame-rate 60 --scale 3 --fullscreen 1 --resolution 1920x1080
grep -Fx 'FRAME=60' "$rb/state/latest.log"
grep -Fx 'SCALE=3' "$rb/state/latest.log"
grep -Fx 'FULLSCREEN=1' "$rb/state/latest.log"
grep -Fx 'RES=1920x1080' "$rb/state/latest.log"
# Portable mode keeps everything inside one directory and is remembered.
"$app/AppRun" --game-dir "$root/Game Two" --portable "$root/portable"
grep -Fx "SAVE=$root/portable/data/saves" "$root/portable/state/latest.log"
grep -Fx "SETTINGS=$root/portable/config/mcdub3remix_env.txt" "$root/portable/state/latest.log"
[ -f "$app/redub.portable" ]
rm -f "$app/redub.portable"
# Flag order: --game-dir is remembered in the active config, even when
# --portable (or REDUB_DATA_DIR) is applied after it.
"$app/AppRun" --game-dir "$root/Game Two" --portable "$root/portable-order"
grep -Fx "$root/Game Two" "$root/portable-order/config/game-location.txt"
"$app/AppRun" --portable "$root/portable-order"
grep -Fx "GAME=$root/Game Two" "$root/portable-order/state/latest.log"
rm -f "$app/redub.portable"
mkdir -p "$root/portable-env"
REDUB_DATA_DIR="$root/portable-env" "$app/AppRun" --game-dir "$root/Game Two"
grep -Fx "$root/Game Two" "$root/portable-env/config/game-location.txt"
if "$app/AppRun" --game-dir "$root/missing" 2>/dev/null; then exit 1; else [ "$?" = 2 ]; fi
# An existing XDG install keeps its own folders and says so once.
app2="$root/App Dir 2"
mkdir -p "$app2/usr/bin" "$root/data/redub/saves"
cp /work/packaging/AppRun "$app2/AppRun"; chmod +x "$app2/AppRun"
printf '0.2.4-test\n' > "$app2/version"
cp "$app/usr/bin/mcdub3remix" "$app2/usr/bin/mcdub3remix"
"$app2/AppRun" --game-dir "$root/Game Two" 2>"$root/xdg-note"
grep -Fx "GAME=$root/Game Two" "$root/state/redub/latest.log"
grep -q 'keeping the existing data' "$root/xdg-note"
printf 'LAUNCHER_TESTS_OK\n'
