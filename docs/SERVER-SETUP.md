# Running a free-roam server

A relay server lets two players in different houses play together. Without one, free roam
only links players on the same LAN.

This is the 0.2.6 server (`tools/mc3relay.py`). It is **not** part of 0.2.5 and 0.2.5 does not
mention it.

---

## What it is

One Python file, standard library only, no dependencies. It listens on one UDP port and:

- **relays** each player's datagrams to the other players in the same **room**;
- **answers PING**, so the client's timing works;
- **forgets** a player after 30 s of silence.

Relaying is what makes NAT irrelevant: each client opens its own NAT by sending to the
server, the server replies from that same address, and the return traffic is allowed. No
player needs a port forward or a public address — only the server does.

`HELLO` already carries a room number, so rooms need no new protocol:
`MC3_ROAM_ROOM=1` on each client is all it takes.

## Requirements

| Item | Value |
|---|---|
| Python | 3.8 or newer (standard library only) |
| CPU | negligible; one process, one port |
| Memory | a few MB |
| Port | **UDP 47210** (the roam port), reachable from the internet |
| Bandwidth | see below |

## Quick start

**Linux / macOS:**

```sh
cd source
python3 tools/mc3relay.py --port 47210
```

**Windows:**

```bat
cd source
python tools\mc3relay.py --port 47210
```

You should see:

```
[relay] listening on 0.0.0.0:47210 (UDP), relaying by room
[relay] clients: MC3_ROAM_HUB_IP=<this host>, MC3_ROAM_RELAY=1, MC3_ROAM_ROOM=<n>
```

Add `--verbose` to log every relayed datagram while testing.

## Make the port reachable

1. **Firewall:** allow inbound **UDP 47210**.
   - Linux: `sudo ufw allow 47210/udp`
   - Windows: `New-NetFirewallRule -DisplayName "ReDUB relay" -Direction Inbound -Protocol UDP -LocalPort 47210 -Action Allow`
2. **Router:** forward **UDP 47210** to the machine running the relay.
3. **Cloud/VPS:** open the port in the provider's firewall/security group as well as the OS
   firewall — both are usually on.

Only the server needs this. Players need nothing.

## Client settings

Put these in each player's `mcdub3remix_env.txt` (launcher → Settings writes the same keys):

```
MC3_ROAM=1
MC3_ROAM_HUB=1
MC3_ROAM_HUB_IP=<the server's public address>
MC3_ROAM_HUB_PORT=47210
MC3_ROAM_RELAY=1
MC3_ROAM_ROOM=1
MC3_ROAM_NAME=<the player's name>
```

- `MC3_ROAM_ROOM` must be **the same number on both clients**. Different numbers are
  different rooms and never see each other.
- The room number is a **group**: the game also tags its room with the city it loads, and the
  city is packed under your group. So two players who pick room 1 stay together at San Diego,
  and a third in room 2 never appears — which is what you want.
- `MC3_ROAM_NAME` is what the other player sees in the player list and over the car.
- `MC3_ROAM_RELAY=1` is what turns on relay behaviour. Without it the client expects to reach
  each peer directly, which only works on a LAN.

Then both players enter free roam the same way (Online → Free Roam). Each should log:

```
[roam] relay <ip>:47210 (room 1): datagrams go through it
[roam] <name> joined
[ROAM] pool: 1 peers, 1 cars
```

## Running it as a service

**systemd (Linux):**

```ini
# /etc/systemd/system/redub-relay.service
[Unit]
Description=ReDUB free-roam relay
After=network.target

[Service]
Type=simple
WorkingDirectory=/opt/redub/source
ExecStart=/usr/bin/python3 tools/mc3relay.py --port 47210
Restart=always
RestartSec=5

[Install]
WantedBy=multi-user.target
```

```sh
sudo systemctl enable --now redub-relay
journalctl -u redub-relay -f
```

**Windows:** run it under [NSSM](https://nssm.cc/) or a Task Scheduler job with "run whether
user is logged on or not", so it survives a logout. Do not leave it in a console window and
expect it to stay up.

**Docker:** any base with Python 3 will do; publish `47210/udp`.

```sh
docker run -d --name redub-relay --restart unless-stopped \
  -p 47210:47210/udp \
  -v /opt/redub/source:/srv -w /srv python:3-slim \
  python3 tools/mc3relay.py --port 47210
```

## Capacity

The relay forwards every datagram to every other member of the room, so the cost grows with
the square of the room size. With the client's defaults (20 snapshots/s, up to ~1.2 KB each):

| Players in a room | Roughly |
|---|---|
| 2 | ~50 KB/s through the relay |
| 4 | ~300 KB/s |
| 8 | ~1.2 MB/s |

Eight is the practical ceiling anyway — that is the game's own player-slot count, and the
heap supports about eight remote cars (see `docs/0.2.6-PLAN.md`). Plan for 8, not 64.

A cheap VPS handles that easily; the limit is bandwidth, not CPU.

## Troubleshooting

| Symptom | Look at |
|---|---|
| `[relay]` never prints `joined` | The client never reached the server. Check the address and that UDP 47210 is open end to end (try from another network). |
| Relay logs `joined` but no car appears | Different `MC3_ROAM_ROOM`, or the client lacks `MC3_ROAM_RELAY=1`. Both are in the client's log as `[roam] relay ... (room N)`. |
| Works for one player, not the other | Almost always an inbound firewall on the *server* or a provider security group. |
| `gone` every 30 s | The client stopped sending. Check the game is actually in free roam; the client only streams while driving. |

The relay's own log is the first thing to read: `joined` with a room number means the
transport is fine and the problem is above it.

## What it is not

No accounts, no auth, no encryption, no matchmaking. Anyone who knows a room number and the
address can join that room, and traffic is plain UDP. Treat it as a hobby server: fine for
playing with friends, not something to expose as a public service.

Matchmaking (a room list instead of a number players must share) is a later step; the relay
currently assumes players agree on a room number out of band.
