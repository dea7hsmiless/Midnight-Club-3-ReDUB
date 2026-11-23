"""Live check: run the relay as a process, join it as two players, ask the
census, and confirm the answer. Complements the in-process unit tests by
exercising the real entry point (argparse, asyncio, the socket) with two
separate client sockets, the way two machines look to the relay."""
import socket
import struct
import subprocess
import sys
import threading
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import mc3hub as h

PY = sys.executable
HERE = Path(__file__).resolve().parent
RELAY = str(HERE / "mc3relay.py")
PORT = 47611


def hello(sender, name, room):
    nb = name.encode("latin-1")
    payload = (struct.pack("<HBBHHI", 17, 1, 0, 47210, 1, room)
               + bytes([len(nb)]) + nb + bytes([0]) + bytes([7]))
    return h.build(h.T_HELLO, sender, payload, flags=h.FLAG_HELLO_ALL)


def main() -> int:
    proc = subprocess.Popen([PY, RELAY, "--port", str(PORT)],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    lines: list[str] = []

    def reader():
        for raw in proc.stdout:                      # type: ignore[union-attr]
            lines.append(raw.decode("utf-8", "replace").rstrip())

    threading.Thread(target=reader, daemon=True).start()

    dst = ("127.0.0.1", PORT)
    a = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    b = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    for s in (a, b):
        s.settimeout(0.5)
    fail = 0
    text = ""
    try:
        deadline = time.time() + 8
        while time.time() < deadline and not any("listening" in l for l in lines):
            time.sleep(0.1)
        a.sendto(hello(0x11110001, "alice", (3 << 16) | 1), dst)
        b.sendto(hello(0x11110002, "bob", (3 << 16) | 1), dst)
        time.sleep(0.3)
        for _ in range(12):
            try:
                a.sendto(b"MC3Q", dst)
                data, _ = a.recvfrom(2048)
            except (socket.timeout, ConnectionResetError):
                continue
            if data.startswith(b"MC3ROOMS"):
                text = data.decode("latin-1")
                break
            # anything else is a relayed roam datagram (bob's HELLO): skip
        if text.startswith("MC3ROOMS 1 1 3:2"):
            print("census:", text)
            print("ok   two players in room 3 are reported as 3:2")
        else:
            print("FAIL unexpected census:", repr(text))
            fail = 1
    finally:
        for s in (a, b):
            s.close()
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
        print("--- relay output ---")
        for line in lines[:8]:
            print(" ", line)
    print("FAIL" if fail else "all checks passed")
    return fail


if __name__ == "__main__":
    sys.exit(main())
