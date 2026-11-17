#!/bin/bash
set -euo pipefail
root=$(mktemp -d)
trap 'rm -rf -- "$root"' EXIT
export HOME="$root/home" XDG_CONFIG_HOME="$root/config" XDG_DATA_HOME="$root/data" XDG_CACHE_HOME="$root/cache" XDG_STATE_HOME="$root/state"
unset DISPLAY WAYLAND_DISPLAY APPIMAGE MC3_GAME_DIR MC3_SAVE_DIR MC3_SETTINGS_FILE RECOMP_GL_HD_DIR
app="$root/App Dir"
mkdir -p "$app/usr/bin" "$root/Game # One" "$root/Game Two" "$HOME"
cp /work/packaging/AppRun "$app/AppRun"; chmod +x "$app/AppRun"
touch "$root/Game # One/default.xbe" "$root/Game Two/default.xbe"
cat > "$app/usr/bin/mcdub3remix" <<'STUB'
#!/bin/sh
printf 'GAME=%s\nSAVE=%s\nSETTINGS=%s\nCWD=%s\nHD=%s\n' "$MC3_GAME_DIR" "$MC3_SAVE_DIR" "$MC3_SETTINGS_FILE" "$PWD" "$RECOMP_GL_HD_DIR"
printf 'MC3_FULLSCREEN=0\n' > "$MC3_SETTINGS_FILE"
STUB
chmod +x "$app/usr/bin/mcdub3remix"
"$app/AppRun" --help | grep -q 'Usage:'
"$app/AppRun" --version | grep -q 0.2.2
if "$app/AppRun" 2>"$root/error"; then echo 'Missing data accepted unexpectedly'; exit 1; else [ "$?" = 2 ]; fi
grep -q 'No game files are included' "$root/error"
printf '# Comment\r\n"../Game # One"\r\n' > "$app/redub.game-location.txt"
"$app/AppRun"
grep -Fx "GAME=$root/Game # One" "$root/state/redub/latest.log"
grep -Fx "SETTINGS=$root/config/redub/mcdub3remix_env.txt" "$root/state/redub/latest.log"
grep -Fx "HD=$app/hd" "$root/state/redub/latest.log"
MC3_GAME_DIR="$root/Game Two" "$app/AppRun"
grep -Fx "GAME=$root/Game Two" "$root/state/redub/latest.log"
"$app/AppRun" --game-dir "$root/Game Two"
grep -Fx "$root/Game Two" "$root/config/redub/game-location.txt"
"$app/AppRun"
grep -Fx "GAME=$root/Game Two" "$root/state/redub/latest.log"
[ -f "$root/state/redub/previous.log" ]
[ ! -f "$app/usr/bin/mcdub3remix_env.txt" ]
if "$app/AppRun" --game-dir "$root/missing" 2>/dev/null; then exit 1; else [ "$?" = 2 ]; fi
printf 'LAUNCHER_TESTS_OK\n'
