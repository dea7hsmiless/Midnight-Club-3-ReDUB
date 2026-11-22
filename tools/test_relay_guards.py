#!/usr/bin/env python3
"""Self-test for the relay's protections: room key, room cap, flood guard.

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


def hello(sender, name, room, key=0, flags=h.FLAG_HELLO_ALL):
    nb = name.encode("latin-1")
    payload = (struct.pack("<HBBHHI", 17, 1, 0, 47210, 1, room)
               + bytes([len(nb)]) + nb + bytes([0]) + bytes([7]))
    if key:
        payload += struct.pack("<I", key)
    return h.build(h.T_HELLO, sender, payload, flags=flags)


def main() -> int:
    K = r.key_hash("letmein")

    # --- no key configured: open relay, as before -------------------------
    open_relay = r.Relay()
    out = open_relay.handle(hello(0x1111, "a", 1), ("10.0.0.1", 1))
    check("open relay accepts a client with no key", 0x1111 in open_relay.members)
    out = open_relay.handle(hello(0x2222, "b", 1, key=K), ("10.0.0.2", 2))
    check("open relay accepts a keyed client too", 0x2222 in open_relay.members)

    # --- key configured ---------------------------------------------------
    keyed = r.Relay(key="letmein")
    keyed.handle(hello(0x3333, "good", 1, key=K), ("10.0.0.3", 3))
    check("right key joins", 0x3333 in keyed.members)

    keyed.handle(hello(0x4444, "wrong", 1, key=r.key_hash("guess")), ("10.0.0.4", 4))
    check("wrong key refused", 0x4444 not in keyed.members)
    check("wrong key counted", keyed.refused_key == 1, str(keyed.refused_key))

    keyed.handle(hello(0x5555, "nokey", 1), ("10.0.0.5", 5))
    check("client with no key refused when a key is set", 0x5555 not in keyed.members)
    check("no-key refusal counted", keyed.refused_key == 2, str(keyed.refused_key))

    # a known member presenting a bad key loses its place
    keyed.handle(hello(0x3333, "good", 1, key=r.key_hash("guess")), ("10.0.0.3", 3))
    check("a member with a wrong key is dropped", 0x3333 not in keyed.members)

    # --- room cap ---------------------------------------------------------
    cap = r.Relay(key="letmein")
    for i in range(r.MAX_MEMBERS_PER_ROOM):
        cap.handle(hello(0x9000 + i, f"p{i}", 1, key=K), (f"10.1.0.{i}", i))
    check(f"room holds {r.MAX_MEMBERS_PER_ROOM}", len(cap.members) == r.MAX_MEMBERS_PER_ROOM,
          str(len(cap.members)))
    cap.handle(hello(0x9999, "extra", 1, key=K), ("10.1.0.99", 99))
    check("one more is refused", 0x9999 not in cap.members)
    check("full room counted", cap.refused_full == 1, str(cap.refused_full))
    # a different room still has space
    cap.handle(hello(0x9998, "other", 2, key=K), ("10.1.0.98", 98))
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
