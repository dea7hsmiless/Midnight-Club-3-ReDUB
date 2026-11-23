/* roam_api.c -- free-roam multiplayer: the game layer's API (roam_api.h) over
 * the core (roam_core.h) and the platform (roam_plat.h).  After nfsu2-web's
 * roam_game.c session handling (MIT License, Copyright (c) 2026 th0mk),
 * without its NFSU2 game layer.
 *
 * Environment: MC3_ROAM_K (links wanted from the hub, default 7),
 * MC3_ROAM_DELAY (render delay ms, default 100), MC3_ROAM_HUB=0 (no hub:
 * discovery by broadcast and gossip, groups of up to 8). */
/* Name resolution for MC3_ROAM_HUB_IP / MC3_ROAM_RELAY_IP.
 *
 * Winsock's legacy `far` macro rewrites every `far` identifier -- including the
 * `far` field of rc_session in roam_core.h -- into `0x3535`, so it has to go
 * before any of the roam headers. Windows headers must also come first, and
 * winsock2.h before windows.h. */
#ifdef _WIN32
#include <winsock2.h>
#endif
#ifdef far
#undef far
#endif
#include "roam_plat.h"
#include "roam_addr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef _WIN32
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#define EXTRAP_CAP_US 500000        /* past the newest state a car goes on this long, then holds */

static rc_session g_s;
static int        g_on;
static int64_t    g_last_eval;
static float      g_me[3];
static int        g_k = ROAM_K_DEFAULT;
static uint32_t   g_room;
/* MC3_ROAM_ROOM, shifted up: the game sets the room from the city it loads
 * (roam_set_room(city)), so an explicit room is a *group* the city is packed
 * into, not a value the game may overwrite. Without this, two players who
 * picked room 1 were put into different rooms the moment one of them reached
 * the cruise ("Big left: other room"). */
static uint32_t   g_room_base;

/* ── tunnelling another protocol (System Link) through the relay ─────────────
 * The handler and its user are declared here because roam_start registers them
 * when a session is made; the rest is with the API below. */
static roam_tunnel_fn g_tun_cb;
static void          *g_tun_user;
static void tunnel_adapter(void *user, const rc_tunnel *t);

static int env_int(const char *k, int def, int lo, int hi){
    const char *v = getenv(k);
    if (!v || !*v) return def;
    int x = atoi(v);
    return x < lo ? lo : x > hi ? hi : x;
}

/* A server address from the environment: a dotted quad or a host name.
 *
 * A name is what people are given ("server.dea7hsmiles.com"), so refusing one
 * would mean every player has to look up an IP that can change under them. The
 * result is the host-order IPv4 address the core stores, or 0 if the name does
 * not resolve. Resolved once, at start: a relay's address is not expected to
 * move mid-session, and a failed lookup then falls back to the caller's
 * default instead of silently pointing at nothing. */
/* 1 once an address has arrived encoded: it is then kept out of the log too.
 * An address hidden in the file and printed in plain text two lines later has
 * not been hidden from anyone who reads the log (see roam_addr.h). */
static int g_addr_private;

static uint32_t resolve_host(const char *host, const char *what)
{
    unsigned a, b, cc, d;
    struct hostent *he;
    char plain[ROAM_ADDR_MAX + 1];
    int dec;
    if (!host || !*host)
        return 0;
    /* An rd1: value, for a player who would rather their settings file did not
     * name the server in plain sight. Malformed must be reported, not treated
     * as a host name: that would fail as "does not resolve". */
    dec = roam_addr_decode(host, plain, sizeof plain);
    if (dec < 0) {
        fprintf(stderr, "[roam] %s is not a valid encoded address (a mistyped "
                        "character?); run tools/redub_addr.py to make one\n", what);
        return 0;
    }
    if (dec > 0) {
        host = plain;
        g_addr_private = 1;
    }
    if (sscanf(host, "%u.%u.%u.%u", &a, &b, &cc, &d) == 4 &&
        a < 256u && b < 256u && cc < 256u && d < 256u)
        return (a << 24) | (b << 16) | (cc << 8) | d;
    he = gethostbyname(host);
    if (he && he->h_addrtype == AF_INET && he->h_length == 4 && he->h_addr_list[0]) {
        uint32_t ip;
        memcpy(&ip, he->h_addr_list[0], 4);
        return ntohl(ip);
    }
    fprintf(stderr, "[roam] %s=%s does not resolve: using the built-in address\n", what, host);
    return 0;
}
/* The dotted quad for a log line, or a mask when the address arrived encoded.
 * MC3_ROAM_SHOW_ADDR=1 prints it anyway, because a wrong address has to be
 * seeable to be found -- the mask is the default, not a rule. */
