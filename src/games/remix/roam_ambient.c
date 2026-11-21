/* roam_ambient.c -- free roam: the city's ambient traffic, police and
 * pedestrians, mapped for synchronization (MC3_ROAM=1).
 *
 * Stage 2: the live instances and their transforms, measured at run time.
 * Nothing is sent or spawned yet.
 *
 * ── What the code does (from the lifted source) ─────────────────────────
 *
 *   [0x557EE4]   the city/type table, 14 entries of 0x50 bytes
 *                (sub_0026ED10, 0x464 = 4 + 14*0x50). Entry +0 = city name,
 *                +0xC = type count, +0x10 = array of type-name pointers.
 *                These are model-name strings ("s_dt", "a_fwy"), i.e. the
 *                ambient type registry, NOT live vehicles.
 *
 *   sub_0006C840  builds the per-city ambient owner; it calls sub_0006BD20,
 *                which feeds it to sub_000807A0 (the per-frame driver) with
 *                ecx = owner+0x38 (the traffic pool).
 *   sub_000807A0  calls sub_000806F0 (sizing/spawn pass, sub_00080640), then
 *                per frame sub_0006A7D0 (spawn/slot pass, sub_000719xx...).
 *   sub_0007E450  the live ambient car ctor: base class sub_00093010 gives
 *                vtable 0x44D868, a secondary vtable at +0x2C (0x44D848) and
 *                +0x38 (0x44D6C8), +0x0C render object, +0x1C the transform.
 *
 *   The owner's live list:
 *     +0x30  array of pointers to live cars (0x690 B each)
 *     +0x34  slot count (u16)
 *     +0x12C/+0x130  the lane/route table (0x21C B per lane)
 *     +0x38  the traffic pool (sub_0007FDA0 filled it from tune/traffic:
 *            +0x14 city index, +0x18 Pool_Size, +0x04 rows of
 *            {ptr, u16 count, u16 cap} at 0x68 B each)
 *
 * ── The transform (measured, not guessed) ───────────────────────────────
 *
 * A live car's pose was found by diffing the object between frames, then
 * confirmed against the traffic it must be doing:
 *
 *     mtx = [obj+0x1C]        a transform record, repointed as the car
 *                             crosses into the next one
 *       mtx+0x24              3x3 rotation, 9 contiguous floats
 *       mtx+0x48              world position, 3 contiguous floats
 *       mtx+0x88              the same pair again 0x40 later (the
 *                             interpolation pair the renderer picks from)
 *
 * Evidence, San Diego: the two position floats advance together at 15-24 m/s
 * while the height stays at the lane's ~7 m; the nine rotation floats stay
 * orthonormal and turn smoothly as the car follows the road. The player car
 * class keeps a 12-float matrix instead - this class does not, which is why
 * the registry-first guess (and the +0x10 matrix) was wrong.
 *
 * MC3_ROAM_AMBIENT=1  one line a second per live car: slot, object, model,
 *                     position, speed since the last line, distance and
 *                     heading
 * MC3_ROAM_AMBIENT=2  also the city table, the global manager, the vtables,
 *                     and per-car layout confirmations (once each)
 *
 * Only the Free Roam cruise. */

#include <math.h>

#define AMB_CITY_VA      0x00557EE4u    /* the city/type table */
#define AMB_CITY_N       14u
#define AMB_CITY_SIZE    0x50u
#define AMB_TRAFFIC_VA   0x005587E0u    /* the global ambient manager */
#define AMB_PM_VA        0x00558814u    /* the player manager */
#define AMB_NODE_MAX     8              /* objects captured by the wrappers */
#define AMB_SLOT_LOG     12             /* live cars logged per owner */
#define AMB_CAR_SIZE     0x690u         /* one live ambient car */

/* The transform record a live car points at (see the header). */
#define AMB_REC_ROT      0x24u
#define AMB_REC_POS      0x48u

/* Set by the wrappers in recomp_manual.c. */
#define AMB_TAG_CTOR     0u             /* sub_0006C840: the owner */
#define AMB_TAG_FEED     1u             /* sub_0006BD20: per cruise load */
#define AMB_TAG_SPAWN    2u             /* sub_0006A7D0: the spawn pass */
#define AMB_TAG_GLOBAL   3u             /* sub_002B8B20: the global manager */

