#!/usr/bin/env python3
"""Check the path System Link packets take through the relay.

Two halves, both without a game, a display or a second machine:

  1. the core's TUNNEL message -- encode, decode, and the callback, using two
     sessions in one process (A's send is captured and fed to B's receive);
  2. the transport itself (xnet_relay.c) -- virtual addressing, the destination
     and port filter, and the queue -- compiled against small stubs for the
     session it talks to.

The split matters: the core hands every well-formed TUNNEL to its handler,
because only the transport knows this player's virtual address. So "a packet for
someone else is ignored" is a property of the transport, and it is tested there.

Run:  python tools/test_roam_tunnel.py
"""
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

FAIL = 0

HARNESS = r"""
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#ifdef _WIN32
#include <winsock2.h>          /* before the roam headers: its `far` macro would */
#else
#include <arpa/inet.h>
#endif
#ifdef far
#undef far                     /* rewrite rc_session's `far` field (roam_api.c) */
#endif
#include "net/roam/roam_core.h"
#include "net/xnet_hle.h"

/* ---- the session the transport talks to, as the real roam_api provides it ---- */
static roam_tunnel_fn g_from;
static uint32_t g_id = 0x1234;
static uint32_t s_src, s_dst;
static uint16_t s_sp, s_dp;
static int s_len, s_sent;

int roam_tunnel_ready(uint32_t *self_id)
{
    if (self_id) *self_id = g_id;
    return 1;
}
int roam_tunnel_send(uint32_t src, uint32_t dst, uint16_t sport, uint16_t dport,
                     const void *data, int len)
{
    s_src = src; s_dst = dst; s_sp = sport; s_dp = dport; s_len = len;
    (void)data;
    s_sent++;
    return 0;
}
void roam_tunnel_from(roam_tunnel_fn cb, void *user) { (void)user; g_from = cb; }
void roam_tunnel_stats(uint32_t *tx, uint32_t *rx, uint32_t *dropped)
{
    (void)tx; (void)rx; (void)dropped;
}

/* ---- 1: the core's TUNNEL message, A -> B ---- */
static uint8_t cap[2048];
static int capn;
static void sink(void *u, uint32_t ip, uint16_t port, const uint8_t *b, int n)
{
    (void)u; (void)ip; (void)port;
    if (n > (int)sizeof cap) n = (int)sizeof cap;
    memcpy(cap, b, (size_t)n);
    capn = n;
}

static int got;
static rc_tunnel last;
static uint8_t lastdata[2048];
static void on_tunnel(void *u, const rc_tunnel *t)
{
    (void)u;
    got++;
    last = *t;
    memcpy(lastdata, t->data, t->len < (int)sizeof lastdata ? t->len : 2048);
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    rc_config ca;
    memset(&ca, 0, sizeof ca);
    ca.self_id = 0x1111; ca.room = 0;
    ca.relay = 1; ca.relay_ip = 0x0A000001u; ca.relay_port = 47210;
    rc_io ia = { 0, sink, 0, 0 };
    static rc_session A;
    rc_session_init(&A, &ca, &ia);

    rc_config cb = ca;
    cb.self_id = 0x2222;
    rc_io ib = { 0, 0, 0, 0 };
    static rc_session B;
    rc_session_init(&B, &cb, &ib);
    rc_set_tunnel(&B, on_tunnel, 0);
    int i = rc_add_peer(&B, ca.relay_ip, ca.relay_port, 1, 1000);
    if (i < 0) { printf("nopeer\n"); return 1; }
    B.peer[i].id = 0x1111;
    B.peer[i].state = RC_PS_EST;

    const char *msg = "system link packet";
    int n = (int)strlen(msg);
    printf("send %d\n", rc_send_tunnel(&A, 0x0A4D0002u, 0x0A4D0004u, 2000, 2000, msg, n));
    printf("wire %d\n", capn);
    rc_receive(&B, ca.relay_ip, ca.relay_port, cap, capn, 2000);
    printf("got %d %d %08x %08x %u %u %s\n", got, last.len, last.src, last.dst,
           last.sport, last.dport, lastdata);
    printf("bcast %d\n", rc_send_tunnel(&A, 0x0A4D0002u, 0xFFFFFFFFu, 2000, 2001, msg, n));
    rc_receive(&B, ca.relay_ip, ca.relay_port, cap, capn, 3000);
    printf("got2 %d\n", got);
    {
        static uint8_t big[RC_TUNNEL_MAX + 1];
        memset(big, 0x5A, sizeof big);
        printf("big %d\n", rc_send_tunnel(&A, 1, 2, 3, 4, big, (int)sizeof big));
        printf("max %d\n", rc_send_tunnel(&A, 1, 2, 3, 4, big, RC_TUNNEL_MAX));
    }
    {
        rc_config cc = ca;
        cc.relay = 0;
        static rc_session C;
        rc_session_init(&C, &cc, &ia);
        printf("norelay %d\n", rc_send_tunnel(&C, 1, 2, 3, 4, msg, n));
    }

    /* ---- 2: the transport ---- */
    {
        const xnet_transport *T = &xnet_transport_relay;
        uint32_t me = T->local_addr();          /* network order, as the HLE keeps it */
        intptr_t h = T->open();
        char buf[64];
        uint32_t ip = 0;
        uint16_t port = 0;
        printf("me %08x bind %d\n", (unsigned)ntohl(me), T->bind(h, 2000));
        printf("outsend %d\n", T->sendto(h, msg, 4, 0xFFFFFFFFu, 2001));
        printf("outframe %08x %08x %u %u\n", s_src, s_dst, s_sp, s_dp);

        g_from(0, 0x0A4D0009u, ntohl(me), 2001, 2000, "hi", 2);   /* for us */
        g_from(0, 0x0A4D0009u, 0x0A4D000Au, 2001, 2000, "no", 2); /* another player */
        g_from(0, 0x0A4D0009u, ntohl(me), 2001, 2002, "no", 2);   /* wrong port */
        {
            int n1 = T->recvfrom(h, buf, sizeof buf, &ip, &port);
            int n2 = T->recvfrom(h, buf, sizeof buf, &ip, &port);
            printf("recv %d %d %08x %u\n", n1, n2, (unsigned)ntohl(ip), port);
        }
        /* a broadcast frame reaches our bound port too */
        g_from(0, 0x0A4D0009u, 0xFFFFFFFFu, 2001, 2000, "hey", 3);
        printf("bcastrecv %d\n", T->recvfrom(h, buf, sizeof buf, &ip, &port));
    }
    return 0;
}
"""


