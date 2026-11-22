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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void roam_net_start_now(void);

static void roam_net_boot(void)
{
    static int done;
    const char *e;
    if (done)
        return;
    done = 1;
    e = getenv("MC3_ROAM");
    if (!e || e[0] != '1')
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
#ifdef _WIN32
        _putenv_s("MC3_ROAM_HUB", "0");
#else
        setenv("MC3_ROAM_HUB", "0", 0);
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
    fprintf(stderr, "[ROAM] transport up as \"%s\"; peers' cars are built at the cruise"
            " (MC3_ROAM=1)\n", e && *e ? e : "Player");
}
