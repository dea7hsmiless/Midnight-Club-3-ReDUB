#!/usr/bin/env python3
"""An app icon from the game's artwork (config/<game>/app.env APP_ICON).

    tools/app_icon.py switch <artwork> <out.jpg>
    tools/app_icon.py android <artwork> <out.png>

switch: 256x256, a bare baseline JPEG (4:2:0, no EXIF, no progressive, no
ICC profile): what hbmenu and the home menu show. Anything more and the
home menu draws a '?' or nothing (NTON re-encodes the same way); hbmenu
ignores an icon that is not 256x256. Non-square artwork is cropped to the
middle square first.

android: the foreground of an adaptive launcher icon, 432x432 (108 dp at
xxxhdpi). Launchers cut the layer to their own shape (circle, squircle, ...),
so the artwork covers the whole canvas and the mask is the icon's only edge.
An earlier version inset the art at 60 dp on a blurred copy of itself; its
rounded border showed inside the launcher's mask as a stray outline, so it
was dropped (2026-11-18).

(The Mac's rounded .icns is tools/mac_icon.py, Windows' .ico
tools/windows/win_icon.py.)
"""
import os
import sys

from PIL import Image


def square(im):
    w, h = im.size
    s = min(w, h)
    return im.crop(((w - s) // 2, (h - s) // 2, (w - s) // 2 + s, (h - s) // 2 + s))


def switch(src, dst):
    im = square(Image.open(src).convert("RGB"))
    if im.size != (256, 256):
        im = im.resize((256, 256), Image.LANCZOS)
    clean = Image.frombytes("RGB", im.size, im.tobytes())   # pixels only: no EXIF, no ICC
    clean.save(dst, format="JPEG", quality=98, subsampling=2)
    print("  icon: %s -> %s (256x256 baseline JPEG)" % (os.path.relpath(src), os.path.relpath(dst)))


def android(src, dst):
    # Full-bleed: the launcher's own mask is the icon's only shape. The inset
    # badge this used to draw put its rounded border inside that mask, which
    # read as a stray outline around the icon (2026-11-18). The wordmark sits
    # in the middle third, so it survives a circular mask.
    side = 432
    im = square(Image.open(src).convert("RGB"))
    im.resize((side, side), Image.LANCZOS).save(dst, format="PNG", optimize=True)
    print("  icon: %s -> %s (Android adaptive foreground, full-bleed)"
          % (os.path.relpath(src), os.path.relpath(dst)))


def main():
    if len(sys.argv) != 4 or sys.argv[1] not in ("switch", "android"):
        sys.exit(__doc__)
    (switch if sys.argv[1] == "switch" else android)(sys.argv[2], sys.argv[3])


if __name__ == "__main__":
    main()
