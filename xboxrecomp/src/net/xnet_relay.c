/* xnet_relay.c -- System Link over the free-roam relay.
 *
 * Free roam reaches the other players through the relay; a race has to reach
 * the same players the same way, because behind a relay that is the only address
 * anyone has. This transport carries XNet's datagrams as TUNNEL frames on the
 * relay session that already works (roam_core's RC_MSG_TUNNEL), so the relay
 * needs no change: it forwards any datagram from a member of the room to the
 * rest of the room, and these are just datagrams.
 *
 * The players still see a LAN. Each one is given a virtual address 10.77.a.b
 * derived from its session id (the same 10.77.x.x the web build's virtual LAN
 * uses), and a frame names both ends, so the receiving side can tell a packet
 * meant for it from one meant for someone else. A guest that broadcasts is
 * therefore broadcasting to the room, which is exactly what System Link's
 * discovery does.
 *
 * What this transport does NOT do: loop a broadcast back to the sender's own
 * sockets. A host advertises and a client queries; neither needs its own packet
 * back, and not looping keeps each inbound queue with a single writer (the
 * session's receive thread), which is what makes the queue lock-free.
 *
 * Threading: guest calls (socket, sendto, recvfrom) run under the guest lock,
 * one at a time; inbound frames arrive on the roam session's receive thread.
 * Each socket's queue is therefore single-producer / single-consumer.
 */
#ifdef _WIN32
#include <winsock2.h>          /* before the roam headers: its `far` macro would */
#endif
#ifdef far
#undef far                     /* rewrite rc_session's `far` field (roam_api.c) */
#endif
#include "xnet_hle.h"
#include "roam/roam_api.h"

#include <stdio.h>
#include <string.h>

#ifndef _WIN32
#include <arpa/inet.h>
#endif

#define XR_SOCKS    8
#define XR_QUEUE    64
#define XR_DGRAM    1200                  /* an XNet datagram at most */
#define BROADCAST_N 0xFFFFFFFFu

typedef struct {
    uint32_t src;                          /* the sender's virtual address */
    uint16_t sport, len;
    uint8_t  d[XR_DGRAM];
} xr_frame;

typedef struct {
    volatile int      used;
    volatile uint16_t port;                /* the guest port bound; 0: any */
    volatile int      head, tail;          /* consumer, producer */
    xr_frame q[XR_QUEUE];
} xr_sock;

static xr_sock  xr_s[XR_SOCKS];
static uint32_t xr_me;                     /* our virtual address, 0 until known */
static uint32_t xr_tx, xr_rx, xr_qfull, xr_notun;
static int      xr_reported;

/* 10.77.a.b, a in 0..127 and b even in 2..126: never the network or a broadcast
 * address, and stable for the life of the session (the id is).
 *
 * Host order here; the boundary converts. The HLE keeps addresses in network
 * order (the UDP transport passes inet_addr's value straight through), so a
 * host-order value returned as-is is read as a different address: 10.77.42.14
 * went out as 14.42.77.10 once, which is a real routable one. */
static uint32_t xr_vaddr(uint32_t id)
{
    uint32_t a = (id >> 8) & 0x7Fu;
    uint32_t b = (id & 0x7Eu) | 2u;
    return 0x0A4D0000u | (a << 8) | b;
}

/* A packet meant for everyone: the limited broadcast, our subnet's, or a
 * 0 address (some stacks send discovery there). */
static int xr_broadcast(uint32_t ip)
{
    return ip == BROADCAST_N || ip == 0 || (ip & 0xFFu) == 0xFFu;
}

static void xr_enqueue(xr_sock *s, uint32_t src, uint16_t sport, const void *d, int n)
{
    int t = s->tail;
    int next = (t + 1) % XR_QUEUE;
    if (next == s->head) {                 /* full: the oldest stays, this one goes */
        xr_qfull++;
        return;
    }
    s->q[t].src = src;
    s->q[t].sport = sport;
    s->q[t].len = (uint16_t)n;
    memcpy(s->q[t].d, d, (size_t)n);
    s->tail = next;
}

