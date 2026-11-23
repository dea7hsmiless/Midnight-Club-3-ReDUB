#!/usr/bin/env python3
"""Package the iOS binary as an unsigned .app and .ipa.

    python ios/package_ipa.py --binary work-remix/ios-build/mcdub3remix \
        --disc <game-remix folder> --join 162.35.56.66 --name Donte

The IPA is unsigned on purpose: Sideloadly/AltStore signs it with your own
certificate and applies mcdub3remix.entitlements (which asks for the increased
memory limit the game needs).

Two details that matter for testing online:

- UIFileSharingEnabled and LSSupportsOpeningDocumentsInPlace are set, so the
  app's Documents folder appears in the Files app and mcdub3remix_env.txt can be
  copied in or edited there.
- The settings file the app is built with is placed INSIDE the bundle as
  mcdub3remix_env.txt, and ios_boot() copies it to Documents on first run if
  none exists. That is a convenience for a test build: --join writes the server
  line into it so the app arrives configured. Pass --no-env for a package with
  no settings file at all (what a release should do).
"""
from __future__ import annotations

import argparse
import plistlib
import shutil
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IOS = ROOT / "ios"
APP_NAME = "mcdub3remix.app"
EXE_NAME = "mcdub3remix"
BUNDLE_ID = "com.ladastra.mcdub3remix"

# base name, suffix, pixels
ICONS = [
    ("AppIcon20x20", "@2x", 40), ("AppIcon20x20", "@3x", 60),
    ("AppIcon29x29", "@2x", 58), ("AppIcon29x29", "@3x", 87),
    ("AppIcon40x40", "@2x", 80), ("AppIcon40x40", "@3x", 120),
    ("AppIcon60x60", "@2x", 120), ("AppIcon60x60", "@3x", 180),
    ("AppIcon76x76", "@2x", 152), ("AppIcon83.5x83.5", "@2x", 167),
    ("AppIcon1024x1024", "", 1024),
]

ENTITLEMENTS = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>com.apple.developer.kernel.increased-memory-limit</key>
    <true/>
