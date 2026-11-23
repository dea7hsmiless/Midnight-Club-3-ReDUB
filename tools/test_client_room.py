#!/usr/bin/env python3
"""End-to-end room test with the real game binary.

Starts the relay, a plain UDP client that joins the same room, and the game
itself (a copy of a built Windows binary with its own settings file), then
checks:

  * the game joins the room in its settings file (the relay says so);
  * the game's traffic reaches the other member of the room (the client
    receives the game's HELLOs and snapshots);
  * the census reports the room with both players.

This is the PC half of "two players in one room": the second player here is a
plain socket, which is enough to prove the game joins, announces and sends,
and that the relay carries it. Two game windows cannot share the roam port on
one machine, so the two-screen race itself is a two-device job.

  python tools/test_client_room.py --exe <dir with mcdub3remix.exe> [--room 9]
"""
import argparse
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mc3hub as h

HERE = Path(__file__).resolve().parent
ROOM = 9


def hello(sender, name, room):
    nb = name.encode("latin-1")
    payload = (struct.pack("<HBBHHI", 17, 1, 0, 47210, 1, room)
               + bytes([len(nb)]) + nb + bytes([0]) + bytes([7]))
    return h.build(h.T_HELLO, sender, payload, flags=h.FLAG_HELLO_ALL)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=r"C:\Users\Donte\Desktop\ReDUB-0.2.5-local-test",
                    help="folder holding mcdub3remix.exe and its DLLs")
    ap.add_argument("--room", type=int, default=ROOM)
    ap.add_argument("--port", type=int, default=47210)
    ap.add_argument("--timeout", type=int, default=150)
    args = ap.parse_args()

    src = Path(args.exe)
    exe = src / "mcdub3remix.exe"
    if not exe.exists():
        print("no mcdub3remix.exe in", src)
        return 2

    work = Path(tempfile.mkdtemp(prefix="roomtest-"))
    for f in src.iterdir():
        if f.is_file() and (f.name.endswith(".dll") or f.name == "mcdub3remix.exe"):
            shutil.copy2(f, work / f.name)
    game_dir = os.environ.get("MC3_GAME_DIR", "")
    env_file = work / "mcdub3remix_env.txt"
    env_file.write_text(
        "MC3_ONLINE=1\n"
        f"MC3_ROAM_JOIN=127.0.0.1\n"
        f"MC3_ROAM_HUB_PORT={args.port}\n"
        f"MC3_ROAM_ROOM={args.room}\n"
        "MC3_ROAM_NAME=ClientTest\n"
        "MC3_NETPLAY=1\n"
        + (f"MC3_GAME_DIR={game_dir}\n" if game_dir else "")
        + "SDL_VIDEO_MINIMIZE_ON_FOCUS_LOSS=0\n",
        encoding="ascii")

    relay = subprocess.Popen([sys.executable, str(HERE / "mc3relay.py"),
                              "--port", str(args.port), "--verbose"],
                             stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    lines: list[str] = []

    def reader():
        for raw in relay.stdout:                 # type: ignore[union-attr]
            lines.append(raw.decode("utf-8", "replace").rstrip())

    threading.Thread(target=reader, daemon=True).start()
    for _ in range(60):
        if any("listening" in l for l in lines):
            break
        time.sleep(0.1)

    # a plain member in the same room, so the game has someone to talk to
    peer = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    peer.bind(("127.0.0.1", 0))
    peer.settimeout(0.5)
    dst = ("127.0.0.1", args.port)
    peer.sendto(hello(0x22220001, "PeerOne", (args.room << 16) | 1), dst)
    time.sleep(0.2)

    game = subprocess.Popen([str(work / "mcdub3remix.exe")], cwd=str(work),
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    fail = 0
    joined = False
    heard: dict[int, int] = {}
    deadline = time.time() + args.timeout
    try:
        while time.time() < deadline:
            if not joined:
                for line in lines:
                    if "joined" in line and "ClientTest" in line:
                        joined = True
                        print("ok   the game joined the room:", line.split("  ", 1)[-1].strip())
                        break
            try:
                data, _ = peer.recvfrom(2048)
            except socket.timeout:
                continue
            got = h.parse(data)
            if got:
                heard[got[0]] = heard.get(got[0], 0) + 1
            if joined and heard.get(h.T_HELLO) and heard.get(3):
                break
        if not joined:
            print("FAIL the game never joined the room")
            fail = 1
        print("     message types received from the game:",
              ", ".join(f"{k}x{v}" for k, v in sorted(heard.items())) or "none")
        if not heard.get(h.T_HELLO):
            print("FAIL the game's HELLO never reached the other member")
            fail = 1
        else:
            print("ok   the game's HELLO reached the other member of the room")

        # the census sees both players
        peer.sendto(b"MC3Q", dst)
        text = ""
        for _ in range(8):
            try:
                data, _ = peer.recvfrom(2048)
            except socket.timeout:
                break
            if data.startswith(b"MC3ROOMS"):
                text = data.decode("latin-1")
                break
        print("     census:", text or "(no answer)")
        want = re.search(rf"\b{args.room}:2\b", text)
        if want:
            print(f"ok   the census reports room {args.room} with both players")
        else:
            print(f"FAIL the census did not report {args.room}:2")
            fail = 1
    finally:
        game.terminate()
        try:
            game.wait(timeout=5)
        except subprocess.TimeoutExpired:
            game.kill()
        relay.terminate()
        try:
            relay.wait(timeout=5)
        except subprocess.TimeoutExpired:
            relay.kill()
        peer.close()
        shutil.rmtree(work, ignore_errors=True)
    print("FAIL" if fail else "all checks passed")
    return fail


if __name__ == "__main__":
    sys.exit(main())
