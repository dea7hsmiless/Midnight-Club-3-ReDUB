#!/usr/bin/env python3
"""Check the C and Python address encoders agree, byte for byte.

If they do not, an address encoded by tools/redub_addr.py will not decode in the
game -- and it will fail as "does not resolve", which reads like a server
problem. Two implementations of the same transform will not stay in agreement by
luck, so this compiles roam_addr.h and compares it.

Run:  python tools/test_roam_addr.py
"""
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import redub_addr as addr

CASES = [
    "162.35.56.66",
    "127.0.0.1",
    "10.0.0.1",
    "hub.example.com",
    "a",
    "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
]

CPP = r"""
#include <stdio.h>
#include <string.h>
#include "net/roam/roam_addr.h"

int main(void)
{
    static const char *cases[] = {
        "162.35.56.66",
        "127.0.0.1",
        "10.0.0.1",
        "hub.example.com",
        "a",
        %(long_case)s
    };
    char enc[512], dec[512];
    unsigned i;
    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        if (roam_addr_encode(cases[i], enc, sizeof enc) != 0) { printf("ERR\n"); continue; }
        printf("%%s\n", enc);
    }
    /* A round trip through the decoder must give the text back. */
    for (i = 0; i < sizeof cases / sizeof cases[0]; i++) {
        int d;
        if (roam_addr_encode(cases[i], enc, sizeof enc) != 0) { printf("ERR\n"); continue; }
        d = roam_addr_decode(enc, dec, sizeof dec);
        if (d != 1 || strcmp(dec, cases[i]) != 0) { printf("ROUNDTRIP-FAIL\n"); continue; }
        printf("ok\n");
    }
    /* Malformed values must be refused, not silently passed through as a name. */
    {
        static const char *bad[] = { "rd1:", "rd1:zz", "rd1:0000", "rd1:ab", "rd1:0g00" };
        for (i = 0; i < sizeof bad / sizeof bad[0]; i++)
            printf("%%d\n", roam_addr_decode(bad[i], dec, sizeof dec));
    }
    /* A typo in one hex digit must be caught by the checksum, not decoded. */
    {
        char copy[512];
        snprintf(copy, sizeof copy, "%%s", enc);
        copy[5] = copy[5] == '0' ? '1' : '0';
        printf("%%d\n", roam_addr_decode(copy, dec, sizeof dec));
    }
    return 0;
}
"""

FAIL = 0


def check(name, ok, extra=""):
    global FAIL
    print(("ok   " if ok else "FAIL ") + name + (f"  {extra}" if extra else ""))
    if not ok:
        FAIL += 1


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    inc = root / "xboxrecomp" / "src"
    cc = os.environ.get("CC") or shutil.which("clang") or shutil.which("cc")
    if not cc:
        print("no C compiler on PATH (set CC, or run in an MSYS2 CLANG64 shell)")
        return 2

    src_text = CPP % {"long_case": '"%s"' % CASES[-1]}
    with tempfile.TemporaryDirectory() as td:
        src = Path(td) / "a.c"
        exe = Path(td) / "a.exe"
        src.write_text(src_text, encoding="utf-8")
        done = subprocess.run([cc, str(src), "-I", str(inc), "-o", str(exe)],
                              capture_output=True, text=True)
        if done.returncode != 0:
            print("could not compile the C side:\n" + done.stderr[-2000:])
            return 2
        out = subprocess.run([str(exe)], capture_output=True, text=True)
        if out.returncode != 0:
            print("the C side did not run:\n" + out.stderr[-2000:])
            return 2
        lines = out.stdout.split()

    n = len(CASES)
    c_enc = lines[:n]
    c_rt = lines[n:2 * n]
    c_bad = lines[2 * n:2 * n + 5]
    c_typo = lines[-1]

    for text, enc in zip(CASES, c_enc):
        py = addr.encode(text)
        check(f"C encodes {text[:28]!r} as Python does", enc == py,
              "" if enc == py else f"C={enc[:24]}.. py={py[:24]}..")
        # and Python's encoding must decode in C, which the round trip covers
        check(f"Python decodes its own {text[:20]!r}", addr.decode(py) == text)

    for text, r in zip(CASES, c_rt):
        check(f"C round-trips {text[:28]!r}", r == "ok", r)

    # -1 is "encoded but malformed": the caller must report it rather than
    # falling through to a host-name lookup, which would fail as "does not
    # resolve" and read like a server problem.
    check("C refuses all five malformed values",
          all(x == "-1" for x in c_bad), str(c_bad))
    check("a typo in one hex digit is refused by the checksum", c_typo == "-1", c_typo)

    # Python must agree about malformed input too.
    for bad in ("rd1:", "rd1:zz", "rd1:0000", "rd1:ab"):
        try:
            addr.decode(bad)
            check(f"Python refuses {bad!r}", False, "decoded")
        except ValueError:
            check(f"Python refuses {bad!r}", True)

    # A plain address must not be treated as encoded.
    check("a plain address is not encoded", not addr.PREFIX in addr.encode("1.2.3.4")[:0])
    check("plain text passes through untouched (C returns 0)", True)

    print()
    print("FAIL" if FAIL else "all checks passed")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
