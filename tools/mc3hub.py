#!/usr/bin/env python3
"""mc3hub -- a free-roam hub for ReDUB, written from the wire format.

What a hub is for
-----------------
ReDUB's free roam works without one: every client beacons and links directly,
which is all a two-player session needs. A hub adds two things:

  - **PEERS lists**, so clients learn about each other instead of relying on
    broadcast, which is what makes play across subnets possible;
  - **FAR lists**, the players beyond your direct links, drawn as map markers.

Nothing here is required for the game to run. `MC3_ROAM_HUB=0` turns the client
away from a hub entirely.

The protocol is rc_core's, from xboxrecomp/src/net/roam/roam_core.h:

    header, 16 bytes
      magic   "MC3R"            (0x4D 0x43 0x33 0x52)
      ver     u8   = 1
      type    u8
      len     u16  whole datagram, header + payload + crc
      sender  u32  nonzero, stable per sender
      seq     u16
      flags   u16
    payload
    crc32   u32  IEEE, of everything before it

    HELLO  core_ver u16, platform u8, status u8, listen_port u16, cfg_rev u16,
           room u32, name (len u8 + bytes), n_seen u8 + ids u32,
           [RC_HF_CREW: n_crews u8 ...], links u8
    PEERS  n u8, then n x { ip 4 bytes big-endian, port u16, id u32 }
    FAR    n u8, then n x { id u32, status u8, p f32 x3, q i16 x4,
                            name (len u8 + bytes) }

All integers little-endian except the PEERS ip, which is big-endian.

This implementation is deliberately small and dependency-free: one asyncio-stdlib
UDP socket, no frameworks. It is our own code; it was written from the header
above rather than from any other hub's source.

    python tools/mc3hub.py                 # listen on 0.0.0.0:47210
    python tools/mc3hub.py --port 47210 --verbose

Then point a client at it:
    set MC3_ROAM=1
    set MC3_ROAM_HUB_IP=127.0.0.1
    set MC3_ROAM_HUB_PORT=47210
"""

from __future__ import annotations

import argparse
import asyncio
import socket
import struct
import sys
import time
import zlib

MAGIC = b"MC3R"
PROTO_VER = 1
HDR = 16
CRC = 4
MAX_DGRAM = 1200

T_HELLO = 1
T_PEERS = 2
T_PING = 6
T_PONG = 7
T_BYE = 8

FLAG_HELLO_ALL = 0x01          # a HELLO addressed to everyone
FLAG_CREW = 0x02               # the crew extension is present in HELLO

# A player is forgotten after this long without a datagram. The client's own
# T_DROP is 10 s; a hub should be at least as patient, or it would hand out
# players the sender has already dropped.
EXPIRE_S = 30.0


class Player:
    __slots__ = ("id", "ip", "port", "name", "room", "links", "last_seen", "status")

    def __init__(self, pid: int, ip: str, port: int, name: str, room: int,
                 links: int, status: int) -> None:
        self.id = pid
        self.ip = ip
        self.port = port
        self.name = name
        self.room = room
        self.links = links
        self.status = status
        self.last_seen = time.monotonic()

    def age(self) -> float:
        return time.monotonic() - self.last_seen

    def __repr__(self) -> str:
        return f"<{self.name or '?'} {self.id:08x} {self.ip}:{self.port}>"


def crc32(data: bytes) -> int:
    """CRC-32 IEEE, as rc_crc32 computes it."""
    return zlib.crc32(data) & 0xFFFFFFFF


def build(msg_type: int, sender: int, payload: bytes,
          seq: int = 0, flags: int = 0) -> bytes:
    """A whole datagram: header, payload, CRC."""
    total = HDR + len(payload) + CRC
    hdr = MAGIC + struct.pack("<BBHIHH", PROTO_VER, msg_type, total, sender, seq, flags)
    body = hdr + payload
    return body + struct.pack("<I", crc32(body))


def parse(data: bytes):
    """(type, sender, seq, flags, header_ver, payload) or None."""
    if len(data) < HDR + CRC or len(data) > MAX_DGRAM:
        return None
    if data[:4] != MAGIC:
        return None
    ver, mtype, length, sender, seq, flags = struct.unpack_from("<BBHIHH", data, 4)
    if ver != PROTO_VER or length != len(data):
        return None
    want = struct.unpack_from("<I", data, len(data) - CRC)[0]
    if want != crc32(data[:-CRC]):
        return None
    return mtype, sender, seq, flags, ver, data[HDR:len(data) - CRC]


def parse_hello(payload: bytes, flags: int):
    """(core_ver, platform, status, port, cfg_rev, room, name, links, key) or None.

    `key` is the optional room key (0 when absent). It is the last 4 bytes a
    current client sends; see rc_key_hash() in roam_core.h. Anything after it
    belongs to a newer core and is ignored.
    """
    try:
        (core_ver, platform, status, port, cfg_rev, room) = struct.unpack_from("<HBBHHI", payload, 0)
        off = 12
        nlen = payload[off]
        off += 1
        name = payload[off:off + nlen].decode("latin-1")
        off += nlen
        n_seen = payload[off]
        off += 1 + 4 * n_seen
        links = 0
        if flags & FLAG_CREW:
            n_crews = payload[off] if off < len(payload) else 0
            off += 1
            # crews are not used by MC3; skip them defensively
            for _ in range(n_crews):
                if off >= len(payload):
                    break
                clen = payload[off]
                off += 1 + clen
        if off < len(payload):
            links = payload[off]
            off += 1
        key = 0
        if len(payload) - off >= 4:
            key = struct.unpack_from("<I", payload, off)[0]
        return core_ver, platform, status, port, cfg_rev, room, name, links, key
    except (IndexError, struct.error):
        return None