static uint32_t s_amb_seen[AMB_NODE_MAX];
static uint32_t s_amb_tag[AMB_NODE_MAX];
static int      s_amb_nseen;

/* 0: off, 1: the per-car lines, 2: also the layout dump. */
static int amb_level(void)
{
    static int lvl = -1;
    if (lvl < 0) {
        const char *e = getenv("MC3_ROAM_AMBIENT");
        lvl = e ? atoi(e) : 0;
        if (lvl > 2)
            lvl = 2;
    }
    return lvl;
}

/* A printable string in guest memory. */
static void amb_str(uint32_t va, char *out, int cap)
{
    int i = 0;
    out[0] = 0;
    if (!online_ram(va))
        return;
    while (i < cap - 1) {
        unsigned char c = MEM8(va + (uint32_t)i);
        if (!c)
            break;
        out[i++] = (c >= 0x20 && c < 0x7F) ? (char)c : '.';
    }
    out[i] = 0;
}

static void amb_str_at(const char *what, uint32_t va)
{
    char s[40];
    amb_str(va, s, sizeof s);
    fprintf(stderr, "[AMBIENT] %s @%08X: \"%s\"\n", what, (unsigned)va, s);
}

/* Called from the wrappers (game thread); records the first objects only. */
static void amb_note(int tag, uint32_t va, uint32_t arg)
{
    int i;
    (void)arg;
    if (amb_level() <= 0 || !online_ram(va))
        return;
    for (i = 0; i < s_amb_nseen; i++)
        if (s_amb_seen[i] == va)
            return;
    if (s_amb_nseen >= AMB_NODE_MAX)
        return;
    s_amb_seen[s_amb_nseen] = va;
    s_amb_tag[s_amb_nseen] = (uint32_t)tag;
    s_amb_nseen++;
}

/* ── The registry, for the record ───────────────────────────── */

static void amb_city_table(void)
{
    uint32_t mgr = MEM32(AMB_CITY_VA), c;
    if (!online_ram_n(mgr, AMB_CITY_N * AMB_CITY_SIZE))
        return;
    fprintf(stderr, "[AMBIENT] city table %08X: %u entries of 0x%X\n",
            (unsigned)mgr, AMB_CITY_N, AMB_CITY_SIZE);
    for (c = 0; c < AMB_CITY_N; c++) {
        uint32_t e = mgr + c * AMB_CITY_SIZE;
        uint32_t cnt = MEM32(e + 0xCu), arr = MEM32(e + 0x10u), i;
        char name[40];
        amb_str(MEM32(e), name, sizeof name);
        fprintf(stderr, "[AMBIENT]  city %u \"%s\": types %u array %08X\n",
                (unsigned)c, name, (unsigned)cnt, (unsigned)arr);
        if (!online_ram(arr) || cnt > 64u)
            continue;
        for (i = 0; i < cnt && i < 8u; i++) {
            char mn[40];
            amb_str(MEM32(arr + i * 4u), mn, sizeof mn);
            fprintf(stderr, "[AMBIENT]    type %u \"%s\"\n", (unsigned)i, mn);
        }
    }
}

/* ── The live cars ──────────────────────────────────────────── */

/* Is this 12-float window a rigid transform with a sane world translation?
 * Three unit rows, then the translation - the layout the player car uses. */
static int amb_is_matrix(uint32_t a, const float me[3], float out[3], double *dist)
{
    float r[3], t[3];
    double l, dx, dy, dz;
    int i, row;

    if (!online_ram_n(a, 0x30u))
        return 0;
    for (row = 0; row < 3; row++) {
        for (i = 0; i < 3; i++)
            r[i] = MEMF(a + 0xCu * (uint32_t)row + 4u * (uint32_t)i);
        l = sqrt((double)(r[0] * r[0] + r[1] * r[1] + r[2] * r[2]));
        if (l < 0.9 || l > 1.1)
            return 0;
    }
    for (i = 0; i < 3; i++)
        t[i] = MEMF(a + 0x24u + 4u * (uint32_t)i);
    if (!(t[0] > -4000.0f && t[0] < 4000.0f && t[1] > -1000.0f && t[1] < 1000.0f &&
          t[2] > -4000.0f && t[2] < 4000.0f))
        return 0;
    out[0] = t[0]; out[1] = t[1]; out[2] = t[2];
    dx = t[0] - me[0]; dy = t[1] - me[1]; dz = t[2] - me[2];
    *dist = sqrt(dx * dx + dy * dy + dz * dz);
    return 1;
}

