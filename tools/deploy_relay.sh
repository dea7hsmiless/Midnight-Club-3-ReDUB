#!/usr/bin/env bash
# Update the ReDUB relay on the server, and refuse to leave it on an old build.
#
#   bash deploy_relay.sh ReDUB-relay-server-0.2.5.zip
#
# Why this exists: copying the files by hand, restarting before the copy, or
# re-extracting a stale zip in /tmp all look successful, and the relay keeps
# answering the room census (MC3Q) from the old code. The only reliable proof
# is the roster query (MC3P), which old relays do not answer at all -- so this
# script checks it before and after the restart and fails loudly if the running
# process is not the new one.
set -euo pipefail

ZIP=${1:?usage: deploy_relay.sh <ReDUB-relay-server-0.2.5.zip>}
DEST=${DEST:-/opt/redub-relay}
UNIT=${UNIT:-redub-relay}
PORT=${PORT:-47210}

[ -f "$ZIP" ] || { echo "no such file: $ZIP" >&2; exit 1; }

# 1. The zip itself must carry the roster query.
unzip -p "$ZIP" ReDUB-relay-server/mc3relay.py | grep -q "def roster" \
    || { echo "this zip is an old relay build (no roster query); upload the current one" >&2; exit 1; }

# 2. Keep the previous tree, stamped, so a revert is a single cp away.
BACKUP="$DEST.bak-$(date +%Y%m%d-%H%M%S)"
cp -a "$DEST" "$BACKUP"
echo "backup: $BACKUP"

# 3. Stage and copy the files (the trailing /. is the point).
STAGE=$(mktemp -d)
trap 'rm -rf "$STAGE"' EXIT
unzip -q "$ZIP" -d "$STAGE"
cp -a "$STAGE/ReDUB-relay-server/." "$DEST/"

# 4. The file on disk must be the new one before anything restarts.
grep -q "def roster" "$DEST/mc3relay.py" \
    || { echo "copy did not land: $DEST/mc3relay.py has no roster query" >&2; exit 1; }
ls -l "$DEST/mc3relay.py"

# 5. Restart, then prove the running process answers MC3P.
systemctl restart "$UNIT"
sleep 1

python3 - "$DEST" "$PORT" <<'PY'
import socket, struct, sys, time
dest, port = sys.argv[1], int(sys.argv[2])
sys.path.insert(0, dest)
import mc3hub as h

def hello(sender, name, room):
    nb = name.encode("latin-1")
    payload = (struct.pack("<HBBHHI", 17, 1, 0, port, 1, room)
               + bytes([len(nb)]) + nb + bytes([0]) + bytes([7]))
    return h.build(h.T_HELLO, sender, payload, flags=h.FLAG_HELLO_ALL)

s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
s.settimeout(3)
# A player in room 5, so the roster has something to list: testing MC3P on an
# empty server is what let a crash-on-member bug reach the live one.
s.sendto(hello(0x5EED0001, "DeployCheck", 5 << 16), ("127.0.0.1", port))
time.sleep(0.5)
s.sendto(b"MC3P", ("127.0.0.1", port))
data, _ = s.recvfrom(2048)
text = data.decode(errors="replace")
print("relay answered:", text)
if not text.startswith("MC3PLAYERS") or "DeployCheck" not in text:
    raise SystemExit("the roster did not list the test player")
s.sendto(b"MC3V", ("127.0.0.1", port))
print("version:", s.recvfrom(256)[0].decode(errors="replace"))
PY

echo "relay is updated and answering MC3P"
echo "if anything is wrong: systemctl stop $UNIT; cp -a $BACKUP/. $DEST/; systemctl start $UNIT"
