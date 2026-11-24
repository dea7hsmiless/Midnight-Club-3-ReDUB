/* roam_plat.c -- free-roam multiplayer: the native UDP platform.
 *
 * The core (roam_core.c) is transport-agnostic and already supports hubless
 * discovery for up to 8 players (MC3_ROAM_HUB=0): it broadcasts HELLO and
 * learns peers from each other. This file is the endpoint it expects: one
 * UDP socket on ROAM_PORT, a receive thread that feeds the session and ticks
 * it every 10 ms, and the monotonic clock and random id the API asks for.
 *
 * Two copies on one machine: bind with RECOMP_NET_ADDR (127.0.0.1 and
 * 127.0.0.2) and list the other in RECOMP_NET_PEERS, the same way the XNet
 * transport does. On a LAN, neither is needed: HELLOs go to 255.255.255.255.
 *
 * The core and API are from mc3-web by Leandro Dettmer (MIT, commit
 * 7a64713), in turn from nfsu2-web by th0mk (MIT). */

#include "roam_plat.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The room census (matchmaking) is parsed before the core sees a datagram. */
static int census_take(const uint8_t *buf, int n, uint32_t from_ip,
                       uint16_t from_port);
static uint32_t s_census_ip;            /* who may answer, from the request */
static uint16_t s_census_port;

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET rsock_t;
#define RSOCK_BAD INVALID_SOCKET
static CRITICAL_SECTION g_lock;
static HANDLE g_thread;
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
typedef int rsock_t;
#define RSOCK_BAD (-1)
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_t g_thread;
#endif

#define ROAM_DGRAM_MAX 2048
#define ROAM_PEERS_MAX 16

static rsock_t     g_sock = RSOCK_BAD;
static volatile int g_run;
static rc_session *g_s;
static uint32_t    g_peers[ROAM_PEERS_MAX];
static int         g_npeers;
static int         g_lock_init;

static int64_t plat_now_us(void)
{
#ifdef _WIN32
    static LARGE_INTEGER freq;
    LARGE_INTEGER c;
    if (!freq.QuadPart)
        QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&c);
    return (int64_t)((double)c.QuadPart * 1000000.0 / (double)freq.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
#endif
}

int64_t roam_plat_now_us(void) { return plat_now_us(); }

uint32_t roam_plat_random(void)
{
    static uint64_t x;
    if (!x) {
        x = (uint64_t)plat_now_us() ^ ((uint64_t)(uintptr_t)&x << 17) ^ 0x9E3779B97F4A7C15ull;
#ifdef _WIN32
        x ^= (uint64_t)GetCurrentProcessId() << 7;
#else
        x ^= (uint64_t)getpid() << 7;
#endif
    }
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    return (uint32_t)x | 1u;
}

void roam_plat_lock(void)
{
    if (!g_lock_init)
        return;
#ifdef _WIN32
    EnterCriticalSection(&g_lock);
#else
    pthread_mutex_lock(&g_lock);
#endif
}

void roam_plat_unlock(void)
{
    if (!g_lock_init)
        return;
#ifdef _WIN32
    LeaveCriticalSection(&g_lock);
#else
    pthread_mutex_unlock(&g_lock);
#endif
}

static void plat_send(void *user, uint32_t ip, uint16_t port, const uint8_t *buf, int len)
{
    struct sockaddr_in a;
    (void)user;
    if (g_sock == RSOCK_BAD || len <= 0)
        return;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(ip);          /* the core passes host order */
    sendto(g_sock, (const char *)buf, len, 0, (struct sockaddr *)&a, sizeof a);
}

/* A broadcast that a LAN receives; on one machine (or a VPN that drops
 * broadcasts) each RECOMP_NET_PEERS address gets a copy instead. */
static void plat_broadcast(void *user, uint16_t port, const uint8_t *buf, int len)
{
    int i;
    plat_send(user, 0xFFFFFFFFu, port, buf, len);
    for (i = 0; i < g_npeers; i++)
        plat_send(user, g_peers[i], port, buf, len);
}