/* Our own pose: player manager -> player 0 -> vehicle -> matrix (12 floats at
 * +0x10, translation in the last row) - the layout roam_pool.c uses. */
static int amb_local_pos(float p[3])
{
    uint32_t pm = MEM32(AMB_PM_VA), p0, v0, m0;
    if (!online_ram(pm) || !online_ram(p0 = MEM32(pm + 8u)) ||
        !online_ram(v0 = MEM32(p0 + 0x14u)) || !online_ram_n(m0 = MEM32(v0 + 0x1Cu), 0x40u))
        return 0;
    p[0] = MEMF(m0 + 0x34u);
    p[1] = MEMF(m0 + 0x38u);
    p[2] = MEMF(m0 + 0x3Cu);
    return 1;
}

/* A live car's measured pose: position and the rotation rows, or 0 when the
 * record is not up yet (the 0xCDCDCDCD fill of a fresh slot).
 *
 * Note on what this record is: [obj+0x1C] is a *transform record* in a pool
 * the game repoints as the car crosses into the next lane segment, and the
 * same address is reused. So consecutive samples are only comparable while
 * the pointer stays the same (amb_car_line checks that), and a record switch
 * shows as a jump - that is the interpolation handoff, not a teleport. The
 * object's own +0x20 member (the class's accessor, sub_0007EFB0) is never
 * written: it holds the 0x7F800001 sentinel, so the record is the pose. */
static int amb_car_pose(uint32_t o, float p[3], float rot[9])
{
    uint32_t mtx, i;
    if (!online_ram_n(o, 0x20u) || !online_ram_n(mtx = MEM32(o + 0x1Cu), AMB_REC_POS + 12u))
        return 0;
    for (i = 0; i < 3; i++)
        p[i] = MEMF(mtx + AMB_REC_POS + 4u * i);
    if (!(p[0] > -4000.0f && p[0] < 4000.0f) || !(p[1] > -1000.0f && p[1] < 1000.0f) ||
        !(p[2] > -4000.0f && p[2] < 4000.0f))
        return 0;
    if (rot)
        for (i = 0; i < 9; i++)
            rot[i] = MEMF(mtx + AMB_REC_ROT + 4u * i);
    return 1;
}

/* The previous sample per car: the speed comes from the distance between two
 * one-second lines, so the reading can be checked against real traffic. The
 * record pointer is kept too: when it changes the car crossed into a new
 * transform record (or respawned), and the distance would be meaningless. */
typedef struct { uint32_t obj, rec; float pos[3]; int ok; } amb_prev;

static amb_prev s_amb_prev[AMB_SLOT_LOG];
static int s_amb_nprev;

static amb_prev *amb_prev_for(uint32_t o)
{
    int i;
    for (i = 0; i < s_amb_nprev; i++)
        if (s_amb_prev[i].obj == o)
            return &s_amb_prev[i];
    if (s_amb_nprev >= AMB_SLOT_LOG)
        return NULL;
    memset(&s_amb_prev[s_amb_nprev], 0, sizeof s_amb_prev[s_amb_nprev]);
    s_amb_prev[s_amb_nprev].obj = o;
    return &s_amb_prev[s_amb_nprev++];
}

