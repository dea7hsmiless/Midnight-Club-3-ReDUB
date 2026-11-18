#!/usr/bin/env bash
# Release pre-flight: the checks that have actually failed on this project.
#   bash tools/release-check.sh
# Exit 0 = safe to build/tag/publish. Any FAIL line is a blocker.
set -uo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.." || exit 2

fail=0
note() { printf '%s\n' "$*"; }
ok()   { printf 'ok    %s\n' "$*"; }
bad()  { printf 'FAIL  %s\n' "$*"; fail=1; }
warn() { printf 'warn  %s\n' "$*"; }

# 1. One version for every game (CI: Tests -> Version and release notes)
if [ -f config/dub/app.env ] && [ -f config/remix/app.env ]; then
  ver=$(bash -c 'source config/dub/app.env; echo "$APP_VERSION"')
  case "$ver" in
    [0-9]*.[0-9]*.[0-9]*) ok "config/dub APP_VERSION=$ver" ;;
    *) bad "config/dub APP_VERSION '$ver' is not x.y.z" ;;
  esac
  for cfg in config/*/app.env; do
    v=$(bash -c 'source "$1"; echo "$APP_VERSION"' _ "$cfg")
    [ "$v" = "$ver" ] && ok "$cfg = $ver" || bad "$cfg = $v, expected $ver (one version for every game)"
  done
else
  warn "config/*/app.env not found; skipping version parity"
fi

# 2. Release notes section exists for that version
if [ -n "${ver:-}" ] && [ -f RELEASE_NOTES.md ]; then
  if grep -q "^## v$ver\b" RELEASE_NOTES.md; then
    ok "RELEASE_NOTES.md has '## v$ver'"
  else
    bad "RELEASE_NOTES.md has no '## v$ver' section (needed for the release body)"
  fi
fi

# 3. Mojibake in tracked text (the launcher and the release body both hit this)
mojibake=$(git grep -I -n -e 'â€' -e '├ó' -e 'Ã¢â' -e $'\uFFFD' -- . ':!tools/release-check.sh' 2>/dev/null | head -20)
if [ -n "$mojibake" ]; then
  bad "mojibake in tracked text:"
  printf '%s\n' "$mojibake"
else
  ok "no mojibake in tracked text"
fi

# 4. Launcher sources: UTF-8 BOM + compiler told it is UTF-8
for cs in .portable/Launcher.cs .portable/Xdvdfs.cs; do
  if [ -f "$cs" ]; then
    head3=$(head -c 3 "$cs" | od -An -tx1 | tr -d ' \n')
    [ "$head3" = "efbbbf" ] && ok "$cs has a UTF-8 BOM" \
      || bad "$cs lost its UTF-8 BOM (UTF-8 text will compile as ANSI)"
  fi
done
if [ -f .portable/Launcher.cs ]; then
  if grep -q -- '/codepage:65001' .portable/Build-Launcher.ps1 2>/dev/null; then
    ok "Build-Launcher.ps1 passes /codepage:65001"
  else
    bad "Build-Launcher.ps1 is missing /codepage:65001"
  fi
  if [ -f .portable/Xdvdfs.cs ]; then
    grep -q -- 'Xdvdfs.cs' .portable/Build-Launcher.ps1 2>/dev/null \
      && ok "Build-Launcher.ps1 compiles Xdvdfs.cs" \
      || bad "Build-Launcher.ps1 does not compile Xdvdfs.cs"
  fi
else
  warn "no .portable/Launcher.cs in this tree; skipping launcher checks"
fi

# 5. Working tree state (informational; a dirty tree cannot be tagged cleanly).
# MSYS2 Git reports CRLF-only differences that the Windows Git in this repo does not,
# so compare ignoring CR at EOL and look at untracked files separately.
real_changes=$(git diff --ignore-cr-at-eol --name-only 2>/dev/null)
untracked=$(git ls-files --others --exclude-standard 2>/dev/null)
if [ -n "$real_changes" ] || [ -n "$untracked" ]; then
  warn "working tree is dirty:"
  { printf '%s\n' "$real_changes"; printf '%s\n' "$untracked"; } | sed '/^$/d' | head -10
else
  ok "working tree clean (line-ending-only differences ignored)"
fi

# 6. Version already released? A published tag must not move.
if [ -n "${ver:-}" ] && git rev-parse -q --verify "refs/tags/v$ver" >/dev/null; then
  warn "tag v$ver already exists locally ($(git rev-parse --short "v$ver^{commit}")); never move a published tag — bump the version"
fi

echo
if [ "$fail" -eq 0 ]; then
  note "release-check: PASS"
else
  note "release-check: FAIL"
fi
exit "$fail"
