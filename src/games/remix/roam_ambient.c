/* roam_ambient.c -- free roam: the city's ambient traffic, police and
 * pedestrians, mapped for synchronization (MC3_ROAM=1).
 *
 * Stage 1 only: read the ambient manager and its pools at run time and log
 * them, so the wire format can be built from measured offsets instead of
 * guesses (MC3_ROAM_AMBIENT=1). Nothing is sent or spawned yet.
 *
 * What the disassembly says (work-remix/gen/recomp_0011.c sub_0007FDA0,
 * recomp_0047.c sub_0026ED10):
 *
 *   [0x557EE4]  the ambient manager, made once (sub_0026ED10, 0x464 bytes).
 *               Three pool heads at +0x00, +0x50, +0xA0, plus more at
 *               +0xB0 and +0xF0. Each head: vtable +0, something +4/+8,
 *               count +0xC, array +0x10, and two names (+0x14 City,
 *               +0x18 Time) that sub_0007FDA0 resolves through
 *               sub_0020F2E0 / sub_0026D340 / sub_0026D3E0.
 *               The count is per city: sub_0007FDA0 reads entry i at
 *               array + i*0x50.
 *   sub_0007FDA0(base, ...)  fills one pool from tune/traffic/<city>.
 *   sub_000806F0 / sub_000807A0  the per-frame drivers.
 *
 * Only the Free Roam cruise. */

#define AMB_MGR_VA      0x00557EE4u     /* the ambient manager */
#define AMB_ENTRY       0x50u           /* an ambient entry */
#define AMB_POOLS       6               /* heads at +0x00, +0x50, +0xA0, +0xB0, +0xF0, +0x140 */
#define AMB_MAX_LOG     24              /* entries logged per pool */

static int amb_env(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("MC3_ROAM_AMBIENT");
        on = e && e[0] == '1';
    }
    return on;
}

/* One line per pool: count, array, and the first entries' places. */
static void amb_dump_pool(uint32_t mgr, uint32_t head_off, int pool)
{
    uint32_t head = mgr + head_off, count, array, i, e, n = 0;
    if (!online_ram(head))
        return;
    count = MEM32(head + 0xCu);
    array = MEM32(head + 0x10u);
    fprintf(stderr, "[AMBIENT] pool %d @%08X: count %u array %08X (entry 0x%X B)\n", pool, (unsigned)head,
            (unsigned)count, (unsigned)array, (unsigned)AMB_ENTRY);
    if (!online_ram(array) || count > 4096u)
        return;
    for (i = 0; i < count && i < AMB_MAX_LOG; i++) {
        uint32_t inst, vt, r;
        e = array + i * 4u;             /* the array holds pointers to instances */
        if (!online_ram(e))
            break;
        inst = MEM32(e);
        if (!online_ram(inst)) {
            fprintf(stderr, "[AMBIENT]   %u: (instance %08X not in RAM)\n", (unsigned)i, (unsigned)inst);
            n++;
            continue;
        }
        vt = MEM32(inst);
        r = MEM32(inst + 0xCu);         /* render/vehicle object candidate, as a player car has at +0xC */
        fprintf(stderr, "[AMBIENT]   %u: inst %08X vt %08X +04 %08X +08 %08X +0C %08X +10 %08X%s\n", (unsigned)i,
                (unsigned)inst, (unsigned)vt, (unsigned)MEM32(inst + 4u), (unsigned)MEM32(inst + 8u),
                (unsigned)r, (unsigned)MEM32(inst + 0x10u),
                online_ram(r) && online_ram(MEM32(r)) ? " (render object)" : "");
        n++;
    }
    if (count > n)
        fprintf(stderr, "[AMBIENT]   ... %u more\n", (unsigned)(count - n));
}

static void dri_ambient_pass(void)
{
    static int done;
    uint32_t mgr, i;
    if (!amb_env() || done || !roam_running() || !s_roam_cruise || !s_online)
        return;
    mgr = MEM32(AMB_MGR_VA);
    if (!online_ram_n(mgr, 0x464)) {
        fprintf(stderr, "[AMBIENT] manager at [0x%08X] = %08X: not up yet\n", (unsigned)AMB_MGR_VA, (unsigned)mgr);
        return;
    }
    done = 1;
    fprintf(stderr, "[AMBIENT] manager %08X: vt %08X, pools:\n", (unsigned)mgr, (unsigned)MEM32(mgr));
    for (i = 0; i < AMB_POOLS; i++) {
        static const uint32_t offs[AMB_POOLS] = { 0x00, 0x50, 0xA0, 0xB0, 0xF0, 0x140 };
        amb_dump_pool(mgr, offs[i], (int)i);
    }
}
