#!/usr/bin/env python3
"""Build release/ReDUB-<version>-Windows-x86_64.zip from a built dist folder.

The package is the ReDUB Launcher plus the game beside it, which is where the
launcher's recomplauncher.json looks for it:

    ReDUB Launcher.exe
    recomplauncher.json
    launcher-source.zip
    work-remix/build-windows-x86_64/dist/...

That layout is not a mistake: it is what the launcher expects, and it is what
every previous release shipped.

What deliberately does not go in: anything the player's own run produced. A
settings file (_env.txt) must never be shipped -- check-release-package.py
refuses a package that carries one, because an update would overwrite the
player's settings with defaults. The shader caches and the kernel log are the
same kind of thing: local state that would be stale or wrong on another machine.

The one exception is <app>_env.example.txt: the reference template, all
commented out, which the game copies to <app>_env.txt on first run. That is
documentation, not settings.

    python tools/make_windows_package.py [--dist DIR] [--out DIR] [--no-check]
"""
from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RELEASE = ROOT / "release"

# Files in the repo root that go beside the game.
LAUNCHER = ("ReDUB Launcher.exe", "recomplauncher.json", "launcher-source.zip")

# dist/ is the build output (DLLs, the exe, its data); these are not part of it.
SKIP_SUFFIX = (".bak", ".log", "_env.txt")
SKIP_PREFIX = ("progcache",)


def version() -> str:
    text = (ROOT / "config" / "remix" / "app.env").read_text(encoding="utf-8")
    m = re.search(r'^APP_VERSION="([^"]+)"', text, re.M)
    if not m:
        raise SystemExit("no APP_VERSION in config/remix/app.env")
    return m.group(1)


def wanted(p: Path) -> bool:
    name = p.name
    if name.endswith(SKIP_SUFFIX):
        return False
    if name.startswith(SKIP_PREFIX):
        return False
    return True


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--dist", default=str(ROOT / "work-remix" / "build-windows-x86_64" / "dist"))
    ap.add_argument("--out", default=str(RELEASE))
    ap.add_argument("--no-check", action="store_true")
    args = ap.parse_args(argv)

    dist = Path(args.dist)
    if not dist.is_dir():
        print(f"no dist folder at {dist} (build first: make windows)", file=sys.stderr)
        return 2

    files = sorted(p for p in dist.iterdir() if p.is_file() and wanted(p))
    if not any(p.suffix == ".exe" for p in files):
        print(f"{dist} has no .exe: is it a finished build?", file=sys.stderr)
        return 2
    skipped = sorted(p.name for p in dist.iterdir() if p.is_file() and not wanted(p))

    missing = [n for n in LAUNCHER if not (ROOT / n).is_file()]
    if missing:
        print("missing launcher files in the repo root: " + ", ".join(missing), file=sys.stderr)
        return 2

    ver = version()
    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / f"ReDUB-{ver}-Windows-x86_64.zip"
    if out.exists():
        out.unlink()

    arcroot = "work-remix/build-windows-x86_64/dist"
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as z:
        for name in LAUNCHER:
            src = ROOT / name
            info = zipfile.ZipInfo.from_file(src, name)
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, src.read_bytes())
        for p in files:
            info = zipfile.ZipInfo.from_file(p, f"{arcroot}/{p.name}")
            info.compress_type = zipfile.ZIP_DEFLATED
            z.writestr(info, p.read_bytes())

    size = out.stat().st_size
    print(f"built {out}  ({size:,} bytes, {len(files)} game files + {len(LAUNCHER)} launcher files)")
    if skipped:
        print("  left out: " + ", ".join(skipped))

    # Prove the two things this script exists to guarantee.
    with zipfile.ZipFile(out) as z:
        names = z.namelist()
        assert z.testzip() is None, "the archive is corrupt"
        live = [n for n in names if n.endswith("_env.txt")]
        if live:
            print("ERROR: the package carries a live settings file: " + ", ".join(live),
                  file=sys.stderr)
            return 1

    if not args.no_check:
        checker = ROOT / "tools" / "check-release-package.py"
        done = subprocess.run([sys.executable, str(checker), str(out)])
        if done.returncode != 0:
            return done.returncode
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
