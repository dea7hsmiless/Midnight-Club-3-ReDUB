/* roam_caps.c -- MC3_ROAM=1: the engine capacity free roam needs.
 *
 * The vehicle registry (0x005638E4) is made once at boot with capacity 10:
 * eight player cars and two spare. Every car the game builds is added to its
 * 18 lists, and Add never checks the capacity, so more cars run off the end.
 * Free roam builds remote cars past the eight player slots, so the registry
 * is grown once, before any cruise builds cars.
 *
 * Technique and offsets from mc3-web by Leandro Dettmer (MIT, commit
 * 7a64713), src/games/remix/roam_cars.c. The arrays come from the contiguous
 * window; the old ones are left in the game heap (a Free from here could run
 * with another heap current, and the registry lives as long as the game).
 *
 * The particle budget (256 emitters, ~9 per car) is checked where a car is
 * built, not here; see the pool when it lands.
 *
 * Off unless MC3_ROAM=1. */

#include "kernel/xbox_memory_layout.h"
#include "kernel/kernel.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RC_REG_VA   0x005638E4u                 /* the vehicle registry */
#define RC_REG_CAP  0x7Cu                       /* its capacity */
#define RC_SLOTS    8u                          /* the game's player slots */

/* each list is {count, array}; the array is capacity * k bytes */
static const struct { unsigned char arr, k; } s_lists[] = {
    { 0x08, 4 },  { 0x10, 4 },  { 0x18, 4 },  { 0x20, 4 },  { 0x28, 4 },  { 0x30, 4 },
    { 0x38, 8 },  { 0x40, 16 }, { 0x48, 32 }, { 0x50, 8 },  { 0x58, 4 },  { 0x60, 4 },
    { 0x68, 4 },  { 0x70, 4 },  { 0x78, 4 },              /* base ctor */
    { 0x84, 4 },  { 0x8C, 4 },  { 0x94, 4 },              /* the derived ctor */
};

static int caps_ram(uint32_t p, uint32_t n)
{
    return n <= 0x04000000u && ((p >= 0x10000u && p <= 0x04000000u - n) ||
                                (p >= 0x80000000u && p <= 0x84000000u - n));
}

/* Remote cars wanted, which sets the registry size: MC3_ROAM_CARS, else 12
 * with a 64 MB window and 32 with 128 MB (the same rule as the pool). */
static unsigned roam_cars_wanted(void)
{
    static unsigned n;
    if (!n) {
        const char *e = getenv("MC3_ROAM_CARS");
        unsigned v = e && *e && strcmp(e, "auto") ? (unsigned)atoi(e)
                   : XBOX_CONTIG_SIZE >= (128u << 20) ? 32u : 12u;
        n = v < RC_SLOTS ? RC_SLOTS : v > 64u ? 64u : v;
    }
    return n;
}

static void roam_caps_registry(void)
{
    static int done;
    uint32_t reg, cap, want, i, old, neu, k;

    if (done)
        return;
    reg = MEM32(RC_REG_VA);
    if (!caps_ram(reg, 0x80))
        return;                              /* the registry is not up yet */
    cap = MEM32(reg + RC_REG_CAP);
    want = RC_SLOTS + roam_cars_wanted() + 2u;
    if (cap >= want || cap == 0 || cap > 256u) {
        done = 1;
        return;
    }
    for (i = 0; i < sizeof s_lists / sizeof s_lists[0]; i++) {
        if (!caps_ram(MEM32(reg + s_lists[i].arr), 8)) {
            fprintf(stderr, "[ROAM] caps: registry list +0x%X has no array (%08X): left at %u\n",
                    s_lists[i].arr, (unsigned)MEM32(reg + s_lists[i].arr), (unsigned)cap);
            done = 1;
            return;
        }
    }
    for (i = 0; i < sizeof s_lists / sizeof s_lists[0]; i++) {
        k = s_lists[i].k;
        old = MEM32(reg + s_lists[i].arr);
        if (!(neu = xbox_ContiguousAlloc(want * k, 16))) {
            fprintf(stderr, "[ROAM] caps: no memory for the registry (list %u): left at %u\n",
                    (unsigned)i, (unsigned)cap);
            want = 0;
            break;                           /* lists already grown are still valid */
        }
        memset((unsigned char *)XBOX_PTR(neu), 0, want * k);
        memcpy((unsigned char *)XBOX_PTR(neu), (unsigned char *)XBOX_PTR(old), cap * k);
        MEM32(reg + s_lists[i].arr) = neu;
    }
    if (!want)
        return;
    MEM32(reg + RC_REG_CAP) = want;
    done = 1;
    fprintf(stderr, "[ROAM] caps: vehicle registry %u -> %u (%u remote cars at most)\n",
            (unsigned)cap, (unsigned)want, roam_cars_wanted());
}

static int roam_caps_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("MC3_ROAM");
        on = e && e[0] == '1';
    }
    return on;
}

void roam_caps_pass(void)
{
    if (!roam_caps_on())
        return;
    roam_caps_registry();
}