/* One line a second per live car: exactly what a ghost would need. */
static void amb_car_line(uint32_t slot, uint32_t o, const float me[3])
{
    float p[3], rot[9], dx, dy, dz;
    amb_prev *pv = amb_prev_for(o);
    uint32_t rec = online_ram_n(o, 0x20u) ? MEM32(o + 0x1Cu) : 0;
    char speed[40] = "";

    if (!amb_car_pose(o, p, rot)) {
        fprintf(stderr, "[AMBIENT]  slot %2u obj %08X: no live transform yet\n",
                (unsigned)slot, (unsigned)o);
        return;
    }
    if (pv && pv->ok && pv->rec == rec) {
        dx = p[0] - pv->pos[0]; dy = p[1] - pv->pos[1]; dz = p[2] - pv->pos[2];
        snprintf(speed, sizeof speed, " | %.1f m/s",
                 (float)sqrt((double)(dx * dx + dy * dy + dz * dz)));
    } else if (pv && pv->ok) {
        snprintf(speed, sizeof speed, " | record moved %08X -> %08X", (unsigned)pv->rec, (unsigned)rec);
    }
    fprintf(stderr, "[AMBIENT]  slot %2u obj %08X vt %08X rend %08X | pos %.2f %.2f %.2f%s"
                    " | %.1f m away | fwd %.2f %.2f %.2f\n",
            (unsigned)slot, (unsigned)o, (unsigned)MEM32(o),
            (unsigned)MEM32(o + 0x0Cu), p[0], p[1], p[2], speed,
            (float)sqrt((double)((p[0] - me[0]) * (p[0] - me[0]) +
                                 (p[1] - me[1]) * (p[1] - me[1]) +
                                 (p[2] - me[2]) * (p[2] - me[2]))),
            rot[6], rot[7], rot[8]);
    if (pv) {
        memcpy(pv->pos, p, sizeof p);
        pv->rec = rec;
        pv->ok = 1;
    }
}

/* Where does the game keep this owner? Scanning .data for the pointer finds
 * the registry it lives in - and the pedestrian/cop owners are likely the
 * neighbouring entries. One scan per run, no writes. */
#define AMB_DATA_LO 0x004A8140u
#define AMB_DATA_HI 0x0056DE60u

static void amb_find_refs(const char *what, uint32_t value)
{
    uint32_t a, shown = 0;
    if (!value)
        return;
    for (a = AMB_DATA_LO; a + 4u <= AMB_DATA_HI && shown < 8u; a += 4u) {
        if (MEM32(a) != value)
            continue;
        shown++;
        fprintf(stderr, "[AMBIENT] %s %08X stored at %08X (nearby:", what, (unsigned)value, (unsigned)a);
        {   uint32_t k, b = a - 0x10u;
            for (k = 0; k < 0x30u; k += 4u)
                fprintf(stderr, " %08X", (unsigned)MEM32(b + k));
        }
        fprintf(stderr, ")\n");
    }
    if (!shown)
        fprintf(stderr, "[AMBIENT] %s %08X: no .data reference\n", what, (unsigned)value);
}

/* Where does the game keep this owner? It is not in .data, so it lives in a
 * heap list: scan the contiguous window for a pointer to it and dump the
 * owner of that list. One pass per run. */
#define AMB_HEAP_LO 0x82000000u
#define AMB_HEAP_HI 0x84000000u

static uint32_t amb_find_refs_heap(const char *what, uint32_t value)
{
    uint32_t a, shown = 0, first = 0;
    if (!value)
        return 0;
    for (a = AMB_HEAP_LO; a + 4u <= AMB_HEAP_HI && shown < 6u; a += 4u) {
        if (MEM32(a) != value)
            continue;
        shown++;
        if (!first)
            first = a;
        fprintf(stderr, "[AMBIENT] %s %08X referenced at %08X (nearby:", what, (unsigned)value, (unsigned)a);
        {   uint32_t k, b = a - 0x10u;
            for (k = 0; k < 0x30u; k += 4u)
                fprintf(stderr, " %08X", (unsigned)MEM32(b + k));
        }
        fprintf(stderr, ")\n");
    }
    if (!shown)
        fprintf(stderr, "[AMBIENT] %s %08X: no heap reference found\n", what, (unsigned)value);
    return first;
}

/* A range of guest memory as dwords, with every plausible object pointer
 * annotated by its vtable: enough to spot the sibling managers (each has a
 * .rdata vtable and the same owner shape at +0x30/+0x34). One pass. */
