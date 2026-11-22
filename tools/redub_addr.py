#!/usr/bin/env python3
"""Encode a server address (or host name) so it is not in plain sight in a file.

    python tools/redub_addr.py encode 162.35.56.66
    python tools/redub_addr.py decode rd1:....

The client accepts either form in MC3_ROAM_HUB_IP / MC3_ROAM_RELAY_IP, so an
encoded line and a plain one are interchangeable at run time.

Be clear about what this buys: the app has to decode the address with no secret
from the player, so the encoding is reversible by anyone who has the file and a
few lines of code. It keeps the address out of a screenshot, a stream, a shared
install folder, or a config pasted into a bug report. It does not hide anything
from the network -- traffic is plain UDP either way. See roam_addr.h.

The keystream and checksum here must match roam_addr.h exactly; two independent
implementations of the same transform do not stay in agreement by luck, so
tools/test_roam_addr.py compiles the C and compares it with this file.
"""
from __future__ import annotations

import sys
from pathlib import Path

PREFIX = "rd1:"
MAX_LEN = 96


def _keystream(n: int) -> bytes:
    x = 0x9E3779B9
    out = bytearray()
    for _ in range(n):
        x ^= (x << 13) & 0xFFFFFFFF
        x &= 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        x &= 0xFFFFFFFF
        out.append((x >> 11) & 0xFF)
    return bytes(out)


def _check(text: bytes) -> int:
    h = 0x5A
    for b in text:
        h = (h * 31 + b) & 0xFF
    return h


def encode(text: str) -> str:
    raw = text.encode("utf-8")
    if not raw:
        raise ValueError("nothing to encode")
    if len(raw) > MAX_LEN:
        raise ValueError(f"too long ({len(raw)} bytes, limit {MAX_LEN})")
    ks = _keystream(len(raw))
    obf = bytes(a ^ b for a, b in zip(raw, ks))
    blob = obf + bytes([_check(raw)])
    return PREFIX + blob.hex()


def decode(text: str) -> str:
    if not text.startswith(PREFIX):
        raise ValueError("not an rd1: value")
    body = text[len(PREFIX):]
    if len(body) < 4 or len(body) % 2:
        raise ValueError("wrong length")
    try:
        blob = bytes.fromhex(body)
    except ValueError as exc:
        raise ValueError("not hex") from exc
    obf, want = blob[:-1], blob[-1]
    raw = bytes(a ^ b for a, b in zip(obf, _keystream(len(obf))))
    if _check(raw) != want:
        raise ValueError("checksum does not match: a character is wrong")
    return raw.decode("utf-8", "replace")


def main(argv: list[str]) -> int:
    if len(argv) < 2 or argv[0] not in ("encode", "decode"):
        print(__doc__.strip())
        return 2
    cmd, text = argv[0], argv[1]
    try:
        if cmd == "encode":
            out = encode(text.strip())
            print(out)
            print()
            print("paste this line into the player's env file:")
            print(f"MC3_ROAM_HUB_IP={out}")
        else:
            out = decode(text.strip())
            print(out)
            print()
            print("paste this line into the player's env file:")
            print(f"MC3_ROAM_HUB_IP={out}")
    except ValueError as exc:
        print(f"{cmd}: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
