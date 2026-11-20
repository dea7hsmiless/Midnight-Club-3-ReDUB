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
    /* No hub: broadcast discovery, up to 8 players, which is a LAN mesh and
     * exactly the nearest-K the design wants at that size. */
#ifdef _WIN32
    _putenv_s("MC3_ROAM_HUB", "0");
#else
    setenv("MC3_ROAM_HUB", "0", 0);
#endif
    if (roam_start("MC3", 0) != 0) {
        fprintf(stderr, "[ROAM] transport did not start (port %u busy, or no network)\n",
                (unsigned)ROAM_PORT);
        return;
    }
    atexit(roam_stop);
    fprintf(stderr, "[ROAM] transport up; peers' cars are built at the cruise (MC3_ROAM=1)\n");
}
