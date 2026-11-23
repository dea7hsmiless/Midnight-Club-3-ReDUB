#!/usr/bin/env python3
"""Check that MC3_ROAM_JOIN is really one line.

The promise is that

    MC3_ROAM_JOIN=<server>

behaves as MC3_ROAM=1 plus a hub plus the relay plus that address, so nobody has
to set three keys that each fail the same invisible way when left out. That is
a promise about code, so it gets a test rather than a paragraph.

roam_net.c is compiled in here (it is self-contained -- roam_api.h and stdio only)
and roam_net_boot() is called exactly as the game calls it, in a fresh process
per scenario, and the log lines are read back. No disc, no window, no 50 s boot.

Run:  python tools/test_roam_join.py
"""
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import redub_addr as addr

FAIL = 0


def check(name, ok, extra=""):
    global FAIL
    print(("ok   " if ok else "FAIL ") + name + (f"  {extra}" if extra else ""))
    if not ok:
        FAIL += 1


HARNESS = r"""
#include <stdio.h>
#include <stdlib.h>

/* Called by the game on the first frame; compiling the file in makes its
 * static function reachable, which is the whole point of doing it this way. */
static void roam_net_boot(void);
#include "%(roam_net)s"

int main(void)
{
    roam_net_boot();
    return 0;
}
"""


def build(cc, root, work: Path) -> Path:
    src = work / "j.c"
    exe = work / "j.exe"
    src.write_text(HARNESS % {"roam_net": str(root / "src" / "games" / "remix" / "roam_net.c").replace("\\", "/")},
                   encoding="utf-8")
    cmd = [cc, str(src),
           str(root / "xboxrecomp" / "src" / "net" / "roam" / "roam_core.c"),
           str(root / "xboxrecomp" / "src" / "net" / "roam" / "roam_plat.c"),
           str(root / "xboxrecomp" / "src" / "net" / "roam" / "roam_api.c"),
           # roam_net_start_now() now selects the relayed System Link transport
           str(root / "xboxrecomp" / "src" / "net" / "xnet_hle.c"),
           str(root / "xboxrecomp" / "src" / "net" / "xnet_relay.c"),
           "-I", str(root / "xboxrecomp" / "src"),
           "-I", str(root / "xboxrecomp" / "src" / "net" / "roam"),
           "-o", str(exe), "-lws2_32", "-lpthread"]
    done = subprocess.run(cmd, capture_output=True, text=True)
    if done.returncode != 0:
        print("could not build the harness:\n" + (done.stderr or done.stdout)[-2500:])
        return None
    return exe


def run(exe: Path, env_extra: dict) -> str:
    env = {k: v for k, v in os.environ.items()
           if k.upper() in ("SYSTEMROOT", "PATH", "TEMP", "TMP")}
    for k in ("MC3_ROAM", "MC3_ROAM_JOIN", "MC3_ROAM_HUB", "MC3_ROAM_HUB_IP",
              "MC3_ROAM_HUB_PORT", "MC3_ROAM_RELAY", "MC3_ROAM_ROOM",
              "MC3_ROAM_NAME", "MC3_ROAM_SHOW_ADDR", "MC3_ROAM_EARLY"):
        env.pop(k, None)
    env.update(env_extra)
    done = subprocess.run([str(exe)], capture_output=True, text=True, env=env, timeout=30)
    return (done.stdout or "") + (done.stderr or "")


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    cc = os.environ.get("CC") or shutil.which("clang") or shutil.which("cc")
    if not cc:
        print("no C compiler on PATH (set CC, or run in an MSYS2 CLANG64 shell)")
        return 2

    with tempfile.TemporaryDirectory() as td:
        exe = build(cc, root, Path(td))
        if exe is None:
            return 2

        # --- the one line -------------------------------------------------
        out = run(exe, {"MC3_ROAM_JOIN": "1.2.3.4", "MC3_ROAM_NAME": "Donte"})
        check("JOIN starts the transport with no MC3_ROAM set",
              "transport up as \"Donte\"" in out, "" if "transport up" in out else out[-300:])
        check("JOIN sets the hub address", "[roam] hub 1.2.3.4:47210" in out, _line(out, "hub"))
        check("JOIN turns the relay on", "[roam] relay 1.2.3.4:47210" in out, _line(out, "relay"))
        check("JOIN puts us in a hub, not a LAN mesh",
              "MC3_ROAM_HUB=1" in out or "hub 1.2.3.4" in out)

        # --- the same, written encoded ------------------------------------
        enc = addr.encode("9.9.9.9")
        out = run(exe, {"MC3_ROAM_JOIN": enc})
        check("an encoded JOIN starts and masks the address",
              "[roam] relay **.**.**.**:47210" in out, _line(out, "relay"))
        check("and the log does not print the address", "9.9.9.9" not in out, _line(out, "relay"))
        show = run(exe, {"MC3_ROAM_JOIN": enc, "MC3_ROAM_SHOW_ADDR": "1"})
        check("with MC3_ROAM_SHOW_ADDR=1 it decodes to the address",
              "relay 9.9.9.9:47210" in show, _line(show, "relay"))

        # --- the room still works -----------------------------------------
        out = run(exe, {"MC3_ROAM_JOIN": "1.2.3.4", "MC3_ROAM_ROOM": "7"})
        check("JOIN takes the room from MC3_ROAM_ROOM", "(group 7)" in out, _line(out, "relay"))

        # --- explicit settings still win ----------------------------------
        out = run(exe, {"MC3_ROAM_JOIN": "1.2.3.4", "MC3_ROAM_RELAY": "0"})
        check("MC3_ROAM_RELAY=0 keeps the relay off", "[roam] relay" not in out, _line(out, "relay"))
        check("but the hub is still there", "[roam] hub 1.2.3.4:47210" in out, _line(out, "hub"))

        # --- the other shapes still behave --------------------------------
        out = run(exe, {"MC3_ROAM": "1"})
        check("MC3_ROAM=1 alone starts it", "transport up" in out)
        check("and asks for no hub", "[roam] hub" not in out, _line(out, "hub"))

        out = run(exe, {})
        check("nothing set starts nothing", "transport up" not in out, out[-200:])

        # --- a typo is reported, not silent -------------------------------
        out = run(exe, {"MC3_ROAM_JOIN": "rd1:0000"})
        check("a malformed rd1: says so", "not a valid encoded address" in out, out[-300:])

    print()
    print("FAIL" if FAIL else "all checks passed")
    return 1 if FAIL else 0


def _line(out: str, word: str) -> str:
    for line in out.splitlines():
        if word in line:
            return line.strip()
    return "(no line)"


if __name__ == "__main__":
    sys.exit(main())