static void plat_receive_all(void)
{
    uint8_t buf[ROAM_DGRAM_MAX];
    /* Bounded: under a flood the socket may never be empty, and without a
     * budget the loop would never return to rc_tick -- no HELLOs, everyone
     * times out, one core pinned. */
    for (int budget = 64; budget > 0; budget--) {
        struct sockaddr_in from;
        int n;
        socklen_t fl = (socklen_t)sizeof from;
        n = (int)recvfrom(g_sock, (char *)buf, sizeof buf, 0, (struct sockaddr *)&from, &fl);
        if (n <= 0)
            return;
        /* A room census answer is text, not a roam datagram: the core would
         * drop it as RC_E_MAGIC, so take it here. */
        if (census_take(buf, n, ntohl(from.sin_addr.s_addr), ntohs(from.sin_port)))
            continue;
        roam_plat_lock();
        if (g_run && g_s)
            rc_receive(g_s, ntohl(from.sin_addr.s_addr), ntohs(from.sin_port), buf, n, plat_now_us());
        roam_plat_unlock();
    }
}

static void plat_loop(void)
{
    while (g_run) {
        fd_set rfds;
        struct timeval tv;
        int r;
        FD_ZERO(&rfds);
        if (g_sock != RSOCK_BAD)
            FD_SET(g_sock, &rfds);
        tv.tv_sec = 0;
        tv.tv_usec = 10000;                 /* 10 ms: the core's tick */
        r = select((int)g_sock + 1, &rfds, NULL, NULL, &tv);
        if (r > 0 && g_sock != RSOCK_BAD && FD_ISSET(g_sock, &rfds))
            plat_receive_all();
        roam_plat_lock();
        if (g_run && g_s)
            rc_tick(g_s, plat_now_us());
        roam_plat_unlock();
    }
}

#ifdef _WIN32
static DWORD WINAPI plat_thread(LPVOID p)
{
    (void)p;
    plat_loop();
    return 0;
}
#else
static void *plat_thread(void *p)
{
    (void)p;
    plat_loop();
    return NULL;
}
#endif

static int parse_peers(void)
{
    const char *e = getenv("RECOMP_NET_PEERS");
    g_npeers = 0;
    while (e && *e && g_npeers < ROAM_PEERS_MAX) {
        char ip[64];
        size_t n = strcspn(e, ",");
        struct in_addr a;
        if (n && n < sizeof ip) {
            memcpy(ip, e, n);
            ip[n] = 0;
            if (inet_pton(AF_INET, ip, &a) == 1 && a.s_addr != 0xFFFFFFFFu)
                g_peers[g_npeers++] = ntohl(a.s_addr);
        }
        e += n + (e[n] == ',');
    }
    return g_npeers;
}

