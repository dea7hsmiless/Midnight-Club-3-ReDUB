#!/usr/bin/env python3
"""mc3relay -- rooms and a relay for ReDUB free roam, so players behind NAT can meet.

Why this exists
---------------
Free roam links peers directly: every client beacons and talks to the others.
That works on one LAN and not at all between two homes, because neither
player can reach the other's private address and neither wants to open a port.

A relay fixes exactly that, and nothing else. Each client sends to the relay
(the address it was already configured with as its "hub"); the relay forwards
each datagram to the other players in the same room. Because every client
opened its NAT to the relay by sending to it, the return traffic is allowed --
no port forward, no hole punching, no public address for anybody.

What the client must do for this to work (and now does, `MC3_ROAM_RELAY=1`):

  - identify peers by the `sender` id in the header, not by source address,
    because every player's traffic arrives from the relay's one endpoint;
  - send to the relay endpoint for every peer;
  - treat relay traffic as peer traffic, not as hub bookkeeping.

Rooms
-----
`HELLO` already carries a `room` (u32). The relay groups by it: two clients in
room 1 never see room 2's players. `MC3_ROAM_ROOM=<n>` on the client picks one.

What this does not do
---------------------
No matchmaking (a player must know the room number), no auth, no encryption,
and no traffic analysis. It is a hobby relay, like the rest of the roam
transport. Do not run it expecting it to be safe on the open internet.

Run it:

    python tools/mc3relay.py --port 47210 --verbose

Then, on each client:

    MC3_ROAM=1
    MC3_ROAM_HUB=1
    MC3_ROAM_HUB_IP=<the relay's address>
    MC3_ROAM_HUB_PORT=47210
    MC3_ROAM_RELAY=1
    MC3_ROAM_ROOM=1

This module is our own, written from the wire format in
xboxrecomp/src/net/roam/roam_core.h. It shares that format with mc3hub.py.
"""

from __future__ import annotations

import argparse
import asyncio
import socket
import struct
import sys
import time

from mc3hub import HDR, MAX_DGRAM, PROTO_VER, build, crc32, parse, parse_hello

# Forgetting a player: longer than the client's own 10 s T_DROP, so the relay
# never hands out somebody the sender has already dropped.
EXPIRE_S = 30.0

# A client this quiet is not "gone" yet, but it must not be relayed to for ever.
T_HELLO = 1
T_PING = 6
T_PONG = 7
T_BYE = 8


class Member:
    __slots__ = ("id", "addr", "room", "name", "last_seen", "rx", "tx")

    def __init__(self, pid: int, addr, room: int, name: str) -> None:
        self.id = pid
        self.addr = addr
        self.room = room
        self.name = name
        self.last_seen = time.monotonic()
        self.rx = 0
        self.tx = 0

    def age(self) -> float:
        return time.monotonic() - self.last_seen

    def __repr__(self) -> str:
        return f"<{self.name or '?'} {self.id:08x} {self.addr[0]}:{self.addr[1]} room {self.room}>"


