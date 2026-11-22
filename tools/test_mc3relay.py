#!/usr/bin/env python3
"""Self-test for tools/mc3relay.py.

Builds HELLOs the way roam_core.c does and checks the relay's decisions:
a player is learned from HELLO, datagrams go to the rest of the room only,
rooms are separate, strangers are never relayed, and BYE leaves.

Run:  python tools/test_mc3relay.py
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mc3hub as h
import mc3relay as r

FAIL = 0


def check(name, cond, extra=""):
    global FAIL
    print(("ok   " if cond else "FAIL ") + name + (f"  {extra}" if extra else ""))
    if not cond:
        FAIL += 1


def hello(sender, name, room, port=47210, flags=h.FLAG_HELLO_ALL):
    nb = name.encode("latin-1")
    payload = (struct.pack("<HBBHHI", 17, 1, 0, port, 1, room)
               + bytes([len(nb)]) + nb + bytes([0]) + bytes([7]))
    return h.build(h.T_HELLO, sender, payload, flags=flags)


def snap(sender, payload=b"\x01\x02\x03\x04"):
    return h.build(3, sender, payload)          # RC_MSG_SNAP = 3


def main() -> int:
    relay = r.Relay(verbose=False)
    A = ("10.0.0.1", 40001)
    B = ("10.0.0.2", 40002)
    C = ("10.0.0.3", 40003)
    D = ("10.0.0.4", 40004)

    # --- a stranger's SNAP is never relayed -------------------------------
    out = relay.handle(snap(0xAAAA0001), A)
    check("unknown sender is not relayed", out == [], f"{len(out)} out")

    # --- HELLO arrives, nobody to send to yet -----------------------------
    out = relay.handle(hello(0x11111111, "alice", room=1), A)
    check("first HELLO relays to nobody", out == [], f"{len(out)} out")
    check("alice is remembered", 0x11111111 in relay.members)
    check("alice's room is 1", relay.members[0x11111111].room == 1)

    # --- a second player in the same room --------------------------------
    out = relay.handle(hello(0x22222222, "bob", room=1), B)
    dests = [d for _p, d in out]
    check("bob's HELLO goes to alice", dests == [A], str(dests))

    # --- SNAP relays to the other member, not back to the sender ---------
    out = relay.handle(snap(0x11111111), A)
    dests = [d for _p, d in out]
    check("alice's SNAP goes to bob only", dests == [B], str(dests))
    check("the payload is forwarded verbatim", out and out[0][0] == snap(0x11111111))

    # --- rooms are separate ----------------------------------------------
    relay.handle(hello(0x33333333, "carol", room=2), C)
    out = relay.handle(snap(0x33333333), C)
    check("room 2 does not reach room 1", out == [], f"{len(out)} out")

    # --- a third player in room 1 ----------------------------------------
    relay.handle(hello(0x44444444, "dave", room=1), D)
    out = relay.handle(snap(0x11111111), A)
    dests = sorted(d for _p, d in out)
    check("alice's SNAP now reaches bob and dave", dests == sorted([B, D]), str(dests))

    # --- PING is answered, never relayed ---------------------------------
    ping = h.build(h.T_PING, 0x11111111, struct.pack("<I", 1234))
    out = relay.handle(ping, A)
    check("ping is answered", len(out) == 1)
    if out:
        got = h.parse(out[0][0])
        check("answer is PONG to the pinger", got is not None and got[0] == h.T_PONG
              and out[0][1] == A)

    # --- BYE leaves -------------------------------------------------------
    relay.handle(h.build(h.T_BYE, 0x22222222, bytes([0])), B)
    check("bye removes bob", 0x22222222 not in relay.members)
    out = relay.handle(snap(0x11111111), A)
    dests = [d for _p, d in out]
    check("alice's SNAP no longer goes to bob", dests == [D], str(dests))

    # --- a bad CRC is dropped --------------------------------------------
    good = snap(0x11111111)
    bad = good[:-1] + bytes([good[-1] ^ 0xFF])
    before = relay.dropped
    out = relay.handle(bad, A)
    check("bad crc is dropped", out == [] and relay.dropped > before)

    print()
    print("FAIL" if FAIL else "all checks passed")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
