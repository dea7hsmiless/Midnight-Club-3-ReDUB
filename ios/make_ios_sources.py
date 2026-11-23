#!/usr/bin/env python3
"""Write the iOS source list from the Linux build's, with the platform swaps.

The Linux and iPhone builds share everything above the host layer: the same
lifted code, the same kernel HLE, the same OpenGL/GLES renderer. What differs is
the host files, and this script makes exactly those substitutions:

  linux_settings.c, linux_media.c  ->  ios_host.c        (settings, log, music)
  (none)                           ->  ios_launcher.m    (the native launcher)
  (none)                           ->  ios_scene.m       (UIScene plumbing)
  (none)                           ->  ios_xdvdfs.c      (reading the disc)

recomp_manual.c stays: the iOS-only blocks inside it (the Port Options pages,
the launcher hooks) are behind TARGET_OS_IPHONE.

Usage: python ios/make_ios_sources.py <linux-sources.txt> <ios-sources.txt>
"""
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent

DROP = {"linux_settings.c", "linux_media.c"}
ADD = ["src/ios_host.c", "src/ios_launcher.m", "src/ios_scene.m", "src/ios_xdvdfs.c"]


def main(argv):
    if len(argv) != 2:
        print(__doc__.strip())
        return 2
    src = Path(argv[0]).read_text(encoding="utf-8").split()
    out = []
    seen = set()
    for p in src:
        name = Path(p).name
        if name in DROP:
            continue
        # the Linux paths are /work/release-source/... or relative to the repo
        q = p.replace("\\", "/")
        if "/release-source/" in q:
            q = q.split("/release-source/", 1)[1]
        elif q.startswith("/work/release-gen/"):
            q = "work-remix/gen/" + Path(q).name
        elif q.startswith("/"):
            q = q.rsplit("/", 1)[-1]
        if q in seen:
            continue
        seen.add(q)
        out.append(q)
    for a in ADD:
        if a not in seen:
            out.append(a)
            seen.add(a)

    Path(argv[1]).write_text("\n".join(out) + "\n", encoding="utf-8")
    gen = sum(1 for p in out if "/gen/" in p or p.startswith("work-remix/gen/"))
    print(f"{len(out)} sources ({gen} generated) -> {argv[1]}")
    missing = [p for p in out if not (REPO / p).exists()]
    if missing:
        print("missing (the build will fail on these):")
        for m in missing:
            print("  ", m)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