static const char *addr_text(uint32_t ip, char *buf, size_t cap)
{
    const char *show = getenv("MC3_ROAM_SHOW_ADDR");
    if (g_addr_private && !(show && show[0] == '1'))
        snprintf(buf, cap, "**.**.**.**");
    else
        snprintf(buf, cap, "%u.%u.%u.%u",
                 (unsigned)(ip >> 24) & 0xFFu, (unsigned)(ip >> 16) & 0xFFu,
                 (unsigned)(ip >> 8) & 0xFFu, (unsigned)ip & 0xFFu);
    return buf;
}

/* A server address implies wanting the server.
 *
 * roam_net.c defaults MC3_ROAM_HUB to 0 when it is unset (a LAN mesh needs no
 * hub), so a player who set only MC3_ROAM_HUB_IP -- which is exactly what the
 * setup guide says to do -- had the address read and then thrown away: the
 * transport came up with no hub, no relay, and no line in the log saying why.
 * It cost a test run to find. Setting either address is an unambiguous request
 * to use one, so it wins over the default.
 *
 * Note this has to gate the block itself: the default is already in the
 * environment by the time we get here, so "is the variable set" cannot tell a
 * real MC3_ROAM_HUB=0 from the port's own default. */
static int server_address_given(void)
{
    const char *h = getenv("MC3_ROAM_HUB_IP");
    const char *r = getenv("MC3_ROAM_RELAY_IP");
    const char *j = getenv("MC3_ROAM_JOIN");
    return (h && *h) || (r && *r) || (j && *j);
}

/* MC3_ROAM_JOIN=<server>: one line meaning "play online on this server".
 *
 * It is the same request as MC3_ROAM_HUB_IP plus HUB=1 plus RELAY=1, and it
 * exists because that is three chances to leave one out and get a client that
 * looks exactly like a server that is down. A player who named a server has
 * already said what they want, so this is read before any of those and the
 * individual keys still win where they disagree with it on purpose. */
static const char *join_addr(void)
{
    const char *j = getenv("MC3_ROAM_JOIN");
    return (j && *j) ? j : NULL;
}

static void on_event(void *user, int ev, int peer, const char *text){
    (void)user; (void)ev; (void)peer;
    fprintf(stderr, "[roam] %s\n", text);
}