static void amb_heap_dump(uint32_t base, uint32_t len)
{
    uint32_t a, i;
    if (!online_ram_n(base, len))
        return;
    fprintf(stderr, "[AMBIENT] heap %08X..%08X:\n", (unsigned)base, (unsigned)(base + len));
    for (a = base & ~0xFu; a < base + len; a += 16u) {
        fprintf(stderr, "[AMBIENT]  %08X:", (unsigned)a);
        for (i = 0; i < 16u; i += 4u) {
            uint32_t v = MEM32(a + i);
            fprintf(stderr, " %08X", (unsigned)v);
        }
        fprintf(stderr, "  |");
        for (i = 0; i < 16u; i += 4u) {
            uint32_t v = MEM32(a + i), vt;
            if (!online_ram_n(v, 0x40u))
                continue;
            vt = MEM32(v);
            if (vt >= 0x004476E0u && vt < 0x004A8140u)
                fprintf(stderr, " +%X:vt%08X/30:%u/34:%u", (unsigned)i, (unsigned)vt,
                        (unsigned)MEM32(v + 0x30u), (unsigned)ZX16(MEM16(v + 0x34u)));
        }
        fprintf(stderr, "\n");
    }
}

/* Every object in the heap whose first dword is `vt`: if the pedestrians and
 * police use the same owner class as traffic, they are all here, and the
 * count/array/lane fields say which is which. One pass. */
static void amb_census_vt(uint32_t vt)
{
    uint32_t a, shown = 0;
    for (a = AMB_HEAP_LO; a + 0x140u <= AMB_HEAP_HI && shown < 12u; a += 4u) {
        if (MEM32(a) != vt)
            continue;
        shown++;
        fprintf(stderr, "[AMBIENT] census %08X: +04 %08X +08 %08X +0C %08X | city %u time %u"
                        " slots %u arr %08X | lanes %u tab %08X | tune %08X/%08X +128 %08X +130 %08X\n",
                (unsigned)a, (unsigned)MEM32(a + 4u), (unsigned)MEM32(a + 8u), (unsigned)MEM32(a + 0xCu),
                (unsigned)MEM32(a + 0x38u + 0x14u), (unsigned)MEM32(a + 0x38u + 0x18u),
                (unsigned)ZX16(MEM16(a + 0x34u)), (unsigned)MEM32(a + 0x30u),
                (unsigned)ZX16(MEM16(a + 0x12Cu)), (unsigned)MEM32(a + 0x130u),
                (unsigned)MEM32(a + 0x11Cu), (unsigned)MEM32(a + 0x124u),
                (unsigned)MEM32(a + 0x128u), (unsigned)MEM32(a + 0x130u));
    }
    fprintf(stderr, "[AMBIENT] census vt %08X: %u object(s)\n", (unsigned)vt, (unsigned)shown);
}

/* Scan the heap for objects with the *owner shape*, whatever their vtable:
 * a populated pointer array at +0x30 with a small u16 count at +0x34 and a
 * city/time pair at +0x4C/+0x50 (pool+0x14/+0x18). Pedestrians and police do
 * not share the traffic vtable, so this is the way to find their owners. */
static void amb_census_shape(void)
{
    uint32_t a, shown = 0, me_city = 0;
    {   /* the city the traffic owner reports, so unrelated pools don't match */
        uint32_t i;
        for (i = 0; i < (uint32_t)s_amb_nseen; i++)
            if (s_amb_tag[i] != AMB_TAG_GLOBAL && s_amb_seen[i]) {
                me_city = MEM32(s_amb_seen[i] + 0x38u + 0x14u);
                break;
            }
    }
    for (a = AMB_HEAP_LO; a + 0x140u <= AMB_HEAP_HI && shown < 16u; a += 4u) {
        uint32_t vt = MEM32(a), arr, count, city, time, pvt;
        if (vt < 0x004476E0u || vt >= 0x004A8140u)
            continue;
        pvt = MEM32(a + 0x38u);
        if (pvt < 0x004476E0u || pvt >= 0x004A8140u)
            continue;                    /* the owner embeds its pool here */
        count = ZX16(MEM16(a + 0x34u));
        arr = MEM32(a + 0x30u);
        if (count < 1u || count > 64u || !online_ram_n(arr, count * 4u))
            continue;
        city = MEM32(a + 0x38u + 0x14u);
        time = MEM32(a + 0x38u + 0x18u);
        if (city != me_city || time > 4u)
            continue;
        shown++;
        fprintf(stderr, "[AMBIENT] shape %08X: vt %08X poolvt %08X slots %u arr %08X first %08X"
                        " | city %u time %u lanes %u\n",
                (unsigned)a, (unsigned)vt, (unsigned)pvt, (unsigned)count, (unsigned)arr,
                (unsigned)MEM32(arr), (unsigned)city, (unsigned)time,
                (unsigned)ZX16(MEM16(a + 0x12Cu)));
    }
    fprintf(stderr, "[AMBIENT] shape scan: %u candidate owner(s)\n", (unsigned)shown);
}