int roam_plat_start(rc_session *s, uint16_t port)
{
    struct sockaddr_in a;
    const char *addr;
    int one = 1;

    if (g_sock != RSOCK_BAD)
        return -1;
#ifdef _WIN32
    {
        WSADATA w;
        if (WSAStartup(MAKEWORD(2, 2), &w) != 0)
            return -1;
    }
#else
    if (!g_lock_init) {
        pthread_mutex_init(&g_lock, NULL);
    }
#endif
#ifdef _WIN32
    if (!g_lock_init)
        InitializeCriticalSection(&g_lock);
#endif
    g_lock_init = 1;

    g_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (g_sock == RSOCK_BAD) {
        g_lock_init = 0;
        return -1;
    }
#ifdef _WIN32
    setsockopt(g_sock, SOL_SOCKET, SO_REUSEADDR, (const char *)&one, sizeof one);
#else
    setsockopt(g_sock, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
#endif
    setsockopt(g_sock, SOL_SOCKET, SO_BROADCAST, (const char *)&one, sizeof one);

    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    /* Bind the address this copy is told to use, so two instances on one
     * machine can share the port. A LAN leaves it unset. */
    addr = getenv("RECOMP_NET_ADDR");
    if (addr && *addr) {
        struct in_addr ia;
        if (inet_pton(AF_INET, addr, &ia) == 1)
            a.sin_addr.s_addr = ia.s_addr;
    }
    if (bind(g_sock, (struct sockaddr *)&a, sizeof a) != 0) {
#ifdef _WIN32
        closesocket(g_sock);
#else
        close(g_sock);
#endif
        g_sock = RSOCK_BAD;
        g_lock_init = 0;
        return -1;
    }

    /* Non-blocking. plat_receive_all() drains with recvfrom() after one
     * select(), so a blocking socket parks this whole thread on the second call
     * when the queue is empty -- and while it is parked it never returns to
     * rc_tick(): no HELLOs, no peer timeouts, no pings. A relay only forwards,
     * so a quiet room sends nothing back; the client then goes silent and the
     * relay reaps a player who is still in the game. That is the "0 rooms after
     * a minute, fixed by restarting" report. */
#ifdef _WIN32
    {
        u_long nb = 1;
        ioctlsocket(g_sock, FIONBIO, &nb);
    }
#else
    {
        int fl = fcntl(g_sock, F_GETFL, 0);
        if (fl >= 0)
            fcntl(g_sock, F_SETFL, fl | O_NONBLOCK);
    }
#endif

    parse_peers();
    if (s) {
        s->io.send = plat_send;
        s->io.broadcast = plat_broadcast;
    }
    g_s = s;
    g_run = 1;
#ifdef _WIN32
    g_thread = CreateThread(NULL, 0, plat_thread, NULL, 0, NULL);
    if (!g_thread) {
        g_run = 0;
        closesocket(g_sock);
        g_sock = RSOCK_BAD;
        return -1;
    }
#else
    if (pthread_create(&g_thread, NULL, plat_thread, NULL) != 0) {
        g_run = 0;
        close(g_sock);
        g_sock = RSOCK_BAD;
        return -1;
    }
#endif
    fprintf(stderr, "[roam] udp platform on port %u, %d peer(s)%s\n",
            (unsigned)port, g_npeers, g_npeers ? " (broadcasts unicast to them)" : "");
    return 0;
}

void roam_plat_stop(void)
{
    if (g_sock == RSOCK_BAD)
        return;
    g_run = 0;
#ifdef _WIN32
    if (g_thread) {
        WaitForSingleObject(g_thread, 2000);
        CloseHandle(g_thread);
        g_thread = NULL;
    }
    closesocket(g_sock);
#else
    pthread_join(g_thread, NULL);
    close(g_sock);
#endif
    g_sock = RSOCK_BAD;
    g_s = NULL;
}

/* ── room census (matchmaking) ────────────────────────────────────────────
 *
 * The relay answers a bare "MC3Q" with one line of text on the same socket:
 *
 *     MC3ROOMS 1 <n> <room>:<players> ...
 *
 * That is not a roam datagram, so the receive path must not hand it to the
 * core; the parser below runs first and swallows it. The answer is kept as a
 * small table; the game reads it to offer "Find a room".
 */
#define CENSUS_ROOMS 32
static struct { int room, players; } s_census[CENSUS_ROOMS];
static int      s_census_n = -1;         /* -1: no answer yet */
static int64_t  s_census_at;

static int census_take(const uint8_t *buf, int n, uint32_t from_ip,
                       uint16_t from_port)
{
    /* "MC3ROOMS 1 2 3:4 7:1" -- only the shape matters, so a hand-typed
     * answer works too. */
    char text[512];
    char *p;
    int count, i, room, players;

    if (n <= 0)
        return 0;
    if (n < 8 || memcmp(buf, "MC3ROOMS", 8) != 0)
        return 0;
    /* Only the server that was asked may answer: otherwise any datagram on
     * this port can choose the room Find a Room sends the player to. */
    if (s_census_ip && (from_ip != s_census_ip || from_port != s_census_port))
        return 0;
    /* Take what fits instead of rejecting the answer: a relay listing more
     * rooms than this table holds is still useful, and dropping the whole
     * answer made Find a Room silently do nothing. The parser stops at the
     * last pair it can read, so a truncation cannot invent a room. */
    if (n >= (int)sizeof text)
        n = (int)sizeof text - 1;
    memcpy(text, buf, (size_t)n);
    text[n] = 0;
    p = text + 8;
    if (*p == ' ') p++;
    if (*p < '0' || *p > '9')
        return 0;                          /* version, ignored */
    while (*p && *p != ' ') p++;
    while (*p == ' ') p++;
    count = (int)strtol(p, &p, 10);
    if (count < 0 || count > CENSUS_ROOMS)
        count = CENSUS_ROOMS;
    for (i = 0; i < count; i++) {
        while (*p == ' ') p++;
        room = (int)strtol(p, &p, 10);
        if (*p != ':')
            break;
        players = (int)strtol(p + 1, &p, 10);
        if (room < 0 || room > 0xFFFF || players < 0 || players > 0xFF)
            break;                 /* an entry this game cannot use: stop */
        s_census[i].room = room;
        s_census[i].players = players;
    }
    if (i == 0 && count > 0)
        return 1;                  /* garbage: keep the last good answer */
    roam_plat_lock();
    s_census_n = i;
    s_census_at = plat_now_us();
    roam_plat_unlock();
    fprintf(stderr, "[roam] rooms: %d with players\n", i);
    for (int k = 0; k < i; k++)
        fprintf(stderr, "[roam]   room %d: %d/8\n",
                s_census[k].room, s_census[k].players);
    return 1;
}

int roam_plat_census_request(uint32_t relay_ip, uint16_t relay_port)
{
    static const char q[] = "MC3Q";
    struct sockaddr_in a;
    if (g_sock == RSOCK_BAD || !relay_ip || !relay_port)
        return -1;
    /* The answer to *this* question, not the previous one: an old census
     * applied immediately would send the player to a room chosen from stale
     * data (and possibly now full). */
    roam_plat_lock();
    s_census_n = -1;
    s_census_at = 0;
    s_census_ip = relay_ip;
    s_census_port = relay_port;
    roam_plat_unlock();
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(relay_port);
    a.sin_addr.s_addr = htonl(relay_ip);
    sendto(g_sock, q, (int)strlen(q), 0, (struct sockaddr *)&a, sizeof a);
    return 0;
}

/* The fullest room that still has a slot (the title's own 8-player limit),
 * preferring the room we are already in when it is empty. */
int roam_plat_census_best(int *players_out)
{
    int i, best = -1, best_n = 0, n;
    roam_plat_lock();
    n = s_census_n;
    for (i = 0; i < n; i++) {
        if (s_census[i].players >= ROAM_ROOM_PLAYERS)
            continue;
        if (s_census[i].players > best_n) {
            best = s_census[i].room;
            best_n = s_census[i].players;
        }
    }
    roam_plat_unlock();
    if (players_out)
        *players_out = best_n;
    return best;
}

int roam_plat_census_players(int room)
{
    int i, n, got = -1;
    roam_plat_lock();
    n = s_census_n;
    for (i = 0; i < n; i++)
        if (s_census[i].room == room) { got = s_census[i].players; break; }
    roam_plat_unlock();
    return got;
}

int roam_plat_census_age_ms(void)
{
    int n; int64_t at; int age;
    roam_plat_lock();
    n = s_census_n; at = s_census_at;
    roam_plat_unlock();
    if (n < 0 || at == 0)
        return -1;
    age = (int)((plat_now_us() - at) / 1000);
    return age < 0 ? 0 : age;
}