class Relay:
    def __init__(self, verbose: bool = False) -> None:
        self.verbose = verbose
        self.members: dict[int, Member] = {}
        self.relayed = 0
        self.dropped = 0

    def say(self, text: str) -> None:
        print(f"[relay] {text}", flush=True)

    def log(self, text: str) -> None:
        if self.verbose:
            print(f"[relay] {text}", flush=True)

    def reap(self) -> None:
        gone = [pid for pid, m in self.members.items() if m.age() > EXPIRE_S]
        for pid in gone:
            m = self.members.pop(pid)
            self.say(f"gone     {m.name or '?'} {pid:08x}  ({len(self.members)} here)")

    def room_of(self, room: int, except_id: int) -> list[Member]:
        return [m for m in self.members.values() if m.room == room and m.id != except_id]

    def handle(self, data: bytes, addr) -> list[tuple[bytes, tuple]]:
        """One datagram in; the datagrams to send out (payload, address)."""
        got = parse(data)
        if got is None:
            self.dropped += 1
            return []
        mtype, sender, seq, flags, _ver, payload = got
        if sender == 0:
            return []

        now = time.monotonic()
        known = self.members.get(sender)

        if mtype == T_BYE:
            if known:
                self.members.pop(sender, None)
                self.say(f"left     {known.name or '?'} {sender:08x}"
                         f"  ({len(self.members)} here)")
            return []

        if mtype == T_PING:
            # The client pings whoever it talks to; answer so its timing works.
            return [(build(T_PONG, 0x52454C41, payload[:4] + struct.pack("<II", 0, 0),
                           seq=seq), addr)]

        # HELLO is how a player is first seen, and how the room is learned.
        if mtype == T_HELLO:
            hello = parse_hello(payload, flags)
            if hello is None:
                self.dropped += 1
                return []
            _core_ver, _platform, _status, _port, _cfg_rev, room, name, _links = hello
            if known is None:
                m = Member(sender, addr, room, name)
                self.members[sender] = m
                self.say(f"joined   {name or '?'} {sender:08x} {addr[0]}:{addr[1]}"
                         f"  room {room}  ({len(self.members)} here)")
            else:
                known.last_seen = now
                known.addr = addr          # follow a NAT re-binding
                known.room = room
                known.name = name
            known = self.members[sender]
        else:
            if known is None:
                # Not a player we know: never relay a stranger onwards.
                self.dropped += 1
                return []
            known.last_seen = now
            known.addr = addr

        self.reap()

        out: list[tuple[bytes, tuple]] = []
        for other in self.room_of(known.room, sender):
            out.append((data, other.addr))
        if out:
            known.rx += 1
            known.tx += len(out)
            self.relayed += len(out)
            self.log(f"{sender:08x} -> {len(out)} in room {known.room} ({mtype})")
        return out


class RelayProtocol(asyncio.DatagramProtocol):
    """Receive side. create_datagram_endpoint, not a raw socket: Windows'
    default ProactorEventLoop has no sock_recvfrom (the hub hit this first)."""

    def __init__(self, relay: Relay) -> None:
        self.relay = relay
        self.transport: asyncio.DatagramTransport | None = None
        self.last_reap = time.monotonic()

    def connection_made(self, transport) -> None:      # type: ignore[override]
        self.transport = transport

    def datagram_received(self, data: bytes, addr) -> None:   # type: ignore[override]
        for payload, dest in self.relay.handle(data, addr):
            if self.transport is not None:
                try:
                    self.transport.sendto(payload, dest)
                except OSError as e:
                    self.relay.say(f"send to {dest[0]}:{dest[1]}: {e}")
        now = time.monotonic()
        if now - self.last_reap >= 5.0:
            self.last_reap = now
            self.relay.reap()

    def error_received(self, exc) -> None:             # type: ignore[override]
        self.relay.log(f"transport: {exc}")


async def serve(relay: Relay, bind: str, port: int) -> None:
    loop = asyncio.get_running_loop()
    transport, _proto = await loop.create_datagram_endpoint(
        lambda: RelayProtocol(relay),
        local_addr=(bind, port),
        allow_broadcast=True,
    )
    relay.say(f"listening on {bind}:{port} (UDP), relaying by room")
    relay.say("clients: MC3_ROAM_HUB_IP=<this host>, MC3_ROAM_RELAY=1, MC3_ROAM_ROOM=<n>")
    try:
        await asyncio.Event().wait()
    finally:
        transport.close()


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="ReDUB free-roam relay (rooms, NAT-friendly)")
    ap.add_argument("--bind", default="0.0.0.0", help="address to listen on")
    ap.add_argument("--port", type=int, default=47210, help="UDP port (the roam port)")
    ap.add_argument("--verbose", action="store_true", help="log every relayed datagram")
    args = ap.parse_args(argv)

    relay = Relay(args.verbose)
    try:
        asyncio.run(serve(relay, args.bind, args.port))
    except KeyboardInterrupt:
        relay.say(f"stopped: {relay.relayed} datagrams relayed, {relay.dropped} dropped, "
                  f"{len(relay.members)} player(s) still listed")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
