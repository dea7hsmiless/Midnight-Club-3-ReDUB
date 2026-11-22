#!/usr/bin/env python3
"""Self-test for the relay's protections: the room cap and the flood guard.

No room key: like the service this replaces, a room is a number, not a lock.
What the relay does carry is protection for the host it runs on.

Complements tools/test_mc3relay.py (which covers relaying itself). Run:

    python tools/test_relay_guards.py
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mc3hub as h
import mc3relay as r

FAIL = 0


def check(name, ok, extra=""):
    global FAIL
    print(("ok   " if ok else "FAIL ") + name + (f"  {extra}" if extra else ""))
    if not ok:
        FAIL += 1


def hello(sender, name, room, flags=h.FLAG_HELLO_ALL):
    nb = name.encode("latin-1")
    payload = (struct.pack("<HBBHHI", 17, 1, 0, 47210, 1, room)
               + bytes([len(nb)]) + nb + bytes([0]) + bytes([7]))
    return h.build(h.T_HELLO, sender, payload, flags=flags)


def main() -> int:
    # --- a plain join, no key anywhere -----------------------------------
    plain = r.Relay()
    plain.handle(hello(0x1111, "a", 1), ("10.0.0.1", 1))
    check("a client joins an open room", 0x1111 in plain.members)
    plain.handle(hello(0x2222, "b", 1), ("10.0.0.2", 2))
    check("a second joins the same room", 0x2222 in plain.members)
    check("and they are in one room", len(plain.room_of(1, 0x1111)) == 1)

    # --- room cap ---------------------------------------------------------
    cap = r.Relay()
    for i in range(r.MAX_MEMBERS_PER_ROOM):
        cap.handle(hello(0x9000 + i, f"p{i}", 1), (f"10.1.0.{i}", i))
    check(f"room holds {r.MAX_MEMBERS_PER_ROOM}", len(cap.members) == r.MAX_MEMBERS_PER_ROOM,
          str(len(cap.members)))
    cap.handle(hello(0x9999, "extra", 1), ("10.1.0.99", 99))
    check("one more is refused", 0x9999 not in cap.members)
    check("full room counted", cap.refused_full == 1, str(cap.refused_full))
    # a different room still has space
    cap.handle(hello(0x9998, "other", 2), ("10.1.0.98", 98))
    check("another room is unaffected", 0x9998 in cap.members)

    # --- flood guard ------------------------------------------------------
    flood = r.Relay()
    addr = ("10.9.9.9", 9)
    refused = 0
    for _ in range(r.MAX_DGRAMS_PER_S + 50):
        if flood.flooding(addr):
            refused += 1
    check("flood guard trips past the budget", refused > 0, f"{refused} refused")
    # The first MAX_DGRAMS_PER_S in a second are allowed, the rest are not.
    check("allows the budget, refuses the rest", refused == 50, f"{refused} refused, want 50")

    # a different source is unaffected
    check("another source is not throttled", not flood.flooding(("10.9.9.10", 10)))

    print()
    print("FAIL" if FAIL else "all checks passed")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
