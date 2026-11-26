#!/usr/bin/env python3
"""Keep the two Android manifests in step.

The release ships two APKs from one source tree:

  src/main/AndroidManifest.xml     the normal build (music, all-files access)
  src/nomusic/AndroidManifest.xml  -PMC3_MUSIC=0, the one Play Protect installs
                                   from a browser where enhanced fraud
                                   protection runs

They are hand-written files rather than a merged overlay, because the merger
cannot remove a service's `android:permission` attribute in a way that leaves
the notification-listener permission out of the packaged manifest. Two copies
of one document drift; this refuses the drift instead of trusting the comment.

Exactly two differences are allowed, and nothing else:

  1. `android.permission.MANAGE_EXTERNAL_STORAGE` is absent from nomusic.
  2. the `.MediaSessionListener` service is absent from nomusic
     (that declaration is what becomes BIND_NOTIFICATION_LISTENER_SERVICE).

    python tools/check_android_manifests.py [--repo DIR]

Exit 0 = the only differences are those two.
"""
from __future__ import annotations

import argparse
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ANDROID = "{http://schemas.android.com/apk/res/android}"

DROP_PERMISSION = "android.permission.MANAGE_EXTERNAL_STORAGE"
DROP_SERVICE = ".MediaSessionListener"

# The declarations Play Protect's enhanced fraud protection looks for. The
# no-music build must contain none of them, however it ended up that way.
BANNED_PERMISSIONS = {
    "android.permission.RECEIVE_SMS",
    "android.permission.READ_SMS",
    "android.permission.MANAGE_EXTERNAL_STORAGE",
}
BANNED_SERVICE_PERMISSIONS = {
    "android.permission.BIND_NOTIFICATION_LISTENER_SERVICE",
    "android.permission.BIND_ACCESSIBILITY_SERVICE",
}


def attrs(el):
    """Attributes as sorted (local name, value), namespace-independent."""
    out = []
    for k, v in el.attrib.items():
        local = k.split("}", 1)[-1] if "}" in k else k
        out.append((local, v))
    return tuple(sorted(out))


def canonical(root):
    """A comparable, order-independent picture of the manifest's meaning."""
    perms, feats, activities, app_attrs = set(), set(), set(), None
    services = {}          # name -> (attrs, permission), so a same-named service still compares

    for el in root:
        tag = el.tag.split("}")[-1]
        if tag == "uses-permission":
            perms.add(el.get(ANDROID + "name"))
        elif tag == "uses-feature":
            feats.add((el.get(ANDROID + "name"), el.get(ANDROID + "glEsVersion"),
                       el.get(ANDROID + "required")))
        elif tag == "application":
            app_attrs = attrs(el)
            for child in el:
                ctag = child.tag.split("}")[-1]
                if ctag == "service":
                    services[child.get(ANDROID + "name")] = (
                        attrs(child), child.get(ANDROID + "permission"))
                elif ctag == "activity":
                    filters = set()
                    for f in child:
                        if f.tag.split("}")[-1] == "intent-filter":
                            for a in f:
                                filters.add((a.tag.split("}")[-1],
                                             tuple(sorted(a.attrib.values()))))
                    activities.add((attrs(child), tuple(sorted(filters))))

    return {
        "permissions": perms,
        "features": feats,
        "application": app_attrs,
        "activities": activities,
        "services": services,
        "manifest_attrs": attrs(root),
    }


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default=str(Path(__file__).resolve().parent.parent))
    args = ap.parse_args(argv)

    root = Path(args.repo)
    full_path = root / "android/app/src/main/AndroidManifest.xml"
    no_path = root / "android/app/src/nomusic/AndroidManifest.xml"
    for p in (full_path, no_path):
        if not p.exists():
            raise SystemExit(f"missing manifest: {p}")

    full = canonical(ET.parse(full_path).getroot())
    nomu = canonical(ET.parse(no_path).getroot())

    problems = []

    # manifest / application / activities: identical
    for key in ("manifest_attrs", "application", "activities", "features"):
        if full[key] != nomu[key]:
            only_full = full[key] - nomu[key] if isinstance(full[key], set) else None
            only_nomu = nomu[key] - full[key] if isinstance(nomu[key], set) else None
            problems.append(f"{key} differs (only in main: {only_full}; only in nomusic: {only_nomu})")

    # permissions: exactly one removed
    dropped = full["permissions"] - nomu["permissions"]
    added = nomu["permissions"] - full["permissions"]
    if dropped != {DROP_PERMISSION}:
        problems.append(f"nomusic must drop exactly {DROP_PERMISSION}; it drops {sorted(dropped)}")
    if added:
        problems.append(f"nomusic declares permissions main does not: {sorted(added)}")

    # services: exactly one removed, by name
    s_dropped = set(full["services"]) - set(nomu["services"])
    s_added = set(nomu["services"]) - set(full["services"])
    if s_dropped != {DROP_SERVICE}:
        problems.append(f"nomusic must drop exactly {DROP_SERVICE}; it drops {sorted(s_dropped)}")
    if s_added:
        problems.append(f"nomusic declares services main does not: {sorted(s_added)}")
    # A service kept in main under the same name must be identical there.
    for name in set(full["services"]) & set(nomu["services"]):
        if full["services"][name] != nomu["services"][name]:
            problems.append(f"service {name} differs between the two manifests")

    # The property that actually matters, checked on the packaged declarations
    # rather than inferred from the diff: nothing Play Protect blocks may
    # remain, however it got there.
    banned = nomu["permissions"] & BANNED_PERMISSIONS
    if banned:
        problems.append(f"nomusic still declares sensitive permission(s): {sorted(banned)}")
    for name, (_attrs, perm) in nomu["services"].items():
        if perm in BANNED_SERVICE_PERMISSIONS:
            problems.append(f"nomusic still declares service {name} with {perm}")
    # If main stops carrying the permission this build exists to remove, the
    # two manifests no longer need to differ: say so instead of passing quietly.
    if DROP_PERMISSION not in full["permissions"]:
        problems.append(f"main no longer declares {DROP_PERMISSION}: the nomusic build is redundant, update this checker")

    if problems:
        print("check_android_manifests: FAIL")
        for p in problems:
            print("  " + p)
        return 1

    print(f"check_android_manifests: PASS ({len(full['permissions'])} permissions, "
          f"{len(full['activities'])} activities, {len(full['services'])} service(s); "
          f"nomusic drops {DROP_PERMISSION} and {DROP_SERVICE})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
