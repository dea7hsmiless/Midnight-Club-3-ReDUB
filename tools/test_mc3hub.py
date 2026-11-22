#!/usr/bin/env python3
"""Self-test for tools/mc3hub.py: build a HELLO the way roam_core does, feed it
to the hub, and check the PEERS reply decodes.

Run:  python tools/test_mc3hub.py
"""
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mc3hub as h

FAIL = 0


def check(name, cond, extra=""):
    global FAIL
    print(("ok   " if cond else "FAIL ") + name + (f"  {extra}" if extra else ""))
    if not cond:
        FAIL += 1


def hello_payload(name: str, port: int, room: int, links: int) -> bytes:
    nb = name.encode("latin-1")
    return (struct.pack("<HBBHHI", 17, 1, 0, port, 1, room)
            + bytes([len(nb)]) + nb
            + bytes([0])            # n_seen
            + bytes([links]))


def main() -> int:
    hub = h.Hub(47210, verbose=False, link_limit=0)

    # --- an empty roster still answers, with n = 0 -------------------------
    d = h.build(h.T_HELLO, 0x11111111, hello_payload("alice", 47210, 0, 3), flags=h.FLAG_HELLO_ALL)
    reply = hub.handle(d, ("127.0.0.1", 47210))
    check("hello from one client is answered", reply is not None)
    got = h.parse(reply)
    check("reply parses", got is not None)
    if got:
        mtype, sender, seq, flags, ver, payload = got
        check("reply is PEERS", mtype == h.T_PEERS, f"type={mtype}")
        check("reply sender is the hub", sender == hub.self_id)
        check("PEERS lists nobody but the asker", payload[0] == 0, f"n={payload[0]}")

    # --- a second client appears in the first's list ------------------------
    d2 = h.build(h.T_HELLO, 0x22222222, hello_payload("bob", 47210, 0, 3), flags=h.FLAG_HELLO_ALL)
    hub.handle(d2, ("127.0.0.2", 47210))
    check("hub tracks both players", len(hub.players) == 2, f"{len(hub.players)}")

    # ask again as alice; the reply must name bob only
    d3 = h.build(h.T_HELLO, 0x11111111, hello_payload("alice", 47210, 0, 3), flags=h.FLAG_HELLO_ALL)
    reply = hub.handle(d3, ("127.0.0.1", 47210))
    mtype, sender, seq, flags, ver, payload = h.parse(reply)
    n = payload[0]
    check("PEERS now has one entry", n == 1, f"n={n}")
    if n == 1:
        ip = ".".join(str(b) for b in payload[1:5])
        port, pid = struct.unpack_from("<HI", payload, 5)
        check("entry is bob", pid == 0x22222222, f"id={pid:08x}")
        check("entry ip is bob's", ip == "127.0.0.2", ip)
        check("entry port", port == 47210, str(port))

    # --- a malformed datagram is dropped, not answered ---------------------
    check("bad magic dropped", hub.handle(b"XXXX" + b"\0" * 20, ("127.0.0.1", 1)) is None)
    good = h.build(h.T_HELLO, 0x33333333, hello_payload("x", 47210, 0, 1))
    bad_crc = good[:-1] + bytes([good[-1] ^ 0xFF])
    check("bad crc dropped", hub.handle(bad_crc, ("127.0.0.1", 1)) is None)
    check("bad player count did not join", 0x33333333 not in hub.players)

    # --- BYE removes ------------------------------------------------------
    bye = h.build(h.T_BYE, 0x22222222, bytes([0]))
    hub.handle(bye, ("127.0.0.2", 47210))
    check("bye removes the player", 0x22222222 not in hub.players, f"{len(hub.players)} left")

    # --- PING is answered with PONG ---------------------------------------
    ping = h.build(h.T_PING, 0x11111111, struct.pack("<I", 1234))
    r = hub.handle(ping, ("127.0.0.1", 47210))
    mtype, *_ = h.parse(r)
    check("ping answered with pong", mtype == h.T_PONG, f"type={mtype}")

    print()
    print("FAIL" if FAIL else "all checks passed")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