/* One ambient owner: its slot list and the live cars in it. */
static void amb_owner(const char *tag, uint32_t c)
{
    uint32_t cnt, arr, pool, i, live = 0, shown = 0;
    float me[3];
    int have_me = amb_local_pos(me);

    if (!online_ram_n(c, 0x134u)) {
        fprintf(stderr, "[AMBIENT] %s %08X: not an ambient owner any more\n", tag, (unsigned)c);
        return;
    }
    cnt = ZX16(MEM16(c + 0x34u));
    arr = MEM32(c + 0x30u);
    pool = c + 0x38u;
    fprintf(stderr, "[AMBIENT] %s %08X: %u slots | city %u time %u models %u"
                    " | lanes %u | %s\n",
            tag, (unsigned)c, (unsigned)cnt,
            (unsigned)MEM32(pool + 0x14u), (unsigned)MEM32(pool + 0x18u),
            (unsigned)ZX16(MEM16(pool + 8u)),
            (unsigned)ZX16(MEM16(c + 0x12Cu)),
            have_me ? "player pose known" : "no player pose");
    if (have_me)
        fprintf(stderr, "[AMBIENT]  we are at %.2f %.2f %.2f\n", me[0], me[1], me[2]);
    if (!online_ram(arr) || cnt > 512u)
        return;
    for (i = 0; i < cnt; i++) {
        uint32_t o = MEM32(arr + i * 4u);
        if (!o || !online_ram(o))
            continue;
        live++;
        if (shown++ >= AMB_SLOT_LOG)
            continue;
        amb_car_line(i, o, have_me ? me : (const float[3]){ 0, 0, 0 });
    }
    fprintf(stderr, "[AMBIENT]  %u live, %u slots\n", (unsigned)live, (unsigned)cnt);
}

/* ── Layout dump (level 2), once ────────────────────────────── */

static void amb_vtable(const char *what, uint32_t vt)
{
    uint32_t i;
    if (!online_ram_n(vt, 0x50u))
        return;
    fprintf(stderr, "[AMBIENT] vtable %s %08X:", what, (unsigned)vt);
    for (i = 0; i < 20u; i++)
        fprintf(stderr, " %08X", (unsigned)MEM32(vt + i * 4u));
    fprintf(stderr, "\n");
}

static void amb_global(void)
{
    static const uint32_t vec[5] = { 0x2Cu, 0x34u, 0x3Cu, 0x44u, 0x4Cu };
    uint32_t g = MEM32(AMB_TRAFFIC_VA), i;
    if (!online_ram_n(g, 0x60u))
        return;
    fprintf(stderr, "[AMBIENT] global %08X vt %08X | +2C %08X +34 %08X +3C %08X"
                    " +44 %08X +4C %08X +50 %08X +58 %08X +5C %08X\n",
            (unsigned)g, (unsigned)MEM32(g),
            (unsigned)MEM32(g + 0x2Cu), (unsigned)MEM32(g + 0x34u),
            (unsigned)MEM32(g + 0x3Cu), (unsigned)MEM32(g + 0x44u),
            (unsigned)MEM32(g + 0x4Cu), (unsigned)MEM32(g + 0x50u),
            (unsigned)MEM32(g + 0x58u), (unsigned)MEM32(g + 0x5Cu));
    for (i = 0; i < 5u; i++) {
        uint32_t p = MEM32(g + vec[i]);
        if (!online_ram(p))
            continue;
        fprintf(stderr, "[AMBIENT]  global+%02X -> %08X: count %u first %08X %08X %08X\n",
                (unsigned)vec[i], (unsigned)p, (unsigned)MEM32(p - 4u),
                (unsigned)MEM32(p), (unsigned)MEM32(p + 4u), (unsigned)MEM32(p + 8u));
    }
}