</dict>
</plist>
"""

ENV_HEADER = """\
# ReDUB on iOS. KEY=VALUE per line; '#' starts a comment. Read at start, so
# restart the app after editing. One line is the whole online setup:
#
#     MC3_ROAM_JOIN=<the server's address>
#
# MC3_NETPLAY=1 makes System Link (races) use the relay as well.
"""


def app_version() -> str:
    text = (ROOT / "config" / "remix" / "app.env").read_text(encoding="utf-8")
    for line in text.splitlines():
        if line.startswith("APP_VERSION="):
            return line.split("=", 1)[1].strip().strip('"')
    return "0.0.0"


def icons_png(appdir: Path) -> list[str]:
    src = IOS / "assets" / "redub-ios-icon.png"
    try:
        from PIL import Image
    except ImportError:
        print("no Pillow: skipping icons (Sideloadly shows a blank icon)")
        return []
    if not src.exists():
        print(f"no icon artwork at {src}: skipping icons")
        return []
    img = Image.open(src)
    if getattr(img, "format", "") == "ICO":
        img = img.ico.getimage(max(img.ico.sizes(), key=lambda s: s[0] * s[1]))
    img = img.convert("RGBA")
    bg = Image.new("RGBA", img.size, (8, 13, 26, 255))
    bg.alpha_composite(img)
    img = bg.convert("RGB")
    side = min(img.size)
    img = img.crop(((img.width - side) // 2, (img.height - side) // 2,
                    (img.width + side) // 2, (img.height + side) // 2))
    names: list[str] = []
    for base, suffix, px in ICONS:
        img.resize((px, px), Image.LANCZOS).save(appdir / f"{base}{suffix}.png", "PNG")
        if base not in names:
            names.append(base)
    return names


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--binary", default=str(ROOT / "work-remix" / "ios-build" / "mcdub3remix"))
    ap.add_argument("--disc", required=True, help="the game-remix folder")
    ap.add_argument("--out", default=str(ROOT / "ios" / "dist"))
    ap.add_argument("--name", default="ReDUB", help="CFBundleDisplayName")
    ap.add_argument("--join", default="", help="write MC3_ROAM_JOIN=<this> into the seeded env")
    ap.add_argument("--player", default="", help="MC3_ROAM_NAME")
    ap.add_argument("--no-env", action="store_true", help="ship no settings file")
    ap.add_argument("--no-disc", action="store_true", help="omit the 4.4 GB disc (thin test package)")
    args = ap.parse_args(argv)
    binary = Path(args.binary)
    disc = Path(args.disc)
    if not binary.is_file():
        print(f"no binary at {binary} (run ios/build_ios.sh)", file=sys.stderr)
        return 2
    if not args.no_disc and not (disc / "default.xbe").is_file():
        print(f"{disc} is not a game folder (no default.xbe)", file=sys.stderr)
        return 2

    version = app_version()
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    app = out / APP_NAME
    if app.exists():
        shutil.rmtree(app)
    app.mkdir(parents=True)

    shutil.copy2(binary, app / EXE_NAME)
    (app / "save").mkdir()

    if not args.no_disc:
        print(f"copying the disc ({disc}) ... this takes a few minutes")
        shutil.copytree(disc, app / "game")

    if not args.no_env:
        env = ENV_HEADER
        if args.join:
            env += f"\nMC3_ROAM_JOIN={args.join}\nMC3_NETPLAY=1\n"
            if args.player:
                env += f"MC3_ROAM_NAME={args.player}\n"
        (app / f"{EXE_NAME}_env.txt").write_text(env, encoding="utf-8", newline="\n")

    icons = icons_png(app)
    plist = {
        "CFBundleDevelopmentRegion": "en",
        "CFBundleDisplayName": args.name,
        "CFBundleExecutable": EXE_NAME,
        "CFBundleIdentifier": BUNDLE_ID,
        "CFBundleInfoDictionaryVersion": "6.0",
        "CFBundleName": "mcdub3remix",
        "CFBundlePackageType": "APPL",
        "CFBundleShortVersionString": version,
        "CFBundleSupportedPlatforms": ["iPhoneOS"],
        "CFBundleVersion": version,
        "DTPlatformName": "iphoneos",
        "DTSDKName": "iphoneos22.0",
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
        # The scene lifecycle. ios_scene.m's ReDUBSceneDelegate is the app's only
        # scene: it shows the placeholder, then hands over to SDL via
        # postFinishLaunch. Without this key UIKit creates no scene, SDL never
        # starts, and the app looks like it does nothing at all.
        "UIApplicationSceneManifest": {
            "UIApplicationSupportsMultipleScenes": False,
            "UISceneConfigurations": {
                "UIWindowSceneSessionRoleApplication": [
                    {
                        "UISceneConfigurationName": "Default Configuration",
                        "UISceneDelegateClassName": "ReDUBSceneDelegate",
                    }
                ]
            },
        },
        # A launch screen. Without one iOS runs the app in a legacy letterboxed
        # mode and warns at launch.
        "UILaunchScreen": {},
        # The app's Documents folder in the Files app, so the settings file can
        # be edited without a Mac or a cable.
        "UIFileSharingEnabled": True,
        "LSSupportsOpeningDocumentsInPlace": True,
    }
    if icons:
        plist["CFBundleIcons"] = {"CFBundlePrimaryIcon": {"CFBundleIconFiles": icons}}
        plist["CFBundleIcons~ipad"] = {"CFBundlePrimaryIcon": {"CFBundleIconFiles": icons}}
    (app / "Info.plist").write_bytes(plistlib.dumps(plist))

    (out / "mcdub3remix.entitlements").write_text(ENTITLEMENTS, encoding="utf-8")

    ipa = out / f"ReDUB-{version}-ios-unsigned.ipa"
    if ipa.exists():
        ipa.unlink()
    total = 0
    with zipfile.ZipFile(ipa, "w", zipfile.ZIP_STORED) as z:
        for p in sorted(app.rglob("*")):
            if p.is_file():
                z.write(p, Path("Payload") / APP_NAME / p.relative_to(app))
                total += p.stat().st_size
    print(f"app: {app}")
    print(f"IPA: {ipa}  ({ipa.stat().st_size / 1e9:.2f} GB, {total / 1e9:.2f} GB of files)")
    print(f"entitlements: {out / 'mcdub3remix.entitlements'}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