def peers_payload(members: list[Player], exclude_id: int) -> bytes:
    """PEERS: everybody but the receiver."""
    out = bytearray()
    entries = [p for p in members if p.id != exclude_id]
    out.append(len(entries) & 0xFF)
    for p in entries:
        out += socket.inet_aton(p.ip)          # 4 bytes, big-endian
        out += struct.pack("<H", p.port)
        out += struct.pack("<I", p.id)
    return bytes(out)


class Hub:
    def __init__(self, port: int, verbose: bool, link_limit: int) -> None:
        self.port = port
        self.verbose = verbose
        self.link_limit = link_limit
        self.players: dict[int, Player] = {}
        self.self_id = (int(time.time()) ^ (id(self) & 0xFFFF)) & 0xFFFFFFFF or 0x48424855
        self.self_id &= 0x7FFFFFFF
        if self.self_id == 0:
            self.self_id = 0x48424855
        self.seq = 0
        self.rx = 0
        self.tx = 0

    def say(self, text: str) -> None:
        print(f"[hub] {text}", flush=True)

    def log(self, text: str) -> None:
        if self.verbose:
            print(f"[hub] {text}", flush=True)

    def reap(self) -> None:
        gone = [pid for pid, p in self.players.items() if p.age() > EXPIRE_S]
        for pid in gone:
            p = self.players.pop(pid)
            self.say(f"left     {p.name or '?'} {pid:08x}  ({len(self.players)} here)")

    def members(self) -> list[Player]:
        """Everybody, newest heartbeat first, so the list is stable per client."""
        return sorted(self.players.values(), key=lambda p: p.id)

    def handle(self, data: bytes, addr) -> bytes | None:
        """One datagram in; a reply out, or None."""
        got = parse(data)
        if got is None:
            return None
        mtype, sender, seq, flags, _ver, payload = got
        self.rx += 1

        if mtype == T_BYE:
            p = self.players.pop(sender, None)
            if p:
                self.say(f"bye      {p.name or '?'} {sender:08x}  ({len(self.players)} here)")
            return None

        if mtype == T_PING:
            # Answer the keepalive directly: the client pings whoever it talks to.
            self.tx += 1
            return build(T_PONG, self.self_id, payload[:4] + struct.pack("<II", 0, 0), seq=seq)

        if mtype != T_HELLO:
            return None                       # SNAP/CFG/GAME are player to player

        hello = parse_hello(payload, flags)
        if hello is None:
            return None
        core_ver, platform, status, port, cfg_rev, room, name, links, _key = hello
        ip = addr[0]

        known = self.players.get(sender)
        if known is None:
            p = Player(sender, ip, port or addr[1], name, room, links, status)
            self.players[sender] = p
            self.say(f"joined   {name or '?'} {sender:08x} {ip}:{port}  "
                     f"room {room} links {links}  ({len(self.players)} here)")
        else:
            known.last_seen = time.monotonic()
            known.ip, known.port = ip, (port or addr[1])
            known.name, known.room, known.links = name, room, links
            known.status = status

        self.reap()

        # PEERS: the roster, minus the asker.
        self.tx += 1
        self.seq = (self.seq + 1) & 0xFFFF
        return build(T_PEERS, self.self_id, peers_payload(self.members(), sender),
                     seq=self.seq)


class HubProtocol(asyncio.DatagramProtocol):
    """The receive side.

    create_datagram_endpoint is used rather than a raw socket because Windows'
    default ProactorEventLoop has no sock_recvfrom; the datagram transport works
    on every platform and needs no event-loop policy to be changed at startup.
    """

    def __init__(self, hub: "Hub") -> None:
        self.hub = hub
        self.transport: asyncio.DatagramTransport | None = None
        self.last_reap = time.monotonic()

    def connection_made(self, transport) -> None:      # type: ignore[override]
        self.transport = transport

    def datagram_received(self, data: bytes, addr) -> None:   # type: ignore[override]
        reply = self.hub.handle(data, addr)
        if reply is not None and self.transport is not None:
            try:
                self.transport.sendto(reply, addr)
            except OSError as e:
                self.hub.say(f"send to {addr[0]}:{addr[1]}: {e}")
        now = time.monotonic()
        if now - self.last_reap >= 5.0:
            self.last_reap = now
            self.hub.reap()

    def error_received(self, exc) -> None:             # type: ignore[override]
        self.hub.log(f"transport: {exc}")


async def serve(hub: Hub, bind: str, port: int) -> None:
    loop = asyncio.get_running_loop()
    transport, _proto = await loop.create_datagram_endpoint(
        lambda: HubProtocol(hub),
        local_addr=(bind, port),
        allow_broadcast=True,
    )
    hub.say(f"listening on {bind}:{port} (UDP), id {hub.self_id:08x}")
    hub.say("point clients at it with MC3_ROAM_HUB_IP / MC3_ROAM_HUB_PORT")
    try:
        await asyncio.Event().wait()          # run until interrupted
    finally:
        transport.close()


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="ReDUB free-roam hub")
    ap.add_argument("--bind", default="0.0.0.0", help="address to listen on")
    ap.add_argument("--port", type=int, default=47210, help="UDP port (the roam port)")
    ap.add_argument("--verbose", action="store_true", help="log every message")
    ap.add_argument("--links", type=int, default=0,
                    help="advertised links per player (informational; 0 = leave to clients)")
    args = ap.parse_args(argv)

    hub = Hub(args.port, args.verbose, args.links)
    try:
        asyncio.run(serve(hub, args.bind, args.port))
    except KeyboardInterrupt:
        hub.say(f"stopped: {hub.rx} datagrams in, {hub.tx} out, "
                f"{len(hub.players)} player(s) still listed")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