def check(name, ok, extra=""):
    global FAIL
    print(("ok   " if ok else "FAIL ") + name + (f"  {extra}" if extra else ""))
    if not ok:
        FAIL += 1


def main() -> int:
    root = Path(__file__).resolve().parent.parent
    cc = os.environ.get("CC") or shutil.which("clang") or shutil.which("cc")
    if not cc:
        print("no C compiler on PATH (set CC, or run in an MSYS2 CLANG64 shell)")
        return 2

    with tempfile.TemporaryDirectory() as td:
        src = Path(td) / "t.c"
        exe = Path(td) / "t.exe"
        src.write_text(HARNESS, encoding="utf-8")
        cmd = [cc, str(src),
               str(root / "xboxrecomp" / "src" / "net" / "roam" / "roam_core.c"),
               str(root / "xboxrecomp" / "src" / "net" / "xnet_relay.c"),
               "-I", str(root / "xboxrecomp" / "src"),
               "-I", str(root / "xboxrecomp" / "src" / "net" / "roam"),
               "-o", str(exe), "-lws2_32"]
        done = subprocess.run(cmd, capture_output=True, text=True)
        if done.returncode != 0:
            print("could not build the harness:\n" + (done.stderr or done.stdout)[-3000:])
            return 2
        out = subprocess.run([str(exe)], capture_output=True, text=True)
        if out.returncode != 0 or not out.stdout.strip():
            print("the harness failed:\n" + (out.stderr or out.stdout)[-3000:])
            return 2
        L = dict(l.split(" ", 1) for l in out.stdout.strip().splitlines() if " " in l)

    # ---- the core's message ----
    check("a tunnel datagram is sent", L.get("send") == "0", L.get("send"))
    check("it fits one datagram", int(L.get("wire", "0")) > 0, L.get("wire", "") + " bytes")
    got = L.get("got", "").split()
    check("B delivered it once", got and got[0] == "1", " ".join(got[:1]))
    check("the length survives", got and int(got[1]) == 18, " ".join(got[:2]))
    check("the sender's address survives", got and got[2] == "0a4d0002", " ".join(got[:3]))
    check("the destination survives", got and got[3] == "0a4d0004", " ".join(got[:4]))
    check("the ports survive", got and got[4] == "2000" and got[5] == "2000", " ".join(got[:6]))
    check("the payload survives", " ".join(got[6:]) == "system link packet", " ".join(got[6:]))
    check("a broadcast is delivered too", L.get("got2") == "2", L.get("got2"))
    check("an oversized payload is refused", L.get("big") == "-1", L.get("big"))
    check("exactly the maximum is accepted", L.get("max") == "0", L.get("max"))
    check("no relay means no tunnel", L.get("norelay") == "-1", L.get("norelay"))

    # ---- the transport ----
    me_bind = L.get("me", "").split()
    check("the transport has a virtual address", me_bind and me_bind[0] != "00000000",
          " ".join(me_bind[:1]))
    check("and it is a 10.77 address", me_bind and me_bind[0].startswith("0a4d"),
          " ".join(me_bind[:1]))
    check("a socket binds", me_bind and me_bind[2] == "0", " ".join(me_bind))
    check("sendto reports the bytes", L.get("outsend") == "4", L.get("outsend"))
    fr = L.get("outframe", "").split()
    check("the frame carries our address as the source",
          fr and me_bind and fr[0] == me_bind[0], " ".join(fr))
    check("a broadcast destination is passed through", fr and fr[1] == "ffffffff", " ".join(fr))
    check("the guest ports are kept (not host ports)", fr and fr[2] == "2000" and fr[3] == "2001",
          " ".join(fr))
    rec = L.get("recv", "").split()
    check("a frame for us is delivered", rec and rec[0] == "2", " ".join(rec[:2]))
    check("nothing else is queued", rec and rec[1].startswith("-"), " ".join(rec[:2]))
    check("the sender's virtual address is reported", rec and rec[2] == "0a4d0009", " ".join(rec))
    check("the sender's port is reported", rec and rec[3] == "2001", " ".join(rec))
    check("a broadcast frame is delivered", L.get("bcastrecv") == "3", L.get("bcastrecv"))

    print()
    print("FAIL" if FAIL else "all checks passed")
    return 1 if FAIL else 0


if __name__ == "__main__":
    sys.exit(main())