/* The session's receive thread (it holds the session lock: no roam calls here). */
static void xr_on_tunnel(void *user, uint32_t src, uint32_t dst, uint16_t sport,
                         uint16_t dport, const void *d, int n)
{
    int i;
    (void)user;
    if (n <= 0 || n > XR_DGRAM) return;
    if (!xr_broadcast(dst) && dst != xr_me) return;      /* for another player */
    for (i = 0; i < XR_SOCKS; i++) {
        xr_sock *s = &xr_s[i];
        if (!s->used) continue;
        if (s->port && s->port != dport) continue;       /* not this socket's port */
        xr_enqueue(s, src, sport, d, n);
    }
}

/* Called once by xnet_startup: a transport sets itself up here. */
static uint32_t xr_local_addr(void)
{
    uint32_t id = 0;
    if (!roam_tunnel_ready(&id))
        return 0;                          /* no relay: report no network */
    xr_me = xr_vaddr(id);
    roam_tunnel_from(xr_on_tunnel, 0);
    if (!xr_reported) {
        xr_reported = 1;
        fprintf(stderr, "[XNET] relay: virtual LAN %u.%u.%u.%u (System Link through the free-roam relay)\n",
                (unsigned)(xr_me >> 24) & 0xFFu, (unsigned)(xr_me >> 16) & 0xFFu,
                (unsigned)(xr_me >> 8) & 0xFFu, (unsigned)xr_me & 0xFFu);
    }
    return htonl(xr_me);                   /* the HLE works in network order */
}

static intptr_t xr_open(void)
{
    int i;
    for (i = 0; i < XR_SOCKS; i++) {
        if (xr_s[i].used) continue;
        xr_s[i].port = 0;
        xr_s[i].head = xr_s[i].tail = 0;
        xr_s[i].used = 1;
        return (intptr_t)i;
    }
    return -XNET_WSAEMFILE;
}

static int xr_bind(intptr_t h, uint16_t port)
{
    if (h < 0 || h >= XR_SOCKS || !xr_s[h].used)
        return -XNET_WSAENOTSOCK;
    xr_s[h].head = xr_s[h].tail = 0;
    xr_s[h].port = port;
    return 0;
}

static int xr_sendto(intptr_t h, const void *p, int n, uint32_t ip, uint16_t port)
{
    xr_sock *s;
    if (h < 0 || h >= XR_SOCKS || !xr_s[h].used)
        return -XNET_WSAENOTSOCK;
    if (n <= 0 || n > XR_DGRAM)
        return -XNET_WSAEMSGSIZE;
    s = &xr_s[h];
    if (roam_tunnel_send(xr_me, ntohl(ip), s->port, port, p, n) != 0) {
        xr_notun++;
        return -XNET_WSAENETDOWN;
    }
    xr_tx++;
    return n;
}

static int xr_recvfrom(intptr_t h, void *p, int n, uint32_t *ip, uint16_t *port)
{
    xr_sock *s;
    int r;
    if (h < 0 || h >= XR_SOCKS || !xr_s[h].used)
        return -XNET_WSAENOTSOCK;
    s = &xr_s[h];
    if (s->head == s->tail)
        return -XNET_WSAEWOULDBLOCK;
    r = s->q[s->head].len < n ? s->q[s->head].len : n;
    memcpy(p, s->q[s->head].d, (size_t)r);
    *ip = htonl(s->q[s->head].src);
    *port = s->q[s->head].sport;
    s->head = (s->head + 1) % XR_QUEUE;
    xr_rx++;
    return r;
}

static void xr_close(intptr_t h)
{
    if (h < 0 || h >= XR_SOCKS)
        return;
    xr_s[h].used = 0;
    xr_s[h].head = xr_s[h].tail = 0;
}

const xnet_transport xnet_transport_relay = {
    "relay", xr_local_addr, xr_open, xr_bind, xr_sendto, xr_recvfrom, xr_close,
};
