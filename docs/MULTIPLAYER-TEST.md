# Testing free roam across two devices

The point of this test is the one thing no single machine can prove: two copies, two
networks, one room. Everything below has been run at least once on one machine; the
second device is what is being tested.

Read the failure modes at the end before you start — three of them have already cost a
test run each, and every one looks like "the server is down".

## 1. The server

On the VPS, in the relay folder:

```sh
ss -lunp | grep 47210          # something already listening?
pkill -f mc3relay.py           # stop the old one, if so
nohup bash start.sh >/dev/null 2>&1 &
tail -f relay.log
```

Startup says:

```
[relay] listening on 0.0.0.0:47210 (UDP), relaying by room
[relay] clients: MC3_ROAM_HUB_IP=<this host>, MC3_ROAM_RELAY=1, MC3_ROAM_ROOM=<n>
```

If it does not say `listening`, the port is taken or the firewall is dropping it — fix that
first, because nothing else can work.

Check the port is open from outside before blaming the game:

```sh
# from the player's machine
python -c "import socket;s=socket.socket(socket.AF_INET,socket.SOCK_DGRAM);s.settimeout(2);print(s.connect_ex(('162.35.56.66',47210)))"
```

`0` means the path exists. A hang or an error means UDP 47210 is still closed at the
provider's firewall — the most common cause, and there are usually two firewalls.

## 2. Each player

`mcdub3remix_env.txt` in the install folder (PC) or `/storage/emulated/0/mcdub3remix-nx/`
(Android). **One line** is the whole online setup:

```
MC3_ROAM_JOIN=<the server>
```

That is `MC3_ROAM=1`, a hub, the relay, and the address, in one key. On Windows the file is
created on first run from the example beside the exe, so the line is all you add.

Optional: `MC3_ROAM_ROOM=<n>` (0 by default; **both players must use the same number**) and
`MC3_ROAM_NAME=<a name>`.

The seven-key form from earlier builds still works — see `docs/SERVER-SETUP.md` — but there is
no reason to use it: the failure mode of leaving one out is a client that looks exactly like a
server that is down.

## 3. What each side should log

Client:

```
[roam] relay 162.35.56.66:47210 (group 7): datagrams go through it
[roam] started as "Donte" (id 1a2b3c4d, 7 links wanted)
[roam] udp platform on port 47210, 0 peer(s)
[roam] Donte joined
[ROAM] pool: 1 peers, 1 cars (1 slots + 0 standalone), 0 dimmed, heap 900 KB
```

With `rd1:` in the file the address prints as `**.**.**.**`, which is the point.

Server:

```
[relay] joined   Donte 1a2b3c4d 203.0.113.9:51544  room 7  (1 here)
[relay] joined   Phone 5e6f7a8b 198.51.100.4:61222  room 7  (2 here)
```

`(2 here)` is the moment multiplayer is real. If both clients say `joined` but the server
never shows a second `(N here)`, they are in different rooms.

## 4. In game

Both players: Online → Free Roam, then drive. The other car appears with the other player's
name over it. Nothing else needs to be set up, and there is no lobby to join.

## Failure modes, in the order they actually happen

| What you see | What it is |
|---|---|
| `[ROAM] transport did not start (port busy, or no network)` on Android | the APK predates `android.permission.INTERNET`; Android refused `socket()`. Install the 0.2.5 APK. |
| No `[roam] relay …` line at all | `MC3_ROAM_JOIN` missing or misspelled, or on an older build `MC3_ROAM=1` not set. The log says which if the address did not resolve. |
| `[roam] hub 10.77.0.253:47210` | the address did not resolve and the compile-time fallback was used. The line above it says why. |
| `no free-roam hub answers` | the packets are not arriving: firewall, wrong port, or the relay is not running. |
| Both `joined`, no car | different room numbers, or the one player's game is in a different mode than free roam. |
| Works on one machine, not across two | the client has `RECOMP_NET_ADDR=127.0.0.x` set. A loopback source cannot reach a public address; unset it. |
| Relay logs `refused … room is full` | 8 players already in that room. |

## 5. Races (System Link through the relay)

Free roam and races are two different menus and two different paths; both end up
on the relay.

Each player's env file needs `MC3_NETPLAY=1` as well as the roam line:

```
MC3_ROAM_JOIN=<the server>
MC3_NETPLAY=1
```

Then, in game: **Online → System Link**. One player creates a match, the other
finds it and joins. (Free roam is Online → Free Roam; the two work at once.)

### What proves it worked

Client, at startup:

```
[ROAM] System Link will go through the relay (virtual LAN, MC3_NETPLAY=1 to use it)
[XNET] relay: virtual LAN 10.77.103.22 (System Link through the free-roam relay)
[XNET] HLE up: transport relay, address 10.77.103.22
```

Both lines must show the **same 10.77 address**; that is the identity the other
player's guest will see. A public-looking address here is the byte-order bug
fixed on 2026-11-22, and means the build is older than that.

Server, with `--verbose`, once a System Link packet moves:

```
[relay] 1e672f69 -> 1 in room 0 (11)
```

`(11)` is the TUNNEL message: that is a System Link packet being carried. No
`(11)` lines while both players sit in the System Link menu means the guests are
not talking (wrong build, or `MC3_NETPLAY` not set).

### Failure modes

| What you see | What it is |
|---|---|
| `[XNET] HLE up: transport udp` | the build predates the relayed System Link, or `MC3_ROAM_JOIN`/`MC3_ROAM_RELAY` is not set, so there is no relay session to tunnel through. |
| address is `14.42.77.10`-shaped | byte-order bug, fixed 2026-11-22; install the newer build. |
| `[XNET] udp: host port = 20000 …` | the host-UDP transport was selected: it only reaches a LAN, and uses `RECOMP_NET_ADDR`/`RECOMP_NET_PEERS`. |
| System Link menu says no cable | `MC3_NETPLAY=1` missing; `XNetGetEthernetLinkStatus` is the gate. |
| both in the lobby, one list empty | the two are in different rooms (`MC3_ROAM_ROOM`), or the relay never saw a HELLO from one. |
| `(11)` on the server, no lobby | the guests are exchanging packets but the session ids do not match — check the room number first. |

## What this does not test

- Two players on the same room number but different cities: the room is a group with the
  city packed under it, so this is by design.
- Anything past 8 cars in one room — that is the cap, and the game's own slot count.
- A full race from start to finish. The transport is tested (tools/test_roam_tunnel.py)
  and the frames are visible on the relay, but the lobby and the race itself are the
  players' part of the test — see section 5.
