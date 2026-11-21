/* roam_compat.h -- what the upstream free-roam game layer expected from the
 * project it came from, supplied by this port.
 *
 * Included (by recomp_manual.c, before roam_game.c) only while the pool port
 * is being built: --define MC3_ROAM_PORT. It is not part of a normal build. */
#ifndef ROAM_COMPAT_H
#define ROAM_COMPAT_H

#include <stdint.h>
#include <stdlib.h>

#include "kernel/kernel.h"              /* XBOX_CONTIG_SIZE, the game window */

/* windef.h and sal.h leave these as empty macros; upstream uses them as local
 * variable names (float far, int in). Nothing below needs the macros. */
#undef far
#undef near
#undef in
#undef out

/* The web page's live settings (fps60 / cars / drift / career). Nothing to
 * drive them here: the env values win. */
enum { MC3_LIVE_60FPS, MC3_LIVE_CARS, MC3_LIVE_DRIFT, MC3_LIVE_CAREER, MC3_LIVE_N };
static inline int32_t mc3_live(int i) { (void)i; return 0; }

/* Guest RAM test, the upstream code's shape. */
static inline int online_ram(uint32_t va)
{
    return (va >= 0x00010000u && va < 0x04000000u) ||
           (va >= 0x80000000u && va < 0x84000000u);
}

/* online_ram for a block of `n` bytes (headers and pools use this). */
static inline int online_ram_n(uint32_t va, uint32_t n)
{
    return online_ram(va) && n <= 0x04000000u - va;
}

/* Their Online session flag. Free roam treats the session as Online while it
 * runs, so the pool takes the remote path and the career paths stay out. */
static int s_online = 1;

/* roam_challenge.c (street and group races, name tags) is not ported yet:
 * our state carries the race bit only, no challenge bits. */
static unsigned rch_bits(void) { return 0; }

#endif
