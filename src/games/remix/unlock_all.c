/* unlock_all.c -- MC3_UNLOCK_ALL=1: the game's own "everything unlocked" flags.
 *
 * The five globals below are read by the game's checks before its own
 * unlocking rules, so they are not save data and do not alter a career
 * profile. The addresses and the rule-type notes come from mc3-web by
 * Leandro Dettmer (MIT; commit 7a64713, the last MIT-era commit):
 *
 *   0x0053F2EC  races      every race unlocked (sub_0028A1B0, type 6)
 *   0x0053F2ED  city       every city unlocked (sub_00289CC0, type 5)
 *   0x0053F2EE  cars       every showroom car buyable (sub_00289D20/D40/D80)
 *   0x0053F2F0  abilities  rule types 0xB-0x10 answer 1
 *                          (sub_00289EB0 and sub_00289E10/E30/E50/E70/E90)
 *   0x0053F2F2  garage     every part, rim, paint and perf level
 *                          (the garage's own "all unlocked" flag)
 *
 * A car picks up an ability at its next build (cruise load or garage), as the
 * game does, so a current car may need a reload to show one.
 *
 * The two reward paints are not covered by 0x0053F2F2: the paint list
 * (sub_0014C180) asks the GENERIC check sub_0028B4E0 by name. The wrapper at
 * the end answers those two names while the toggle is on.
 *
 * Nothing in the game writes 0x0053F2EE or 0x0053F2F2, but the cheat handler
 * does write 0x0053F2EC/ED, and a profile load may clear the block early.
 * The flags are therefore re-asserted once a game frame; a byte write each
 * is free next to the frame itself.
 *
 * Money is deliberately not granted: it is not part of "unlocked", and a
 * career save that suddenly gains $2,000,000 is a worse surprise than a
 * garage you still have to pay for. */

#define UA_RACES    0x0053F2ECu
#define UA_CITY     0x0053F2EDu
#define UA_CARS     0x0053F2EEu
#define UA_ABILITY  0x0053F2F0u
#define UA_GARAGE   0x0053F2F2u

static int unlock_all_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("MC3_UNLOCK_ALL");
        on = e && e[0] == '1';
        if (on)
            fprintf(stderr, "[MODS] unlock everything on (MC3_UNLOCK_ALL): races, cities, cars,"
                    " garage items and abilities; career money is untouched\n");
    }
    return on;
}

/* MC3_UNLOCK_REPORT=1 (diagnostic, off by default): once the front end has
 * its rules object, ask the game's own vehicle-purchase check
 * (sub_00289D80, thiscall (car name), ret 4) for every showroom car and say
 * how many it calls buyable. Same probe mc3-web's online_cars_unlocked()
 * uses, so the count is the game's answer, not ours. Runs once. */
void sub_00289D80(void);
static int ua_ram(uint32_t p, uint32_t n)
{
    return n <= 0x04000000u && ((p >= 0x10000u && p <= 0x04000000u - n) ||
                                (p >= 0x80000000u && p <= 0x84000000u - n));
}

static void unlock_all_report(void)
{
    static int on = -1, done;
    static unsigned tries;
    uint32_t mgr, prog, rules, n, i, ok = 0, saved[8];

    if (on < 0) {
        const char *e = getenv("MC3_UNLOCK_REPORT");
        on = e && e[0] == '1';
        if (on)
            fprintf(stderr, "[MODS] unlock report on (MC3_UNLOCK_REPORT): counting showroom cars"
                    " the game calls buyable\n");
    }
    if (!on || done || tries >= 60)
        return;
    /* [0x557F0C] = career manager, +0xC = progress, +0xC = rules; the
     * showroom's [0x557EB4] cars, CarCfg i at 0x541700 + i*0x108. */
    mgr = MEM32(0x00557F0Cu);
    if (!ua_ram(mgr, 0x10) || !ua_ram(prog = MEM32(mgr + 0xCu), 0x10) ||
        !ua_ram(rules = MEM32(prog + 0xCu), 0x10) || !ua_ram(MEM32(0x00557EB0u), 0x40))
        return;                                 /* not up yet: try again next frame */
    if (++tries < 30)                           /* give the front end a moment */
        return;
    n = MEM16(0x00557EB4u);
    if (n > 127u)
        n = 127u;
    saved[0] = g_eax; saved[1] = g_ecx; saved[2] = g_edx; saved[3] = g_ebx;
    saved[4] = g_esp; saved[5] = g_ebp; saved[6] = g_esi; saved[7] = g_edi;
    for (i = 0; i < n; i++) {
        g_esp = (saved[4] - 0x200u) & ~0xFu;
        g_esp -= 4; MEM32(g_esp) = 0x00541700u + i * 0x108u + 0xE3u;
        g_esp -= 4; MEM32(g_esp) = 0;           /* return address */
        g_ecx = rules;
        sub_00289D80();
        ok += (g_eax & 0xFFu) != 0;
    }
    g_eax = saved[0]; g_ecx = saved[1]; g_edx = saved[2]; g_ebx = saved[3];
    g_esp = saved[4]; g_ebp = saved[5]; g_esi = saved[6]; g_edi = saved[7];
    done = 1;
    fprintf(stderr, "[MODS] showroom report: %u of %u cars read as buyable by the game's own"
            " check\n", ok, n);
}

/* Called before each game frame: set what is off, and say it once. */
static void unlock_all_pass(void)
{
    static int said;
    unlock_all_report();    /* MC3_UNLOCK_REPORT: independent of the toggle */
    if (!unlock_all_on())
        return;
    if (MEM8(UA_RACES) != 1)   MEM8(UA_RACES) = 1;
    if (MEM8(UA_CITY) != 1)    MEM8(UA_CITY) = 1;
    if (MEM8(UA_CARS) != 1)    MEM8(UA_CARS) = 1;
    if (MEM8(UA_ABILITY) != 1) MEM8(UA_ABILITY) = 1;
    if (MEM8(UA_GARAGE) != 1)  MEM8(UA_GARAGE) = 1;
    if (!said) {
        said = 1;
        fprintf(stderr, "[MODS] unlock flags applied: races %u, city %u, cars %u,"
                " abilities %u, garage %u\n", MEM8(UA_RACES), MEM8(UA_CITY),
                MEM8(UA_CARS), MEM8(UA_ABILITY), MEM8(UA_GARAGE));
    }
}

/* The GENERIC check sub_0028B4E0 (thiscall (name, x), ret 8): the two reward
 * paints are asked for by name, not through 0x0053F2F2. The wrapper itself
 * lives in recomp_manual.c, where the lift scanner sees it. */
int unlock_all_paint(void)
{
    uint32_t name_va;
    const char *name;
    if (!unlock_all_on())
        return 0;
    name_va = MEM32(g_esp + 4u);
    name = name_va ? (const char *)XBOX_PTR(name_va) : NULL;
    if (!name || (strncmp(name, "Pearlescent_reward", 19) &&
                  strncmp(name, "ColorShift_reward", 18)))
        return 0;
    g_eax = (g_eax & ~0xFFu) | 1u;
    g_esp += 12u;                           /* return address + 2 arguments */
    return 1;
}
