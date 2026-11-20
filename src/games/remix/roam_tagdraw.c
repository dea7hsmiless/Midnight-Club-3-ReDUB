/* roam_tagdraw.c -- free roam: a name over each remote player's car, drawn by
 * the port (MC3_ROAM=1).
 *
 * Upstream (mc3-web, MIT) publishes its tags to a page and lets the browser
 * draw them (roam_tags.c, roam_tags_box). A native build has no page, so the
 * same projection runs here and the tags go to the port's own overlay
 * (xboxrecomp/src/nv2a_gl/port_overlay.h), which already draws the FPS readout
 * and the now-playing line: a small text pass in the game's own screen space.
 *
 * The tag sits where the game's opponent arrow would be: the car's render
 * object ([veh+0xC]) place ([r+0xC4], drawn flag [r+0x12D]), lifted by the
 * model's top ([[r+4]+8] ... +0x18) and the arrow height (+0x274), through
 * the camera's view matrix ([0x559758], set by sub_002D3DA0) and the
 * viewport's projection (vp+4, built by sub_002D4AE0). The projection is read
 * in the sub_002D4AE0 override for the biggest viewport of the frame, which
 * is the main view (the mirror and the minimap are smaller).
 *
 * Fade: none near, out from 200 m to 300 m, as upstream. Names are shown only
 * while driving (remix_driving: not paused, not in a menu, not in the garage)
 * and never with MC3_ROAM_FAKE. MC3_NAMETAGS=0 turns them off live;
 * MC3_ROAM_TRACE=1 logs our own car's projection once a second.
 *
 * Only the free roam cruise, and only while the transport runs. */

#define TAG_MAX     32
#define TAG_NAME    24
#define TAG_VIEW_VA 0x00559758u             /* the view matrix (sub_002D3DA0) */
#define TAG_CAM_VA  0x00559890u             /* the camera's place, as sub_000FA070 reads it */

typedef struct {
    float x, y;                         /* 0..1 across the picture */
    float alpha;
    char name[TAG_NAME];
} tag_draw;

/* The tags this frame, for the overlay (port_overlay.h reads them). */
static struct {
    volatile int n;
    tag_draw t[TAG_MAX];
} s_tagbox;

/* The overlay's reader (port_overlay.h, another library): count, and *tags
 * pointing at the array. 0 when there is nothing to draw. */
int roam_tag_box(void **tags)
{
    if (!s_tagbox.n) {
        if (tags)
            *tags = NULL;
        return 0;
    }
    if (tags)
        *tags = (void *)s_tagbox.t;
    return s_tagbox.n;
}

static int tag_env(const char *name, int def)
{
    const char *e = getenv(name);
    return e && *e ? e[0] != '0' : def;
}

static float tag01(float x) { return x < 0.0f ? 0.0f : x > 1.0f ? 1.0f : x; }

/* p through view then projection; 1 when in front and on the picture. */
static int tag_proj(const float vw[16], const float pr[16], const float p[3], float k, float *u, float *v,
                    float *w)
{
    float x[4] = { p[0], p[1], p[2], 1.0f }, e[4], c[4];
    int i, j;
    for (j = 0; j < 4; j++)
        for (e[j] = 0.0f, i = 0; i < 4; i++)
            e[j] += x[i] * vw[4 * i + j];
    for (j = 0; j < 4; j++)
        for (c[j] = 0.0f, i = 0; i < 4; i++)
            c[j] += e[i] * pr[4 * i + j];
    if (c[3] < 0.5f)
        return 0;                               /* behind the camera, or at it */
    *u = 0.5f + 0.5f * c[0] / c[3] / k;
    *v = 0.5f - 0.5f * c[1] / c[3];
    *w = c[3];
    return *u > -0.02f && *u < 1.02f && *v > -0.02f && *v < 1.02f;
}

