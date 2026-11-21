/* roam_blips.c -- free roam: a map blip for every linked player (MC3_ROAM=1).
 *
 * The game's own walker (sub_000BF850) draws a blip for each player whose car
 * it counts drawn; standalone remote cars are no players at all, and a slot's
 * car only when the game says so. Upstream (mc3-web, MIT) appends one blip per
 * linked peer after that walker; this is that part only (the hub FAR list and
 * the race start points are not ported).
 *
 * Placement: the map's world -> map transform (vtable +0x94, about our car's
 * matrix), then the car blip primitive sub_000BB310 (thiscall map; colour
 * float4*, Matrix34*, scale, 0). Colours and scale from the HUD tuning at
 * [0x507508]: outline +0x90, fill +0x40, scale +0x1C, blip scale from the
 * map's vtable +0x80. A peer away (status != ROAM, or no car in the radius)
 * is dimmed and smaller, as upstream's far players. Render state around the
 * pass is saved and restored ([0x507A94]).
 *
 * Only the Free Roam cruise and only while the transport runs. MC3_ROAM_MAP=0
 * turns the blips off. */

#define BLIP_G_VA       0x00507508u     /* the HUD's tuning: colours and scales */
#define BLIP_RSTATE_VA  0x00507A94u     /* the render state sub_0003A6A0 sets */
#define BLIP_DIM        0.55f
#define BLIP_FAR_SIZE   0.8f

extern void sub_0003A6A0(void);         /* render state, cdecl (int) */
extern void sub_00030780(void);         /* render state flush */
extern void sub_000BB310(void);         /* map blip, thiscall (colour, Matrix34*, scale, int), ret 0x10 */
extern void sub_002BF570(void);         /* GetPlayer, cdecl (index) */
extern void sub_002C84D0(void);         /* a player's car matrix, thiscall */

static int blip_env(void)               /* MC3_ROAM_MAP, read once */
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("MC3_ROAM_MAP");
        on = !e || e[0] != '0';
    }
    return on;
}

static uint32_t blip_f2u(float f) { uint32_t u; memcpy(&u, &f, 4); return u; }

/* The map's blip scale: its vtable +0x80 is `mov eax, [abs]; fld dword [eax+d8]; ret`. */
static float blip_scale(uint32_t map)
{
    uint32_t fn = MEM32(MEM32(map) + 0x80u), p;
    if (fn < 0x10000u || fn >= 0x00440000u)
        return 1.0f;
    if (MEM8(fn) != 0xA1u || MEM8(fn + 5u) != 0xD9u || MEM8(fn + 6u) != 0x40u)
        return 1.0f;
    p = MEM32(MEM32(fn + 1u));
    return online_ram(p) ? MEMF(p + (uint32_t)(int32_t)(int8_t)MEM8(fn + 7u)) : 1.0f;
}

/* The local player's car matrix for a map (player index at +0xC, -1: 0); 0 none. */
static uint32_t blip_car_mat(uint32_t map, uint32_t buf)
{
    uint32_t idx = MEM32(map + 0xCu), a = idx == 0xFFFFFFFFu ? 0u : idx, pl, mat;
    pl = roam_call(sub_002BF570, 0, buf, 1, &a);
    if (!online_ram(pl) || !online_ram(mat = roam_call(sub_002C84D0, pl, buf, 0, NULL)))
        return 0;
    return mat;
}

/* After sub_000BF850 (map), top: the caller's esp at the call (scratch below). */
static void roam_blips(uint32_t map, uint32_t top)
{
    static int said;
    uint32_t g, vt, xf, buf, col, args[5], mat, saved, k, i;
    float rot[9], s, si;
    int n = 0;
    if (!blip_env() || !roam_running() || !s_roam_cruise || !s_online || roam_fake())
        return;
    if (!s_roam_seen || s_roam_nseen <= 0 || roam_now() - s_roam_seen_t > 0.5)
        return;
    if (!online_ram(map) || !online_ram(g = MEM32(BLIP_G_VA)) || !online_ram(vt = MEM32(map)))
        return;
    buf = (top - 0x200u) & ~0xFu;               /* Matrix34 at buf, dim fill colour at buf + 0x40 */
    if (!(mat = blip_car_mat(map, buf)))
        return;
    xf = MEM32(vt + 0x94u);
    if (!xf)
        return;
    s = blip_scale(map);
    col = buf + 0x40u;
    for (k = 0; k < 4; k++)
        MEMF(col + 4u * k) = MEMF(g + 0x40u + 4u * k) * BLIP_DIM;
    saved = MEM32(BLIP_RSTATE_VA);
    args[0] = 0;
    roam_call(sub_0003A6A0, 0, buf, 1, args);
    for (i = 0; i < (uint32_t)s_roam_nseen; i++) {
        const roam_peer *p = &s_roam_seen[i];
        int far;
        if (p->status >= RC_ST__N)
            continue;
        far = p->status != RC_ST_ROAM ||
              (!(p->bits & ROAM_BIT_RACE) && !roam_pool_shows(p->id) && !roam_pool_beyond(p->dist));
        si = far ? s * BLIP_FAR_SIZE : s;
        roam_quat_mat(p->q, rot);
        for (k = 0; k < 9; k++)
            MEMF(buf + 4u * k) = rot[k];
        for (k = 0; k < 3; k++)
            MEMF(buf + 0x24u + 4u * k) = p->p[k];
        args[0] = buf + 0x24u; args[1] = mat + 0x24u; args[2] = buf + 0x24u;
        roam_call(recomp_lookup(xf), map, buf, 3, args);
        args[0] = g + 0x90u; args[1] = buf; args[2] = blip_f2u(MEMF(g + 0x1Cu) * si); args[3] = 0;
        roam_call(sub_000BB310, map, buf, 4, args);
        args[0] = far ? col : g + 0x40u; args[2] = blip_f2u(si);
        roam_call(sub_000BB310, map, buf, 4, args);
        n++;
    }
    args[0] = saved;
    roam_call(sub_0003A6A0, 0, buf, 1, args);
    roam_call(sub_00030780, 0, buf, 0, NULL);
    if (!said && n) {
        said = 1;
        fprintf(stderr, "[ROAM] map: %d peer(s) drawn as blips (minimap/full map)\n", n);
    }
}
