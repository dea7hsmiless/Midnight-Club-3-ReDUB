#!/usr/bin/env python3
"""Reject stale platform artifacts missing the shared Free Roam setup."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import zipfile

ROOT = Path(__file__).resolve().parents[1]
MARKERS = (
    "ENTER NICKNAME", "SAVE NICKNAME", "nickname saved and applied",
    "COULD NOT SAVE - TRY AGAIN", "START FREE ROAM", "BACK TO ONLINE",
    "PLAYERS: %d / 8", "ROOM: %d (%d/8)", "FIND A ROOM",
)


def inspect(label, data):
    missing = [text for text in MARKERS if text.encode("ascii") not in data]
    if missing:
        raise SystemExit(f"FAIL {label}: missing {', '.join(missing)}")
    result = {"artifact": label, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}
    print("PASS " + json.dumps(result, ensure_ascii=True))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--windows", type=Path, default=ROOT / "work-remix/build-windows-x86_64/mcdub3remix.exe")
    parser.add_argument("--linux", type=Path, default=ROOT / "work-remix/build-linux/mcdub3remix")
    parser.add_argument("--ios", type=Path, default=ROOT / "work-remix/ios-build/mcdub3remix")
    parser.add_argument("--android", type=Path, default=ROOT / "work-remix/android/mcdub3remix-0.1.2.apk")
    args = parser.parse_args()
    versions = [re.search(r'^APP_VERSION="?([^"\r\n]+)', (ROOT / "config" / edition / "app.env").read_text(encoding="utf-8-sig"), re.M).group(1)
                for edition in ("dub", "remix")]
    assert versions[0] == versions[1], versions
    for platform in ("windows", "linux", "ios"):
        inspect(platform, getattr(args, platform).read_bytes())
    with zipfile.ZipFile(args.android) as apk:
        native = [name for name in apk.namelist() if name.startswith("lib/") and name.endswith("/libmain.so")]
        assert native, "Android APK has no libmain.so"
        for name in native:
            inspect("android:" + name, apk.read(name))
    print(f"PASS online feature markers across all four platforms; app version {versions[0]}")
    print("Runtime/device behavior still requires the multiplayer device pass.")


if __name__ == "__main__":
    main()
