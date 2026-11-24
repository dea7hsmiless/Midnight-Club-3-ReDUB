#!/usr/bin/env python3
"""Validate nicknames and observe a real session rename over UDP."""
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time

import mc3hub

ROOT = Path(__file__).resolve().parents[1]


def main():
    cc = os.environ.get("CC") or shutil.which("clang") or shutil.which("cc")
    if os.name == "nt":
        msys = Path(os.environ.get("MSYS2_ROOT", os.environ["SystemDrive"] + os.sep))
        if "MSYS2_ROOT" not in os.environ:
            msys /= "msys64"
        compiler = msys / "clang64/bin/clang.exe"
        if compiler.exists():
            cc = str(compiler)
    assert cc, "C compiler required"
    code = r'''
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "roam_api.h"
#include "roam_name.h"
#ifdef _WIN32
#include <windows.h>
#define pause_ms(n) Sleep(n)
#else
#include <unistd.h>
#define pause_ms(n) usleep((n)*1000)
#endif
int main(void) {
    char name[MC3_NICKNAME_MAX+1];
    assert(mc3_nickname_normalize("  Racer_2-.  ",name));
    assert(!strcmp(name,"Racer_2-."));
    assert(mc3_nickname_normalize("123456789012345678901234",name));
    assert(!mc3_nickname_normalize("1234567890123456789012345",name));
    assert(!mc3_nickname_normalize("   ",name));
    assert(!mc3_nickname_normalize("bad\nname",name));
    assert(!mc3_nickname_normalize("bad=name",name));
    assert(!mc3_nickname_normalize("bad#name",name));
    assert(!mc3_nickname_normalize("\xc3\xa9",name));
    assert(roam_set_name("Before")==0);
    assert(roam_start("Before",0)==0);
    pause_ms(1100);
    assert(roam_set_name("  After_2  ")==0);
    assert(!strcmp(getenv("MC3_ROAM_NAME"),"After_2"));
    assert(roam_set_name("bad=name")==-1);
    assert(!strcmp(getenv("MC3_ROAM_NAME"),"After_2"));
    pause_ms(1100);
    roam_stop();
    return 0;
}
'''
    with tempfile.TemporaryDirectory() as tmp, socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as server:
        work = Path(tmp)
        source = work / "nickname.c"
        exe = work / ("nickname.exe" if os.name == "nt" else "nickname")
        source.write_text(code, encoding="utf-8")
        roam = ROOT / "xboxrecomp/src/net/roam"
        cmd = [cc, str(source), *(str(roam / n) for n in ("roam_api.c", "roam_core.c", "roam_plat.c")),
               "-I", str(roam), "-o", str(exe), "-lpthread"]
        if os.name == "nt":
            cmd.append("-lws2_32")
        built = subprocess.run(cmd, capture_output=True)
        assert built.returncode == 0, built.stderr.decode("utf-8")
        server.bind(("127.0.0.1", 0))
        server.settimeout(0.2)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as spare:
            spare.bind(("127.0.0.1", 0))
            client_port = spare.getsockname()[1]
        env = {k: v for k, v in os.environ.items() if not k.startswith("MC3_ROAM_")}
        env.update(MC3_ROAM_HUB="1", MC3_ROAM_HUB_IP="127.0.0.1", MC3_ROAM_RELAY="1",
                   MC3_ROAM_HUB_PORT=str(server.getsockname()[1]), MC3_ROAM_PORT=str(client_port),
                   MC3_ROAM_ROOM="17")
        seen = {}
        with (work / "process.log").open("wb") as log:
            proc = subprocess.Popen([str(exe)], env=env, stdout=log, stderr=log)
            try:
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline and (proc.poll() is None or len(seen) < 2):
                    try:
                        packet, _ = server.recvfrom(2048)
                    except socket.timeout:
                        if proc.poll() is not None:
                            break
                        continue
                    decoded = mc3hub.parse(packet)
                    if decoded and decoded[0] == mc3hub.T_HELLO:
                        hello = mc3hub.parse_hello(decoded[5], decoded[3])
                        if hello:
                            seen[hello[6]] = (decoded[1], hello[5])
                assert proc.wait(timeout=3) == 0
            finally:
                if proc.poll() is None:
                    proc.kill()
                    proc.wait()
        assert "Before" in seen and "After_2" in seen, seen
        assert seen["Before"] == seen["After_2"], seen
        assert seen["After_2"][1] >> 16 == 17, seen
        print("PASS: nickname limits/normalization, invalid-input rejection, live HELLO rename, unchanged identity and room")


if __name__ == "__main__":
    main()