/* One live car's head, and the record it points at: enough to re-check the
 * offsets later without a new build. */
static void amb_car_layout(uint32_t o)
{
    uint32_t mtx = MEM32(o + 0x1Cu), i;
    fprintf(stderr, "[AMBIENT] car %08X head:", (unsigned)o);
    for (i = 0; i < 0x40u; i += 4u)
        fprintf(stderr, " %08X", (unsigned)MEM32(o + i));
    fprintf(stderr, "\n[AMBIENT] car %08X record %08X:", (unsigned)o, (unsigned)mtx);
    if (online_ram_n(mtx, 0x80u))
        for (i = 0; i < 0x80u; i += 4u)
            fprintf(stderr, " %.3f", MEMF(mtx + i));
    fprintf(stderr, "\n");
}

/* ── Once a second, from the physics hook ───────────────────── */

static void dri_ambient_pass(void)
{
    static int once;
    static time_t last;
    static const char *const names[] = { "owner", "feed", "spawn", "global" };
    int lvl, i;
    time_t now;

    if (!roam_running() || !s_roam_cruise || !s_online)
        return;
    lvl = amb_level();
    if (lvl <= 0)
        return;
    if (!once) {
        once = 1;
        if (lvl >= 2) {
            amb_str_at("tune key City", 0x448780u);
            amb_str_at("tune key Time", 0x44D8B8u);
            amb_str_at("tune key Pool_Size", 0x44D8ACu);
            amb_str_at("variant Large", 0x44D8CCu);
            amb_str_at("variant Small", 0x44D8C4u);
            amb_str_at("traffic path", 0x44D89Cu);
            amb_city_table();
            amb_global();
            amb_vtable("ambient car", 0x44D868u);
            amb_vtable("global manager", 0x475FD8u);
        }
    }
    now = time(NULL);
    if (now == last)
        return;
    last = now;
    if (!s_amb_nseen) {
        fprintf(stderr, "[AMBIENT] no ambient object captured yet"
                        " (sub_0006C840 / sub_0006A7D0 not seen)\n");
        return;
    }
    {   /* level 2: where the traffic owner (or its pool) is registered */
        static int refs_done;
        uint32_t own = 0, k;
        if (lvl >= 2 && !refs_done) {
            for (k = 0; k < (uint32_t)s_amb_nseen; k++)
                if (s_amb_tag[k] != AMB_TAG_GLOBAL && s_amb_seen[k]) {
                    own = s_amb_seen[k];
                    break;
                }
            if (own) {
                uint32_t ref;
                refs_done = 1;
                amb_find_refs("owner", own);
                ref = amb_find_refs_heap("owner", own);
                if (ref)
                    amb_heap_dump(ref - 0x60u, 0x140u);
                amb_census_vt(MEM32(own));
                amb_census_shape();
            }
        }
    }
    for (i = 0; i < s_amb_nseen; i++) {
        uint32_t cnt, arr;
        static int laid_out;
        if (s_amb_tag[i] == AMB_TAG_GLOBAL)
            continue;               /* not this layout: amb_global() showed it */
        amb_owner(names[s_amb_tag[i] & 3u], s_amb_seen[i]);
        if (lvl < 2 || laid_out)
            continue;
        cnt = ZX16(MEM16(s_amb_seen[i] + 0x34u));
        arr = MEM32(s_amb_seen[i] + 0x30u);
        if (online_ram(arr) && cnt) {
            uint32_t o = MEM32(arr);
            if (online_ram(o)) {
                laid_out = 1;
                amb_car_layout(o);
            }
        }
    }
}