int roam_start(const char *name, uint32_t vip){
    (void)vip;   /* the switch puts our address on every frame; the core never needs it */
    if (g_on) return -1;
    rc_config c;
    memset(&c, 0, sizeof c);
    c.self_id = roam_plat_random();
    c.port = ROAM_PORT;
    snprintf(c.name, sizeof c.name, "%s", name ? name : "");
    c.platform = RC_PLAT_WEB;
    c.discovery = 1;                 /* only while no hub answers */
    c.max_players = 8;               /* without a hub; the hub's lists lift it */
    c.rate_hz = 30;                  /* 20 -> 30: a driven car's pose every 33 ms */
    c.snap_states = 1;               /* the WebSocket loses nothing (nfsu2-web: one state per SNAP) */
    /* A server address implies wanting the server.
     *
     * roam_net.c defaults MC3_ROAM_HUB to 0 when it is unset (a LAN mesh needs
     * no hub), so a player who set only MC3_ROAM_HUB_IP -- which is exactly
     * what the setup guide says to do -- had the address read and then thrown
     * away: the transport came up with no hub, no relay, and no line in the
     * log saying why. It cost a test run to find. Setting either address is an
     * unambiguous request to use one, so it wins over the default. */
    if (env_int("MC3_ROAM_HUB", 1, 0, 1) || server_address_given()) {
        /* The server's address, overridable so one can be run anywhere: without
         * this the only hub is the compile-time ROAM_HUB_VIP (10.77.0.253),
         * which means nobody can test a hub without assigning that address to
         * an interface. MC3_ROAM_HUB_IP takes a dotted quad or a host name (a
         * name is what a host actually hands out; see resolve_host), and
         * MC3_ROAM_HUB_PORT a port. */
        const char *hip = getenv("MC3_ROAM_HUB_IP");
        c.hub_ip = ROAM_HUB_VIP;
        c.hub_port = ROAM_PORT;
        if (!hip || !*hip)
            hip = join_addr();           /* MC3_ROAM_JOIN is the same address */
        if (hip && *hip) {
            uint32_t ip = resolve_host(hip, "MC3_ROAM_HUB_IP");
            if (ip)
                c.hub_ip = ip;
        }
        c.hub_port = (uint16_t)env_int("MC3_ROAM_HUB_PORT", c.hub_port, 1, 65535);
        {
            char ab[32];
            fprintf(stderr, "[roam] hub %s:%u\n",
                    addr_text(c.hub_ip, ab, sizeof ab), (unsigned)c.hub_port);
        }
    }
    g_k = env_int("MC3_ROAM_K", g_k, 1, ROAM_K_MAX);
    c.links = g_k;
    /* MC3_ROAM_ROOM: which room to join on the server (0 = the default room).
     * Kept as a group so the city the game later loads is packed under it. */
    {
        const char *r = getenv("MC3_ROAM_ROOM");
        if (r && *r) {
            g_room_base = ((uint32_t)strtoul(r, NULL, 0) & 0xFFFFu) << 16;
            g_room = g_room_base;
        }
    }
    c.room = g_room;
    /* MC3_ROAM_RELAY=1: the server forwards between players, so two people
     * behind different NATs can meet without a port forward. The relay uses the
     * hub's address unless MC3_ROAM_RELAY_IP/PORT say otherwise.
     *
     * MC3_ROAM_JOIN turns this on by itself: a named server on a public address
     * is exactly the case a relay exists for, and the alternative -- a client
     * that quietly tries to reach every peer directly -- is indistinguishable
     * from a server that is down. An explicit MC3_ROAM_RELAY=0 still wins. */
    if (env_int("MC3_ROAM_RELAY", join_addr() ? 1 : 0, 0, 1)) {
        const char *rip = getenv("MC3_ROAM_RELAY_IP");
        c.relay = 1;
        c.relay_ip = c.hub_ip;
        c.relay_port = c.hub_port;
        if (rip && *rip) {
            uint32_t ip = resolve_host(rip, "MC3_ROAM_RELAY_IP");
            if (ip)
                c.relay_ip = ip;
        }
        c.relay_port = (uint16_t)env_int("MC3_ROAM_RELAY_PORT", c.relay_port, 1, 65535);
        if (!c.relay_ip || !c.relay_port) {
            fprintf(stderr, "[roam] relay asked for but no address: set MC3_ROAM_RELAY_IP/"
                            "MC3_ROAM_RELAY_PORT (or the hub's)\n");
            c.relay = 0;
        } else {
            char ab[32];
            fprintf(stderr, "[roam] relay %s:%u (group %u): datagrams go through it\n",
                    addr_text(c.relay_ip, ab, sizeof ab), (unsigned)c.relay_port,
                    (unsigned)(g_room_base >> 16));
        }
    }
    rc_io io = { 0, 0, 0, on_event };
    rc_session_init(&g_s, &c, &io);
    /* a tunnelling handler set before the session existed (the XNet transport
     * installs its queue at start) is registered now */
    if (g_tun_cb) rc_set_tunnel(&g_s, tunnel_adapter, 0);
    if (roam_plat_start(&g_s, ROAM_PORT) != 0) return -1;
    g_on = 1; g_last_eval = 0;
    fprintf(stderr, "[roam] started as \"%s\" (id %08x, %d links wanted)\n", g_s.conf.name, (unsigned)c.self_id, g_k);
    return 0;
}
void roam_stop(void){
    if (!g_on) return;
    roam_plat_lock(); rc_shutdown(&g_s, 2); roam_plat_unlock();
    roam_plat_stop();
    g_on = 0;
}
int roam_running(void){ return g_on; }
void roam_set_room(uint32_t room){
    /* The game passes the city. With an explicit MC3_ROAM_ROOM the city is
     * packed under it, so the group survives the cruise load and two players
     * who chose the same group stay together whatever city each loads. */
    if (g_room_base)
        room = g_room_base | (room & 0xFFFFu);
    if (room == g_room) return;
    g_room = room;
    if (!g_on) return;
    roam_plat_lock(); rc_set_room(&g_s, room); roam_plat_unlock();
}
void roam_set_k(int k){
    g_k = k < 1 ? 1 : k > ROAM_K_MAX ? ROAM_K_MAX : k;
    if (!g_on) return;
    roam_plat_lock(); rc_set_links(&g_s, g_k); roam_plat_unlock();
}
void roam_set_status(uint8_t st){
    if (!g_on) return;
    roam_plat_lock(); rc_set_status(&g_s, st); roam_plat_unlock();
}
void roam_set_local(const rc_state *st){
    if (!g_on || !st) return;
    roam_plat_lock();
    int64_t now = roam_plat_now_us();
    rc_state s = *st;
    if (!s.t_us) s.t_us = (uint32_t)now ? (uint32_t)now : 1u;
    memcpy(g_me, s.p, sizeof g_me);
    rc_push_state(&g_s, &s, now);
    roam_plat_unlock();
}
void roam_teleport(void){
    if (!g_on) return;
    roam_plat_lock(); rc_bump_epoch(&g_s); roam_plat_unlock();
}
int roam_peers(roam_peer *out, int max){
    if (!g_on || !out || max <= 0) return 0;
    int delay_ms = env_int("MC3_ROAM_DELAY", 100, 0, 1000), n = 0;
    roam_plat_lock();
    int64_t now = roam_plat_now_us(), fdt = g_last_eval ? now - g_last_eval : 0;
    g_last_eval = now;
    roam_peer tmp[RC_MAX_PEERS];
    for (int i = 0; i < RC_TABLE && n < RC_MAX_PEERS; i++){
        rc_peer *p = &g_s.peer[i];
        rc_pose o;
        if (p->state != RC_PS_EST || p->lost) continue;
        if (!rc_interp_eval(&p->mo, now, fdt, (int64_t)delay_ms * 1000, EXTRAP_CAP_US, &o)) continue;
        roam_peer *r = &tmp[n++];
        memset(r, 0, sizeof *r);
        r->id = p->id; memcpy(r->name, p->name, sizeof r->name);
        r->status = p->status; r->mode = o.mode;
        memcpy(r->p, o.p, sizeof r->p); memcpy(r->q, o.q, sizeof r->q); memcpy(r->v, o.v, sizeof r->v); memcpy(r->w, o.w, sizeof r->w);
        r->steer = o.steer; r->throttle = o.throttle; r->brake = o.brake; r->rpm = o.rpm; r->speed = o.speed;
        r->gear = o.gear; r->bits = o.bits; r->ext_len = o.ext_len; memcpy(r->ext, o.ext, sizeof r->ext);
        r->cfg_gen = p->cfg_len ? p->cfg_gen : 0;
        float dx = o.p[0] - g_me[0], dy = o.p[1] - g_me[1], dz = o.p[2] - g_me[2];
        r->dist = rc_sqrtf(dx * dx + dy * dy + dz * dz);
    }
    roam_plat_unlock();
    /* nearest first (insertion sort: 64 at most) */
    for (int i = 1; i < n; i++){
        roam_peer t = tmp[i]; int j = i;
        while (j > 0 && tmp[j - 1].dist > t.dist){ tmp[j] = tmp[j - 1]; j--; }
        tmp[j] = t;
    }
    if (n > max) n = max;
    memcpy(out, tmp, sizeof *out * (size_t)n);
    return n;
}
void roam_set_cfg(const void *blob, int len){
    if (!g_on || !blob || len <= 0 || len > ROAM_CFG_MAX) return;
    roam_plat_lock(); rc_set_cfg(&g_s, (const uint8_t *)blob, len, roam_plat_now_us()); roam_plat_unlock();
}
int roam_far(roam_far_peer *out, int max){
    if (!g_on || !out || max <= 0) return 0;
    int n = 0;
    roam_plat_lock();
    if (g_s.far_us && roam_plat_now_us() - g_s.far_us < 6000000){
        for (int i = 0; i < g_s.nfar && n < max; i++){
            const roam_far_peer *e = &g_s.far[i];
            int car = 0;   /* linked meanwhile: roam_peers shows it as a car (once a snapshot came) */
            for (int k = 0; k < RC_TABLE && !car; k++)
                car = g_s.peer[k].state == RC_PS_EST && !g_s.peer[k].lost && g_s.peer[k].mo.n > 0 && g_s.peer[k].id == e->id;
            if (car) continue;
            out[n] = *e;
            float dx = e->p[0] - g_me[0], dy = e->p[1] - g_me[1], dz = e->p[2] - g_me[2];
            out[n++].dist = rc_sqrtf(dx * dx + dy * dy + dz * dz);
        }
    }
    roam_plat_unlock();
    return n;
}
int roam_peer_cfg(uint32_t id, void *out, int cap){
    int r = -1;
    if (!g_on || !id) return -1;
    roam_plat_lock();
    for (int i = 0; i < RC_TABLE; i++){
        rc_peer *p = &g_s.peer[i];
        if (p->state != RC_PS_EST || p->id != id) continue;
        r = p->cfg_len < cap ? p->cfg_len : cap;
        if (r > 0 && out) memcpy(out, p->cfg, (size_t)r);
        if (r < 0) r = 0;
        break;
    }
    roam_plat_unlock();
    return r;
}
int roam_send_msg(uint32_t to, const void *data, int len){
    if (!g_on || !data) return -1;
    roam_plat_lock(); int r = rc_send_game(&g_s, to, (const uint8_t *)data, len); roam_plat_unlock();
    return r;
}
int roam_recv_msg(uint32_t *from, void *out, int cap){
    if (!g_on || !out || cap <= 0) return 0;
    roam_plat_lock(); int r = rc_recv_game(&g_s, from, (uint8_t *)out, cap); roam_plat_unlock();
    return r;
}

