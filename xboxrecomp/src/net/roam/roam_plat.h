/* roam_plat.h -- free-roam multiplayer: the platform layer under the core.
 *
 * After nfsu2-web's runtime/roam_plat.h (MIT License, Copyright (c) 2026
 * th0mk), cut down to what roam_api.c needs.  Web: roam_web.c (a virtual UDP
 * endpoint on the page's /lan switch link).  Elsewhere: roam_null.c (no
 * platform yet: roam_plat_start fails).
 *
 * One endpoint per process.  A network thread receives, feeds the session and
 * runs rc_tick every 10 ms; the game thread pushes the local state and reads
 * the remote poses.  Both hold roam_plat_lock() around every session call.
 */
#ifndef ROAM_PLAT_H
#define ROAM_PLAT_H
#include "roam_core.h"

/* installs s->io.send / io.broadcast and starts the network thread on port;
 * call with the lock free.  0 on success, -1: no platform or no link */
int      roam_plat_start(rc_session *s, uint16_t port);
/* joins the network thread; call with the lock free, after rc_shutdown */
void     roam_plat_stop(void);
void     roam_plat_lock(void);
void     roam_plat_unlock(void);
int64_t  roam_plat_now_us(void);   /* monotonic */
uint32_t roam_plat_random(void);   /* nonzero */

/* Room census, for matchmaking. The relay answers a bare "MC3Q" datagram with
 * "MC3ROOMS 1 <n> <room>:<players> ..." on the same socket; that reply is kept
 * here instead of being fed to the core (it is not a roam datagram).
 *
 * request: ask the relay at ip:port (host order), returns 0 when sent;
 * best:    the fullest room with a free slot (8 players), or -1 if none;
 * rooms/is_room: read what the last answer said. */
int  roam_plat_census_request(uint32_t relay_ip, uint16_t relay_port);
int  roam_plat_census_best(int *players_out);
int  roam_plat_census_players(int room);
int  roam_plat_census_age_ms(void);        /* since the last answer, -1 none */

#endif