/* The tag over vehicle veh, as the arrow is placed, appended to the box. */
static void tag_add(int *n, const float vw[16], const float pr[16], float k, float th, uint32_t veh,
                    const char *name)
{
    uint32_t r, b;
    float p[3], d2 = 0.0f, d, s, fade, top = 0.0f, u, v, w;
    tag_draw *t;
    int i;
    if (*n >= TAG_MAX || !online_ram(r = MEM32(veh + 0xCu)) || !MEM8(r + 0x12Du))
        return;                                 /* not drawn this frame */
    for (i = 0; i < 3; i++) {
        p[i] = MEMF(r + 0xC4u + 4u * i);
        d2 += (MEMF(TAG_CAM_VA + 4u * i) - p[i]) * (MEMF(TAG_CAM_VA + 4u * i) - p[i]);
    }
    d = sqrtf(d2);
    fade = 1.0f - tag01((d - 200.0f) / 100.0f);  /* out 200..300 m, as upstream */
    if (fade <= 0.01f)
        return;
    if (online_ram(b = MEM32(r + 4u)) && online_ram(b = MEM32(b + 8u)) &&
        online_ram(b = MEM32(b + 4u)) && online_ram(b = MEM32(b + 0x1Cu)) &&
        online_ram(b = MEM32(b + 4u)) && online_ram(b = MEM32(b + 0xCu)))
        top = MEMF(b + 0x18u);                  /* the model's bounds top */
    s = ((0.03f - 0.01f * tag01(d / 600.0f)) * d * th + 0.25f) * MEMF(r + 0x278u);
    p[1] += MEMF(r + 0x274u) + top + 0.125f + s;
    if (!tag_proj(vw, pr, p, k, &u, &v, &w))
        return;
    t = &s_tagbox.t[(*n)++];
    t->x = u;
    t->y = v;
    t->alpha = fade;
    snprintf(t->name, sizeof t->name, "%s", name[0] ? name : "Player");
}

/* Remote cars driven now: their vehicles and names, at most max. */
static int tag_cars(uint32_t *veh, const char **name, int max)
{
    int n = 0, k;
    for (k = 1; k < s_nrc && n < max; k++)
        if (roam_rc_driven(&s_rc[k]) && s_rc[k].live && (veh[n] = roam_rc_veh(k)) != 0) {
            name[n++] = s_rc[k].name;
        }
    return n;
}

/* The main view's projection, kept by the sub_002D4AE0 override: the biggest
 * viewport of this frame (mirror and minimap are smaller). */
static float s_tag_vp[16];
static uint32_t s_tag_vp_area, s_tag_vp_t;
static float s_tag_fov = 60.0f;

static void roam_tag_projection(uint32_t vp)
{
    uint32_t area;
    int i;
    if (!roam_running() || !online_ram(vp))
        return;
    area = MEM32(vp + 0x168u) * MEM32(vp + 0x16Cu);
    if (area < s_tag_vp_area && s_tag_vp_t && host_ms() - s_tag_vp_t < 250u)
        return;                                 /* the mirror, this frame */
    s_tag_vp_area = area;
    s_tag_vp_t = host_ms();
    for (i = 0; i < 16; i++)
        s_tag_vp[i] = MEMF(vp + 4u + 4u * i);
    s_tag_fov = MEMF(vp + 0x178u);
    if (!(s_tag_fov > 1.0f && s_tag_fov < 170.0f))
        s_tag_fov = 60.0f;
}

/* Once a frame, after the world update: project every driven remote car. */
static void roam_tag_frame(void)
{
    static int said;
    uint32_t veh[TAG_MAX];
    float vw[16], k = 1.0f, th;
    double now;
    int n = 0, nc, j, i;
    if (!roam_running() || !s_roam_cruise || !s_online || roam_fake()) {
        if (s_tagbox.n) {
            s_tagbox.n = 0;
        }
        return;
    }
    if (!tag_env("MC3_NAMETAGS", 1) || !remix_driving() || !s_tag_vp_t || host_ms() - s_tag_vp_t > 250u) {
        s_tagbox.n = 0;                         /* menus, garage, paused: no names */
        return;
    }
    for (i = 0; i < 16; i++)
        vw[i] = MEMF(TAG_VIEW_VA + 4u * i);
    th = tanf(s_tag_fov * 0.01745f * 0.5f);
    {
        const char *name[TAG_MAX];
        nc = tag_cars(veh, name, TAG_MAX);
        for (j = 0; j < nc; j++)
            tag_add(&n, vw, s_tag_vp, k, th, veh[j], name[j]);
    }
    s_tagbox.n = n;
    if (!said) {
        said = 1;
        fprintf(stderr, "[ROAM] name tags: %d remotes, %d on screen (%s)\n", nc, n, tag_env("MC3_NAMETAGS", 1) ?
                "MC3_NAMETAGS" : "off");
    }
    now = roam_now();
    if (tag_env("MC3_ROAM_TRACE", 0)) {
        static double trace_t;
        if (now >= trace_t) {
            float u = -1, v = -1, w = 0;
            const float *me = s_roam_nhist ? s_roam_hist[(s_roam_nhist - 1) % ROAM_HIST].m : NULL;
            trace_t = now + 1.0;
            if (me)
                tag_proj(vw, s_tag_vp, me + 9, k, &u, &v, &w);
            fprintf(stderr, "[ROAM] tags: our car at %.2f,%.2f depth %.1f m, %d tags\n", u, v, w, n);
        }
    }
}
