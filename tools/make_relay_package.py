#!/usr/bin/env python3
"""Build the uploadable relay-server package: release/ReDUB-relay-server-<ver>.zip

The package is what a host actually needs -- one small folder, no install, no
dependencies (Python standard library only):

    mc3relay.py           the relay itself
    mc3hub.py             the wire-format helpers it imports
    start.sh              Linux/macOS: one command to run it
    start.bat             Windows: double-click to run it
    Dockerfile            docker build -t redub-relay . && docker run ...
    docker-compose.yml    docker compose up -d
    redub-relay.service   systemd unit
    README.md             quick start: upload, open the port, point clients at it
    VERSION

Run:  python tools/make_relay_package.py
"""

from __future__ import annotations

import os
import re
import shutil
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLS = ROOT / "tools"
RELEASE = ROOT / "release"


def app_version() -> str:
    try:
        text = (ROOT / "config" / "remix" / "app.env").read_text(encoding="utf-8")
        m = re.search(r'^APP_VERSION="(.*)"', text, re.M)
        if m:
            return m.group(1)
    except OSError:
        pass
    return "dev"


README = """# ReDUB free-roam relay server

A small relay that lets two players in different houses play Midnight Club 3: DUB Edition
Remix free roam together. Upload this folder, open one UDP port, done.

No dependencies. No install. Python 3.8 or newer (standard library only).

---

## 1. Run it

**Linux / macOS**

```sh
./start.sh
```

**Windows**

Double-click `start.bat`, or:

```bat
start.bat
```

You should see:

```
[relay] listening on 0.0.0.0:47210 (UDP), relaying by room
```

Leave it running. `./start.sh 50000` uses another port.

## 2. Open the port

The relay needs **UDP 47210** reachable from the internet. Players need nothing.

- **Linux:** `sudo ufw allow 47210/udp`
- **Windows:** `New-NetFirewallRule -DisplayName "ReDUB relay" -Direction Inbound -Protocol UDP -LocalPort 47210 -Action Allow`
- **Router:** forward **UDP 47210** to this machine.
- **VPS/cloud:** also open UDP 47210 in the provider's firewall/security group — both are
  usually on, and forgetting the second one is the most common reason nothing connects.

## 3. Point the players at it

Each player edits `mcdub3remix_env.txt` in their install folder (the launcher's Settings
write the same keys):

```
MC3_ROAM=1
MC3_ROAM_HUB_IP=<this server's public address>
MC3_ROAM_HUB_PORT=47210
MC3_ROAM_RELAY=1
MC3_ROAM_ROOM=1
MC3_ROAM_NAME=<the player's name>
```

Both players must use the **same room number**. Then both enter free roam.

Each client should log:

```
[roam] relay <address>:47210 (group 1): datagrams go through it
[roam] <name> joined
```

and this server logs:

```
[relay] joined   <name> <id> <ip>:<port>  room <n>  (2 here)
```

If you see `joined` on the server but no car in game, the two clients are in different
rooms, or one is missing `MC3_ROAM_RELAY=1`.

## 4. Keep it running

**systemd (Linux):**

```sh
sudo cp redub-relay.service /etc/systemd/system/
sudo systemctl enable --now redub-relay
journalctl -u redub-relay -f
```

Edit the unit first if this folder is not `/opt/redub-relay`.

**Docker:**

```sh
docker build -t redub-relay .
docker run -d --name redub-relay --restart unless-stopped -p 47210:47210/udp redub-relay
```

or `docker compose up -d`.

**Windows:** run `start.bat` from a Task Scheduler job set to "run whether user is logged on
or not", so it survives a logout.

## Capacity

The relay forwards every datagram to every other player in the room, so cost grows with the
square of the room size. Eight players is the practical ceiling (that is the game's own
player-slot count) at roughly 1.2 MB/s. A cheap VPS is plenty.

## What this is not

No accounts, no auth, no encryption, no matchmaking. Anyone who knows the address and a room
number can join that room, and traffic is plain UDP. Fine for playing with friends; not
something to advertise as a public service.

`VERSION` in this folder says which build the package came from.
"""

START_SH = """#!/usr/bin/env bash
# Run the ReDUB relay. Port: first argument, or MC3_RELAY_PORT, or 47210.
set -euo pipefail
cd "$(dirname "$0")"
PORT="${1:-${MC3_RELAY_PORT:-47210}}"
exec python3 mc3relay.py --port "$PORT" "$@"
"""

START_BAT = """@echo off
rem Run the ReDUB relay. Port: first argument, or MC3_RELAY_PORT, or 47210.
setlocal
cd /d "%~dp0"
set "PORT=%~1"
if "%PORT%"=="" set "PORT=%MC3_RELAY_PORT%"
if "%PORT%"=="" set "PORT=47210"
python mc3relay.py --port %PORT%
pause
"""

DOCKERFILE = """# ReDUB free-roam relay. No dependencies: Python standard library only.
FROM python:3-slim
WORKDIR /relay
COPY mc3relay.py mc3hub.py ./
EXPOSE 47210/udp
CMD ["python", "mc3relay.py", "--port", "47210"]
"""

COMPOSE = """services:
  redub-relay:
    build: .
    container_name: redub-relay
    restart: unless-stopped
    ports:
      - "47210:47210/udp"
"""

SERVICE = """[Unit]
Description=ReDUB free-roam relay
After=network.target

[Service]
Type=simple
# Change this path if the folder is not here:
WorkingDirectory=/opt/redub-relay
ExecStart=/usr/bin/python3 mc3relay.py --port 47210
Restart=always
RestartSec=5
User=redub
Group=redub

[Install]
WantedBy=multi-user.target
"""


def build() -> Path:
    ver = app_version()
    ver_short = ".".join(ver.split(".")[:2]) if "." in ver else ver
    stage = ROOT / "work-remix" / "relay-package"
    if stage.exists():
        shutil.rmtree(stage)
    stage.mkdir(parents=True)

    for name in ("mc3relay.py", "mc3hub.py"):
        shutil.copy2(TOOLS / name, stage / name)
    (stage / "README.md").write_text(README, encoding="utf-8", newline="\n")
    (stage / "start.sh").write_text(START_SH, encoding="utf-8", newline="\n")
    (stage / "start.bat").write_text(START_BAT.replace("\n", "\r\n"), encoding="ascii")
    (stage / "Dockerfile").write_text(DOCKERFILE, encoding="utf-8", newline="\n")
    (stage / "docker-compose.yml").write_text(COMPOSE, encoding="utf-8", newline="\n")
    (stage / "redub-relay.service").write_text(SERVICE, encoding="utf-8", newline="\n")
    (stage / "VERSION").write_text(f"ReDUB relay server from ReDUB {ver}\n", encoding="utf-8")
    if (ROOT / "LICENSE").exists():
        shutil.copy2(ROOT / "LICENSE", stage / "LICENSE")

    RELEASE.mkdir(exist_ok=True)
    out = RELEASE / f"ReDUB-relay-server-{ver_short}.zip"
    if out.exists():
        out.unlink()
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        for f in sorted(stage.iterdir()):
            z.write(f, f"ReDUB-relay-server/{f.name}")
    return out


def main() -> int:
    out = build()
    size = out.stat().st_size
    with zipfile.ZipFile(out) as z:
        names = z.namelist()
    print(f"built {out}  ({size} bytes)")
    for n in names:
        print(f"  {n}")
    print()
    print("upload the zip, extract it, run ./start.sh (or start.bat), open UDP 47210")
    return 0


if __name__ == "__main__":
    sys.exit(main())
