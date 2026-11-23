#!/usr/bin/env python3
"""Build the unsigned iOS .app + IPA for Midnight Club 3: DUB Edition Remix.

Pattern follows NXNativeWindows/win/build_app.py: Info.plist from app.env,
Pillow-scaled loose PNG icons (CFBundleIcons), the binary as
CFBundleExecutable, Payload/<Bundle>.app zip with no signing -- the
sideload tool signs with the user's own certs.

Usage: python ios_build_app.py
"""
import os
import plistlib
import shutil
import subprocess
import sys
import zipfile
from pathlib import Path

from PIL import Image

PROJ = Path(r"D:\Launchbox\Games\Recomps\Apps\mcdub3-nx")
IOSDIR = PROJ / "ios"
OUTDIR = IOSDIR / "dist"
BIN = Path(os.environ["TEMP"]) / "iosbuild" / "mcdub3"
GAME = PROJ / "game-remix"

# config/remix/app.env (quoted) over switch/app.env
def app_env():
    vals = {}
    for f in (PROJ / "switch" / "app.env", PROJ / "config" / "remix" / "app.env"):
        for line in f.read_text(encoding="utf8", errors="replace").splitlines():
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            vals[k.strip()] = v.strip().strip('"')
    return vals

ENV = app_env()
NAME = ENV["APP_NAME"]
VERSION = ENV["APP_VERSION"]
BUNDLE_ID = ENV.get("APP_BUNDLE_ID", "com.ladastra.mcdub3remix")
ICON_SRC = PROJ / "ios" / "assets" / "redub-ios-icon.png"

APPDIR_NAME = "mcdub3remix.app"
# Loose PNG icons with the standard @2x/@3x names; CFBundleIconFiles lists the
# base names (no extension), which is what iOS resolves against the device
# scale. The 1024 is the App Store marketing icon.
ICON_SPEC = [
    ("AppIcon20x20", "@2x", 40), ("AppIcon20x20", "@3x", 60),
    ("AppIcon29x29", "@2x", 58), ("AppIcon29x29", "@3x", 87),
    ("AppIcon40x40", "@2x", 80), ("AppIcon40x40", "@3x", 120),
    ("AppIcon60x60", "@2x", 120), ("AppIcon60x60", "@3x", 180),
    ("AppIcon76x76", "@2x", 152), ("AppIcon83.5x83.5", "@2x", 167),
    ("AppIcon1024x1024", "", 1024),
]


def icons_png(appdir):
    """Use the largest ICO frame and emit opaque iPhone/iPad PNG icons."""
    source = Image.open(ICON_SRC)
    if source.format == "ICO":
        source = source.ico.getimage(max(source.ico.sizes(), key=lambda s: s[0] * s[1]))
    img = source.convert("RGBA")
    background = Image.new("RGBA", img.size, (8, 13, 26, 255))
    background.alpha_composite(img)
    img = background.convert("RGB")
    side = min(img.size)
    img = img.crop(((img.width - side) // 2, (img.height - side) // 2,
                    (img.width + side) // 2, (img.height + side) // 2))
    out = []
    for base, scale, px in ICON_SPEC:
        im = img.resize((px, px), Image.LANCZOS)
        im.save(appdir / f"{base}{scale}.png", "PNG")
        if base not in out:
            out.append(base)
    return out


def info_plist(appdir, icons):
    return {
        "CFBundleDevelopmentRegion": "en",
        "CFBundleDisplayName": NAME,
        "CFBundleExecutable": "mcdub3",
        "CFBundleIcons": {"CFBundlePrimaryIcon": {"CFBundleIconFiles": icons}},
        "CFBundleIcons~ipad": {"CFBundlePrimaryIcon": {"CFBundleIconFiles": icons}},
        "CFBundleIdentifier": BUNDLE_ID,
        "CFBundleInfoDictionaryVersion": "6.0",
        "CFBundleName": "mcdub3remix",
        "CFBundlePackageType": "APPL",
        "CFBundleShortVersionString": VERSION,
        "CFBundleSupportedPlatforms": ["iPhoneOS"],
        "CFBundleVersion": VERSION,
        "DTPlatformName": "iphoneos",
        "DTPlatformVersion": "22.0",
        "DTSDKName": "iphoneos22.0",
        "DTSDKBuild": "22.0",
        "LSRequiresIPhoneOS": True,
        "MinimumOSVersion": "16.0",
        "UIRequiredDeviceCapabilities": ["arm64"],
        "UIDeviceFamily": [1, 2],
        "UIStatusBarHidden": True,
        "UIViewControllerBasedStatusBarAppearance": False,
        "UISupportedInterfaceOrientations": [
            "UIInterfaceOrientationLandscapeLeft",
            "UIInterfaceOrientationLandscapeRight",
        ],
        "UISupportedInterfaceOrientations~ipad": [
            "UIInterfaceOrientationLandscapeLeft",
            "UIInterfaceOrientationLandscapeRight",
        ],
        "UIRequiresFullScreen": True,
    }


def build_app():
    appdir = OUTDIR / APPDIR_NAME
    if appdir.exists():
        shutil.rmtree(appdir)
    appdir.mkdir(parents=True)

    # binary, disc, saves dir, plist, icons
    shutil.copy2(BIN, appdir / "mcdub3")
    print(f"copied binary: {BIN} ({(BIN).stat().st_size / 1e6:.1f} MB)")

    print("copying game disc (4.4 GB) ...")
    gamedir = appdir / "game"
    shutil.copytree(GAME, gamedir)
    n = sum(1 for _ in gamedir.rglob("*"))
    print(f"copied disc: {n} entries")

    (appdir / "save").mkdir()
    icons = icons_png(appdir)
    print(f"icons: {icons}")

    pl = info_plist(appdir, icons)
    with open(appdir / "Info.plist", "wb") as f:
        plistlib.dump(pl, f)
    print("wrote Info.plist")

    entitlements = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>com.apple.developer.kernel.increased-memory-limit</key>
    <true/>
</dict>
</plist>
"""
    ep = OUTDIR / "mcdub3remix.entitlements"
    ep.write_text(entitlements)
    print(f"wrote {ep.name}")
    return appdir


def zip_ipa(appdir):
    ipa = OUTDIR / "mcdub3remix-unsigned.ipa"
    if ipa.exists():
        ipa.unlink()
    print("zipping Payload ... (4.4 GB, takes a while)")
    with zipfile.ZipFile(ipa, "w", zipfile.ZIP_STORED) as z:
        for p in sorted(appdir.rglob("*")):
            if p.is_file():
                z.write(p, "Payload" / Path(APPDIR_NAME) / p.relative_to(appdir))
    print(f"IPA: {ipa} ({ipa.stat().st_size / 1e9:.2} GB)")


if __name__ == "__main__":
    if not BIN.exists():
        sys.exit(f"missing binary: {BIN}")
    appdir = build_app()
    zip_ipa(appdir)
    print("done: unsigned IPA ready for sideload-tool signing")
