/* roam_net.c -- MC3_ROAM=1: bring the free-roam transport up.
 *
 * Only the transport for now: the game layer (the remote-car pool) is the
 * next piece. With no hub (MC3_ROAM_HUB=0, the default here) the core
 * discovers peers by broadcast and links up to 8 players -- a LAN of two or
 * more copies, or the same across a VPN with RECOMP_NET_PEERS.
 *
 * Off unless MC3_ROAM=1, so the default build is unchanged. */

#include "net/roam/roam_api.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
    /* The name every other player sees, from the same sources roam_game.c
     * uses: MC3_ROAM_NAME, then MC3_PLAYER_NAME. This used to pass "MC3"
     * unconditionally, and because roam_begin() only starts a session when
     * none is running, that placeholder won: every player in a city was
     * called "MC3", in the player list and over their car. The transport has
     * to be up early, so the name has to be chosen here. */
    e = getenv("MC3_ROAM_NAME");
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