/* ── tunnelling another protocol (System Link) through the relay ───────────── */

static void tunnel_adapter(void *user, const rc_tunnel *t){
    (void)user;
    if (g_tun_cb) g_tun_cb(g_tun_user, t->src, t->dst, t->sport, t->dport, t->data, t->len);
}

int roam_tunnel_ready(uint32_t *self_id){
    if (!g_on) return 0;
    roam_plat_lock();
    int ok = g_s.conf.relay && g_s.conf.relay_ip && g_s.conf.relay_port;
    if (ok && self_id) *self_id = g_s.conf.self_id;
    roam_plat_unlock();
    return ok;
}

int roam_tunnel_send(uint32_t src, uint32_t dst, uint16_t sport, uint16_t dport,
                     const void *data, int len){
    if (!g_on || !data || len <= 0) return -1;
    roam_plat_lock();
    int r = rc_send_tunnel(&g_s, src, dst, sport, dport, data, len);
    roam_plat_unlock();
    return r;
}

void roam_tunnel_from(roam_tunnel_fn cb, void *user){
    g_tun_cb = cb;
    g_tun_user = user;
    if (g_on){
        roam_plat_lock();
        rc_set_tunnel(&g_s, cb ? tunnel_adapter : 0, 0);
        roam_plat_unlock();
    }
}

void roam_tunnel_stats(uint32_t *tx, uint32_t *rx, uint32_t *dropped){
    if (tx) *tx = g_s.tx_tunnel;
    if (rx) *rx = g_s.rx_tunnel;
    if (dropped) *dropped = g_s.rx_tunnel_drop;
}
/* Our own session id (nonzero while running, 0 when not).  The game layers
 * need it to elect one owner deterministically - e.g. "the lowest id in the
 * room streams the ambient population" - without a separate handshake. */
uint32_t roam_self_id(void){
    if (!g_on) return 0;
    return g_s.conf.self_id;
}
int64_t roam_now_us(void){
    return roam_plat_now_us();
}
