#!/usr/bin/env python3
"""Check the C and Python room-key hashes agree.

The client (rc_key_hash in roam_core.h) and the relay (key_hash in mc3relay.py)
must produce the same 32-bit value or nobody can join a keyed room. Rather than
trust two implementations of FNV-1a to match, this compiles the C one and
compares, for a few keys including the awkward cases.

Run:  python tools/test_room_key.py
"""
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mc3relay as relay

CPP = r"""
#include <stdint.h>
#include <stdio.h>
#include "net/roam/roam_core.h"
int main(void){
    const char *keys[] = { "", "redub", "a-longer-room-key-2026", "password",
                           "with spaces", "unicode-\xc3\xa9", "0123456789abcdef0123456789abcdef" };
    for (unsigned i = 0; i < sizeof keys / sizeof keys[0]; i++)
        printf("%u\n", rc_key_hash(keys[i]));
    return 0;
}
"""

KEYS = ["", "redub", "a-longer-room-key-2026", "password",
        "with spaces", "unicode-é", "0123456789abcdef0123456789abcdef"]

FAIL = 0


def check(name, ok, extra=""):
    global FAIL
    print(("ok   " if ok else "FAIL ") + name + (f"  {extra}" if extra else ""))
    if not ok:
        FAIL += 1


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    inc = root / "xboxrecomp" / "src"
    # The build's own compiler, not whatever "cc" happens to mean here: MSYS's
    # clang64 ships clang as cc, and PATH is not guaranteed.
    cc = os.environ.get("CC") or shutil.which("clang") or shutil.which("cc")
    if not cc:
        print("no C compiler on PATH (set CC, or run in an MSYS2 CLANG64 shell)")
        return 2
    with tempfile.TemporaryDirectory() as td:
        src = Path(td) / "k.c"
        exe = Path(td) / "k.exe"
        src.write_text(CPP, encoding="utf-8")
        done = subprocess.run([cc, str(src), "-I", str(inc), "-o", str(exe)],
                              capture_output=True, text=True)
        if done.returncode != 0:
            print("could not compile the C side:\n" + done.stderr[-1500:])
            return 2
        out = subprocess.run([str(exe)], capture_output=True, text=True)
        if out.returncode != 0:
            print("the C side did not run:\n" + out.stderr[-1500:])
            return 2
        c_vals = [int(x) for x in out.stdout.split()]

    check("C produced a value per key", len(c_vals) == len(KEYS), f"{len(c_vals)} of {len(KEYS)}")
    for key, c in zip(KEYS, c_vals):
        py = relay.key_hash(key) if key else 0
        check(f"{key!r} matches", c == py, f"C={c:#010x} py={py:#010x}")

    # An empty key must hash to 0: 0 is what "no key" means on the wire.
    check("empty key reads as 'no key'", relay.key_hash("") == 0)
    print()
    print("FAIL" if FAIL else "all checks passed")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
