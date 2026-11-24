/* roam_net.c -- MC3_ROAM=1: the free-roam transport, and when to start it.
 *
 * With no hub (MC3_ROAM_HUB=0, the default here) the core discovers peers by
 * broadcast and links up to 8 players -- a LAN of two or more copies, or the
 * same across a VPN with RECOMP_NET_PEERS.
 *
 * Off unless MC3_ROAM=1, so the default build is unchanged.
 *
 * WHEN. The transport starts on the first frame after boot, and that is
 * where the start-up cost is. Measured on one install afterwards:
 *
 *   MC3_ROAM=0   51 s to the cruise
 *   MC3_ROAM=1   a black window at 200 % CPU for 5+ minutes, no front end
 *
 * Ruled out: the hub (nothing bound to its port), the movies (the skip is
 * honoured and the log says so), the disc, and RECOMP_NET_ADDR (MC3_ROAM=0
 * with the same 127.0.0.1 booted in 51 s and never even resolved the address).
 *
 * Moving the start to the cruise was tried and does not work: roam_begin()
 * (roam_game.c) is the only late starter and the Online path does not go
 * through it, so the cruise came up with no transport at all. The early start
 * stays. MC3_ROAM_EARLY=0 still asks for the late start, which is useful for
 * measuring the boot cost but leaves the Online path without a session.
 */

#include "net/roam/roam_api.h"
#include "net/xnet_hle.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void roam_net_start_now(void);

/* Phone defaults for online play.
 *
 * A phone in a lobby of eight has less to spend than a desktop: the same city,
 * the same draw calls, a doubled game rate at 60 fps, and every peer's car
 * inside the radius adds its own build and per-frame blend work. Two defaults
 * are lower on a phone, and both are only defaults -- a value the player set is
 * never overridden:
 *
 *   MC3_ROAM_RADIUS  how near a peer must be to get a car built here (250 m on
 *                    a desktop). The lobby is unchanged; a car that is not
 *                    within the radius is still a blip on the map, so a race
 *                    with a field spread out costs what the nearby cars cost
 *                    instead of what the whole field costs.
 *   MC3_ROAM_DT_MAX  the largest step the pose blend will take in one frame
 *                    (clamped in roam_pool.c). A hitchy phone delivers a long
 *                    frame; without a clamp every remote car is blended across
 *                    that whole step in the next frame, which deepens the
 *                    hitch. Clamping keeps a bad frame a bad frame instead of
 *                    turning it into a catch-up storm.
 *
 * Called from the host after the env file is loaded, before the game boots. */
void roam_mobile_online_defaults(int radius_m, double dt_max)
{
    char v[32];
    const char *cur;
    cur = getenv("MC3_ROAM_RADIUS");
    if (!cur || !*cur) {
        snprintf(v, sizeof v, "%d", radius_m);
#ifdef _WIN32
        _putenv_s("MC3_ROAM_RADIUS", v);
#else
        setenv("MC3_ROAM_RADIUS", v, 1);
#endif
    }
    cur = getenv("MC3_ROAM_DT_MAX");
    if (!cur || !*cur) {
        snprintf(v, sizeof v, "%.2f", dt_max);
#ifdef _WIN32
        _putenv_s("MC3_ROAM_DT_MAX", v);
#else
        setenv("MC3_ROAM_DT_MAX", v, 1);
#endif
    }
}

static void roam_net_boot(void)
{
    static int done;
    const char *e;
    const char *join;
    if (done)
        return;
    done = 1;
    /* MC3_ROAM_JOIN=<server> is one line that means "play online on this
     * server": it implies MC3_ROAM=1, a hub, and the relay. Saying it seven
     * ways in a settings file is what has been going wrong -- a missing
     * MC3_ROAM_HUB=1 or MC3_ROAM_RELAY=1 looks exactly like a server that is
     * down, and has cost a test run for each. The individual keys still work
     * and still win where they disagree with each other. */
    join = getenv("MC3_ROAM_JOIN");
    if (!join || !*join)
        join = NULL;
    e = getenv("MC3_ROAM");
    if (!(e && e[0] == '1') && !join)
        return;
    /* No hub by default: broadcast discovery, up to 8 players, which is a LAN
     * mesh and exactly the nearest-K the design wants at that size.
     *
     * Only when the player has not said otherwise. This used to set
     * MC3_ROAM_HUB=0 unconditionally, which silently overrode both
     * MC3_ROAM_HUB=1 and the hub address, so a configured hub could never be
     * used -- the client only ever reached one by falling back to broadcast
     * discovery. A default is a default; it must not outrank a setting. */
    e = getenv("MC3_ROAM_HUB");
    if (!e || !*e) {
        /* A named server needs a hub; a LAN mesh does not. */
        const char *dflt = join ? "1" : "0";
#ifdef _WIN32
        _putenv_s("MC3_ROAM_HUB", dflt);
#else
        setenv("MC3_ROAM_HUB", dflt, 0);
#endif
    }
    e = getenv("MC3_ROAM_EARLY");
    if (e && e[0] == '0')
        return;                         /* late start: only the native free roam reaches roam_begin */
    roam_net_start_now();
}

/* The transport, now. Called from here with MC3_ROAM_EARLY=1, and from
 * roam_begin() at the cruise, which is the normal path. */
void roam_net_start_now(void)
{
    const char *e = getenv("MC3_ROAM_NAME");
    if (!e || !*e)
        e = getenv("MC3_PLAYER_NAME");
    if (roam_start(e && *e ? e : "Player", 0) != 0) {
        fprintf(stderr, "[ROAM] transport did not start (port %u busy, or no network)\n",
                (unsigned)ROAM_PORT);
        return;
    }
    atexit(roam_stop);
    /* System Link goes through the relay too, when there is one: the players a
     * race can reach are the players the room has, and their real addresses are
     * the relay's. Selected here rather than at xnet_startup because the
     * session's id and relay are only known once roam_start has run -- and the
     * guest's WSAStartup (System Link) is always much later than this. */
    {
        uint32_t id = 0;
        if (roam_tunnel_ready(&id)) {
            xnet_set_transport(&xnet_transport_relay);
            fprintf(stderr, "[ROAM] System Link will go through the relay"
                            " (virtual LAN, MC3_NETPLAY=1 to use it)\n");
        }
    }
    fprintf(stderr, "[ROAM] transport up as \"%s\"; peers' cars are built at the cruise"
            " (MC3_ROAM=1)\n", e && *e ? e : "Player");
}
