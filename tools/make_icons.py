#!/usr/bin/env python3
"""One icon for every platform, from one picture.

    python tools/make_icons.py --from "C:/.../ReDUB-iOS-App-Icon-v2.png"
    python tools/make_icons.py            # regenerate from the master

The master is launcher/art/icon.png (1024x1024). Everything else is derived:

  Windows exe      build-time, tools/windows/win_icon.py, from APP_ICON
  Android          build-time, tools/app_icon.py, adaptive icon from APP_ICON
  Switch           build-time, tools/app_icon.py (256x256 JPEG), from APP_ICON
  launcher .ico    .portable/Build-Icon.ps1, from launcher/art/icon.png
  Linux AppImage   tools/appimage/icons/{16..256}.png, written here
  iOS              ios/assets/redub-ios-icon.png, written here

So this script writes the two that are checked in, and --from updates the master
that the other platforms read. Pillow needed.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MASTER = ROOT / "launcher" / "art" / "icon.png"
IOS = ROOT / "ios" / "assets" / "redub-ios-icon.png"
APPIMAGE_DIR = ROOT / "tools" / "appimage" / "icons"
APPIMAGE_SIZES = [16, 32, 48, 64, 128, 256]
MASTER_SIDE = 1024


def square(img):
    """The centre square, so nothing is squashed if the art is not square."""
    from PIL import Image
    side = min(img.size)
    left = (img.width - side) // 2
    top = (img.height - side) // 2
    return img.crop((left, top, left + side, top + side))


def main(argv):
    ap = argparse.ArgumentParser()
    ap.add_argument("--from", dest="src", default="",
                    help="new master picture to install as launcher/art/icon.png")
    ap.add_argument("--master", default=str(MASTER))
    args = ap.parse_args(argv)

    try:
        from PIL import Image
    except ImportError:
        print("Pillow is needed: pip install pillow", file=sys.stderr)
        return 2

    master = Path(args.master)
    if args.src:
        src = Path(args.src)
        if not src.is_file():
            print(f"no such picture: {src}", file=sys.stderr)
            return 2
        img = square(Image.open(src).convert("RGB"))
        img = img.resize((MASTER_SIDE, MASTER_SIDE), Image.LANCZOS)
        master.parent.mkdir(parents=True, exist_ok=True)
        img.save(master, format="PNG", optimize=True)
        print(f"master: {master.relative_to(ROOT)} ({MASTER_SIDE}x{MASTER_SIDE}, from {src.name})")
    if not master.is_file():
        print(f"no master at {master}: pass --from <picture> the first time", file=sys.stderr)
        return 2

    img = square(Image.open(master).convert("RGB"))

    # iOS: opaque, no alpha -- an App Store icon may not be transparent.
    img.resize((MASTER_SIDE, MASTER_SIDE), Image.LANCZOS).save(IOS, format="PNG", optimize=True)
    print(f"ios:      {IOS.relative_to(ROOT)} ({MASTER_SIDE}x{MASTER_SIDE})")

    APPIMAGE_DIR.mkdir(parents=True, exist_ok=True)
    for px in APPIMAGE_SIZES:
        out = APPIMAGE_DIR / f"{px}.png"
        img.resize((px, px), Image.LANCZOS).save(out, format="PNG", optimize=True)
    print(f"appimage: {APPIMAGE_DIR.relative_to(ROOT)}/{{{','.join(map(str, APPIMAGE_SIZES))}}}.png")
    print("windows/android/switch/launcher icons come from APP_ICON and the master at build time")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
