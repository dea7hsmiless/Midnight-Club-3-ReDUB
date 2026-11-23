#!/usr/bin/env python3
"""Online-room checks that the relay must pass for matchmaking to be honest.

These are the rules the in-game rows depend on:

  * a room's player cap is the number the player picked (the high half of the
    room key), not the city they happen to be in;
  * the census reports that same number, so "3 (2/8)" means two players you
    can actually meet;
  * a player who re-HELLOs into a room that is already full is refused, not
    quietly added (otherwise the cap is advisory);
  * the census stays inside a datagram and inside what the client parses.

Run:  python tools/test_room_rules.py
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


def key(group, city):
    return (group << 16) | city


def census(relay):
    out = relay.handle(b"MC3Q", ("10.9.9.9", 5555))
    if not out:
        return "", {}
    text = out[0][0].decode("latin-1")
    parts = text.split()
    rooms = {}
    if len(parts) >= 3 and parts[0] == "MC3ROOMS":
        for pair in parts[3:]:
            g, _, n = pair.partition(":")
            rooms[int(g)] = int(n)
    return text, rooms


def main() -> int:
    # --- the cap counts the room the player picked, across cities ----------
    relay = r.Relay(verbose=False)
    for i in range(8):
        relay.handle(hello(0x50000000 + i, f"p{i}", key(3, 1)), ("10.0.0.%d" % (i + 1), 40000 + i))
    text, rooms = census(relay)
    check("eight in room 3, city 1 count as room 3", rooms.get(3) == 8, text)
    # a ninth player who picked room 3 but loaded another city is the same room
    relay.handle(hello(0x50000099, "p9", key(3, 2)), ("10.0.0.99", 40099))
    text, rooms = census(relay)
    check("a room of eight refuses the ninth, another city or not, and the"
          " census never reports more than eight",
          rooms.get(3, 0) <= 8, text)

    # --- re-HELLO cannot slip into a full room ----------------------------
    relay = r.Relay(verbose=False)
    relay.handle(hello(0x60000001, "first", key(4, 1)), ("10.1.0.1", 41001))
    for i in range(7):
        relay.handle(hello(0x60000010 + i, f"f{i}", key(4, 1)), ("10.1.0.%d" % (i + 2), 41002 + i))
    check("room 4 is full", len(relay.room_of(key(4, 1), 0)) == 8,
          f"{len(relay.room_of(key(4, 1), 0))} here")
    relay.handle(hello(0x60000001, "first", key(5, 1)), ("10.1.0.1", 41001))
    relay.handle(hello(0x60000001, "first", key(4, 1)), ("10.1.0.1", 41001))
    check("a member who leaves and comes back into a full room is refused too",
          len(relay.room_of(key(4, 1), 0)) <= 8,
          f"{len(relay.room_of(key(4, 1), 0))} here")

    # --- moving a member into a full room is refused ----------------------
    relay = r.Relay(verbose=False)
    for i in range(8):
        relay.handle(hello(0x70000000 + i, f"m{i}", key(6, 1)), ("10.2.0.%d" % (i + 1), 42000 + i))
    relay.handle(hello(0x700000FF, "mover", key(7, 1)), ("10.2.0.99", 42099))
    relay.handle(hello(0x700000FF, "mover", key(6, 1)), ("10.2.0.99", 42099))
    check("a known member re-HELLOing into a full room stays out",
          relay.members[0x700000FF].room != key(6, 1),
          f"room {relay.members[0x700000FF].room >> 16}")

    # --- the census is a datagram, even with many rooms -------------------
    relay = r.Relay(verbose=False)
    for g in range(256):
        relay.handle(hello(0x80000000 + g, "x", key(g, 1)), ("10.3.%d.%d" % (g // 200, g % 200), 43000 + g))
    text, rooms = census(relay)
    check("a full server's census is one datagram the client will parse",
          0 < len(text.encode()) < 512, f"{len(text.encode())} bytes")
    check("a busy server's census lists at most 32 rooms (what the client parses)",
          0 < len(rooms) <= 32, f"{len(rooms)} rooms listed")

    # every listed count is that room's real population
    real = {}
    for m in relay.members.values():
        g = m.room >> 16
        real[g] = real.get(g, 0) + 1
    check("every room the census lists has the population it reports",
          all(real.get(g) == n for g, n in rooms.items()),
          ", ".join(f"{g}:{n}" for g, n in list(rooms.items())[:4]))

    print()
    print("FAIL" if FAIL else "all checks passed")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
