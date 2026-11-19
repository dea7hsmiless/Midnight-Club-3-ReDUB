/*
 * touch_pad.c -- an on-screen Xbox pad for touch screens (Android, ADR 0010
 * of mcdub3-nx), player 1 like the keyboard (sdl_keyboard.c).
 *
 * The renderer hands SDL's finger events here (xbox_touchpad_handle), asks
 * for the overlay's picture (xbox_touchpad_image, drawn on the CPU and only
 * redrawn when something changes) and blends it over the frame;
 * xbox_InputGetState adds the pad's state on top of port 0's
 * (xbox_touchpad_state).
 *
 *   stick + d-pad    BACK START    A B X Y  WHITE   (as on the original
 *                                                BLACK  Xbox controller)
 *   bottom left, bottom middle, bottom right: WHITE and BLACK (the
 *   Switch's L / R) two small pills, white and black, tilted along the
 *   diamond's Y-B edge just outside it. LT and RT sit in the upper corners,
 *   where an index finger rests: the pedals, as on an Xbox One pad.
 *
 * The stick and the d-pad are one control: a finger landing inside the ring
 * is the stick, one landing on the ring the d-pad -- the arrow under it, or
 * two at a diagonal, following the finger round. The stick's up and down can
 * also be gas and brake (right / left trigger, as far as pushed), as the
 * keyboard's arrows are; RECOMP_TOUCH_PEDALS=stick keeps that, the default
 * triggers use LT/RT instead. While driving (xbox_keyboard_driving, the
 * title says) stick-Y is left alone, so a race camera on the stick stays put.
 * A finger may slide from one button to another.
 *
 * All one translucent grey; the labels in a stroke font of our own (smooth,
 * sans serif: the few capitals the labels use).
 *
 * Shown while there is a touch screen, no controller on port 0, and the
 * last input was not the keyboard; a touch brings it back. On iOS there is
 * no keyboard or mouse to fall back on, so it is up from the first frame
 * and a paired controller hides it.
 * RECOMP_TOUCH_PAD=-1 auto, 0 off, 1 always (with a mouse acting as a
 * finger: the desktop, to try it); RECOMP_TOUCH_PAD_ALPHA (0.1-1, default
 * 0.55) and RECOMP_TOUCH_PAD_SCALE (0.6-1.6, default 1) for opacity and
 * size; RECOMP_TOUCH_PEDALS=stick (default on touch devices), triggers, bars
 * or sliders;
 * RECOMP_TOUCH_AUTO_GAS=1 holds the throttle while driving;
 * RECOMP_TOUCH_RAMP=<ms> eases the pedals in; RECOMP_TOUCH_LATCH=1 makes a
 * tap on the gas hold it; RECOMP_TOUCH_HAPTICS=0 turns the touch haptics off;
 * RECOMP_TOUCH_DEADZONE=<percent, 0..20> is the stick's rest zone (default 5);
 * MC3_FPS_OVERLAY=1 draws the frame rate at the top of the screen.
 */
#if (!defined(_WIN32) || defined(XBOXRECOMP_SDL_HOST)) && !defined(__SWITCH__)

#include <SDL.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "touch_pad.h"
#include "xinput_xbox.h"

extern int (*xbox_keyboard_driving)(void);      /* sdl_keyboard.c */
#if defined(__IPHONEOS__)
extern void ios_haptic_tick(int strong);        /* src/ios_launcher.m */
#endif

enum { C_NONE = -1, C_STICK, C_DPAD, C_A, C_B, C_X, C_Y, C_WHITE, C_BLACK,
       C_LT, C_RT, C_BACK, C_START, C_COUNT };

enum { D_UP = 1, D_DOWN = 2, D_LEFT = 4, D_RIGHT = 8 };

typedef struct {
    float x, y, w, h;       /* pixels, y down; circles: centre +- w/2 */
    int round;              /* 1 circle, 0 rounded box */
    const char *label;
    float pill_len, pill_th;    /* > 0: a pill tilted along Y-B, this long and thick */
    unsigned char tint;         /* the pill's colour (grey level): WHITE 255, BLACK 0 */
} Ctl;

#define MAX_FINGERS 10
static struct { SDL_FingerID id; int ctl, dir; } s_fing[MAX_FINGERS];
static int s_nfing;

static Ctl s_ctl[C_COUNT];
static int s_sw, s_sh;                  /* the layout's screen size */
static float s_stick_r, s_ring_in, s_ring_out;  /* stick base; the d-pad ring */
static float s_stick_x, s_stick_y;      /* -1..1, y down */
static int s_down[C_COUNT], s_dirs;     /* s_dirs: d-pad arrows held (D_*) */
static volatile int s_shown;            /* 1: visible */
static int s_mode = -2;                 /* RECOMP_TOUCH_PAD: -1 auto, 0, 1 */
static float s_alpha = 0.55f, s_scale = 1.0f;

/* Steering: the stick (default), the accelerometer, or both.
 * RECOMP_TOUCH_STEER=stick|tilt|both, RECOMP_TOUCH_STEER_SENS=0.5..2. */
enum { STEER_STICK, STEER_TILT, STEER_BOTH, STEER_COUNT };
static int s_steer_mode = STEER_STICK;
static float s_steer_sens = 1.0f;
static int s_tilt_invert;
static float s_tilt, s_tilt_zero;
static float s_tilt_range = 0.45f, s_tilt_dead = 0.06f;
static int s_tilt_cal_samples;
static float s_tilt_cal_sum;
static Uint32 s_tilt_last_ms;
static int s_tilt_was_driving;
static int s_tilt_auto_recenter = 1;
static SDL_Sensor *s_accel;
static int s_accel_tried;
static SDL_DisplayOrientation s_tilt_orient = SDL_ORIENTATION_UNKNOWN;

static void tilt_open(void);

/* Where the pedals are (RECOMP_TOUCH_PEDALS). */
enum { PED_TRIGGERS, PED_BARS, PED_STICK, PED_SLIDERS, PED_COUNT };
static int s_pedal_mode = PED_STICK;   /* iOS default: stick is the joystick */
static int s_auto_gas;                  /* RECOMP_TOUCH_AUTO_GAS=1 */
static int s_ramp_ms;                   /* RECOMP_TOUCH_RAMP, 0 off */
static int s_latch;                     /* RECOMP_TOUCH_LATCH: tap to hold gas */
static int s_stick_haptics = 1, s_stick_band;
static int s_haptics = 1;                   /* RECOMP_TOUCH_HAPTICS (iOS default on) */
static int s_fps_on;                    /* MC3_FPS_OVERLAY: draw the frame rate */
static float s_deadzone = 0.05f;        /* RECOMP_TOUCH_DEADZONE, fraction */
static float s_lt_val, s_rt_val;        /* the edge sliders, 0..1 */
static int s_latch_rt;                  /* tap-to-latch: gas held */
#if defined(__IPHONEOS__) || defined(__ANDROID__)
static int s_band_rt, s_band_lt;        /* last stick-pedal detents (haptics) */
#endif
static float s_lt_out, s_rt_out;        /* ramped pedal travel */
static Uint32 s_ramp_last;              /* the previous ramp step, ms */
static int s_keyboard_last, s_controller, s_touched;
static int s_dirty = 1;
static void pad_haptic(int strong);

/* Pad state, read by the USB thread: written whole under this lock. */
static SDL_SpinLock s_lock;
static struct { WORD buttons; BYTE analog[8]; SHORT lx, ly; } s_pad;

static void settings(void)
{
    const char *e;
    if (s_mode != -2)
        return;
    e = getenv("RECOMP_TOUCH_PAD");
#if defined(__ANDROID__) || defined(__IPHONEOS__)
    /* A phone or tablet is the controller until a pad is paired: auto. */
    s_mode = e && *e == '0' ? 0 : e && *e == '1' ? 1 : -1;
#else
    s_mode = e && *e == '1' ? 1 : 0;
    if (s_mode == 1)
        SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "1");  /* the mouse as a finger */
#endif
    if ((e = getenv("RECOMP_TOUCH_PAD_ALPHA")) != NULL) {
        float a = (float)atof(e);
        s_alpha = a < 0.1f ? 0.1f : a > 1.0f ? 1.0f : a;
    }
    if ((e = getenv("RECOMP_TOUCH_PAD_SCALE")) != NULL) {
        float k = (float)atof(e);
        s_scale = k < 0.6f ? 0.6f : k > 1.6f ? 1.6f : k;
    }
    if ((e = getenv("RECOMP_TOUCH_PEDALS")) != NULL && *e) {
        if (!strncmp(e, "stick", 5))        s_pedal_mode = PED_STICK;
        else if (!strncmp(e, "bars", 4))    s_pedal_mode = PED_BARS;
        else if (!strncmp(e, "sliders", 7)) s_pedal_mode = PED_SLIDERS;
        else                                s_pedal_mode = PED_TRIGGERS;
    }
    if ((e = getenv("RECOMP_TOUCH_AUTO_GAS")) != NULL)
        s_auto_gas = e[0] != '0' && e[0] != '\0';
    if ((e = getenv("RECOMP_TOUCH_RAMP")) != NULL) {
        s_ramp_ms = atoi(e);
        if (s_ramp_ms < 0) s_ramp_ms = 0;
        if (s_ramp_ms > 500) s_ramp_ms = 500;
    }
    if ((e = getenv("RECOMP_TOUCH_LATCH")) != NULL)
        s_latch = e[0] != '0' && e[0] != '\0';
#if defined(__IPHONEOS__)
    s_haptics = 1;
#endif
    if ((e = getenv("RECOMP_TOUCH_HAPTICS")) != NULL)
        s_haptics = e[0] != '0' && e[0] != '\0';
    if ((e = getenv("RECOMP_TOUCH_STICK_HAPTICS")) != NULL)
        s_stick_haptics = e[0] != '0' && e[0] != '\0';
    if ((e = getenv("RECOMP_TOUCH_TILT_INVERT")) != NULL)
        s_tilt_invert = e[0] != '0' && e[0] != '\0';
    if ((e = getenv("RECOMP_TOUCH_TILT_RANGE")) != NULL) {
        float r = (float)atof(e);                   /* g at full lock */
        s_tilt_range = r < 0.20f ? 0.20f : r > 0.90f ? 0.90f : r;
    }
    if ((e = getenv("RECOMP_TOUCH_TILT_DEADZONE")) != NULL) {
        float d = (float)atof(e);                   /* g at centre */
        s_tilt_dead = d < 0.02f ? 0.02f : d > 0.20f ? 0.20f : d;
    }
    if (s_tilt_dead > s_tilt_range - 0.05f)
        s_tilt_dead = s_tilt_range - 0.05f;
    if ((e = getenv("RECOMP_TOUCH_TILT_AUTO_RECENTER")) != NULL)
        s_tilt_auto_recenter = e[0] != '0' && e[0] != '\0';
    if ((e = getenv("MC3_FPS_OVERLAY")) != NULL)
        s_fps_on = e[0] != '0' && e[0] != '\0';
    if ((e = getenv("RECOMP_TOUCH_DEADZONE")) != NULL) {
        float d = (float)atof(e);                   /* percent, 0..20 */
        s_deadzone = (d < 0.0f ? 0.0f : d > 20.0f ? 20.0f : d) / 100.0f;
    }
    if ((e = getenv("RECOMP_TOUCH_STEER")) != NULL && *e) {
        if (!strncmp(e, "tilt", 4))       s_steer_mode = STEER_TILT;
        else if (!strncmp(e, "both", 4))  s_steer_mode = STEER_BOTH;
        else                              s_steer_mode = STEER_STICK;
    }
    if ((e = getenv("RECOMP_TOUCH_STEER_SENS")) != NULL) {
        float k = (float)atof(e);
        s_steer_sens = k < 0.5f ? 0.5f : k > 2.0f ? 2.0f : k;
    }
    if (s_steer_mode != STEER_STICK)
        tilt_open();
    {
        /* Once, after everything is parsed: the device log then says what
         * the pad actually uses. */
        static const char *pedal_name[PED_COUNT] = { "triggers", "bars", "stick", "sliders" };
        static int logged;
        if (!logged) {
            logged = 1;
            fprintf(stderr, "  [INPUT] on-screen pad: %s, pedals %s%s%s%s%s "
                    "(RECOMP_TOUCH_PAD)\n",
                    s_mode == 1 ? "always" : s_mode == 0 ? "off" : "auto",
                    pedal_name[s_pedal_mode],
                    s_auto_gas ? ", auto-gas" : "",
                    s_ramp_ms ? ", ramp" : "",
                    s_latch ? ", latch" : "",
                    s_fps_on ? ", fps" : "");
        }
    }
}

static void set_ctl(int i, float cx, float cy, float w, float h, int round, const char *label)
{
    Ctl *c = &s_ctl[i];
    c->x = cx - w / 2; c->y = cy - h / 2; c->w = w; c->h = h; c->round = round;
    c->label = label;
    c->pill_len = c->pill_th = 0;
}

/* Sizes from the screen's height (u), so a phone and a tablet look alike. */
static void layout(int sw, int sh)
{
    float u = (float)sh * s_scale, W = (float)sw, H = (float)sh;
#if defined(__IPHONEOS__)
    /* The home-indicator swipe area overlaps the pad's bottom row; 8% of the
     * height keeps the stick and BACK/START clear of it. */
    float m = 0.08f * u;
#else
    float m = 0.05f * u;                                /* margin */
#endif
    float br = 0.13f * u, d = 0.115f * u;               /* A B X Y */
    /* WHITE and BLACK: two pills tilted along Y-B, side by side just
     * outside that edge of the diamond, the same gap (g) from Y and B and
     * from each other as between the round buttons; colours, no text.
     * Each as long as a circle is wide: together they span the Y-B row
     * from end to end, WHITE beside Y, BLACK beside B. */
    const float r2 = 0.70710678f;
    float g = 1.41421356f * d - br, pl = br, pt = 0.058f * u;
    float off = br / 2 + g + pt / 2, half = pl / 2 + g / 2;
    float mx = 0.5f * d + off * r2, my = -0.5f * d - off * r2;
    float wx = mx - half * r2, wy = my - half * r2, kx = mx + half * r2, ky = my + half * r2;
    float ext = (pl - pt) / 2 * r2 + pt / 2;            /* a tilted pill's half width */
    float ax = W - m - kx - ext, ay = H - m - d - br / 2;
    float pw = 0.19f * u, ph = 0.075f * u, by = H - m - ph / 2;
    float sx, sy, band, reach;
    s_stick_r = 0.15f * u;
    s_ring_in = s_stick_r * 1.06f;                      /* the d-pad ring */
    s_ring_out = s_stick_r + 0.12f * u;
    /* The same margin m to what can be seen on both sides: the d-pad's
     * arrow tips (draw: chevron, its stroke) as the buttons' edges -- the
     * ring's faint band reaches a little further and may go under it. */
    band = s_ring_out - s_ring_in;
    reach = (s_ring_in + s_ring_out) / 2 + 0.35f * band * 0.42f + band * 0.065f;
    sx = m + reach;
    sy = H - m - reach;
    s_sw = sw; s_sh = sh;
    set_ctl(C_STICK, sx, sy, 2 * s_stick_r, 2 * s_stick_r, 1, NULL);
    set_ctl(C_DPAD, sx, sy, 2 * s_ring_out, 2 * s_ring_out, 1, NULL);
    set_ctl(C_A, ax, ay + d, br, br, 1, "A");
    set_ctl(C_B, ax + d, ay, br, br, 1, "B");
    set_ctl(C_X, ax - d, ay, br, br, 1, "X");
    set_ctl(C_Y, ax, ay - d, br, br, 1, "Y");
    set_ctl(C_WHITE, ax + wx, ay + wy, pl, pl, 1, NULL);  /* hit: a circle round it */
    set_ctl(C_BLACK, ax + kx, ay + ky, pl, pl, 1, NULL);
    s_ctl[C_WHITE].pill_len = s_ctl[C_BLACK].pill_len = pl;
    s_ctl[C_WHITE].pill_th = s_ctl[C_BLACK].pill_th = pt;
    s_ctl[C_WHITE].tint = 255;
    s_ctl[C_BLACK].tint = 0;
    /* BACK and START at the bottom, between the stick and the buttons */
    {
        float mid = (sx + s_ring_out + ax - d - br / 2) / 2;
        set_ctl(C_BACK, mid - 0.02f * u - pw / 2, by, pw, ph, 0, "BACK");
        set_ctl(C_START, mid + 0.02f * u + pw / 2, by, pw, ph, 0, "START");
    }
    /* The pedals, in the shape the settings chose (RECOMP_TOUCH_PEDALS).
     * triggers: a button in each upper corner, where an index finger rests
     * on an Xbox pad's RT/LT. bars: the whole upper width as two shoulder
     * bars. sliders: a vertical strip on each edge, dragged for any travel
     * between 0 and full. stick: no pedal controls at all -- stick-Y is
     * the throttle (see publish). */
    s_ctl[C_LT].w = s_ctl[C_RT].w = 0;
    if (s_pedal_mode == PED_TRIGGERS) {
        float tw = 0.20f * u, th = 0.105f * u, ty = 0.12f * u;
        set_ctl(C_LT, m + tw / 2, ty + th / 2, tw, th, 0, "LT");
        set_ctl(C_RT, W - m - tw / 2, ty + th / 2, tw, th, 0, "RT");
    } else if (s_pedal_mode == PED_BARS) {
        float tw = (W - 2 * m - 0.05f * u) / 2, th = 0.085f * u;
        set_ctl(C_LT, m + tw / 2, m + th / 2, tw, th, 0, "LT");
        set_ctl(C_RT, W - m - tw / 2, m + th / 2, tw, th, 0, "RT");
    } else if (s_pedal_mode == PED_SLIDERS) {
        /* Upper third of each edge: the middle of a phone's side edge is
         * where the notch/dynamic island sits in landscape. */
        float tw = 0.085f * u, th = 0.30f * u, cy = H * 0.30f;
        set_ctl(C_LT, m + tw / 2, cy, tw, th, 0, "LT");
        set_ctl(C_RT, W - m - tw / 2, cy, tw, th, 0, "RT");
    }
    s_dirty = 1;
}

/* The d-pad arrows at (x, y) on the ring: eight sectors, the diagonals
 * pressing two arrows. */
static int ring_dirs(float x, float y)
{
    float cx = s_ctl[C_STICK].x + s_stick_r, cy = s_ctl[C_STICK].y + s_stick_r;
    float a = atan2f(y - cy, x - cx) * 180.0f / 3.14159265f;   /* 0 right, 90 down */
    int sector = (int)floorf((a + 22.5f + 360.0f) / 45.0f) % 8;
    static const int dirs[8] = { D_RIGHT, D_RIGHT | D_DOWN, D_DOWN, D_DOWN | D_LEFT,
                                 D_LEFT, D_LEFT | D_UP, D_UP, D_UP | D_RIGHT };
    return dirs[sector];
}

/* The control under (x, y), pixels; generous edges. */
static int hit(float x, float y)
{
    int i, best = C_NONE;
    float bestd = 1e30f;
    {   /* the stick and its d-pad ring first */
        float cx = s_ctl[C_STICK].x + s_stick_r, cy = s_ctl[C_STICK].y + s_stick_r;
        float dd = sqrtf((x - cx) * (x - cx) + (y - cy) * (y - cy));
        if (dd <= s_ring_in)
            return C_STICK;
        if (dd <= s_ring_out * 1.12f)
            return C_DPAD;
    }
    for (i = C_A; i < C_COUNT; i++) {
        const Ctl *c = &s_ctl[i];
        float cx, cy, dx, dy, dd;
        if (c->w <= 0 || c->h <= 0)
            continue;                       /* not laid out in this pedal mode */
        cx = c->x + c->w / 2; cy = c->y + c->h / 2;
        dx = fabsf(x - cx) / (c->w / 2 * 1.18f); dy = fabsf(y - cy) / (c->h / 2 * 1.18f);
        dd = c->round ? dx * dx + dy * dy : (dx > dy ? dx : dy);
        if (dd <= 1.0f && dd < bestd) {
            bestd = dd;
            best = i;
        }
    }
    return best;
}

#define TILT_CAL_SAMPLES 8

/* Start a new neutral. Called when the sensor opens, when the display is
 * rotated, when the player asks, and when a race begins. */
static void tilt_recenter(void)
{
    s_tilt_cal_samples = 0;
    s_tilt_cal_sum = 0.0f;
    s_tilt_zero = 0.0f;
    s_tilt = 0.0f;
    s_tilt_last_ms = SDL_GetTicks();
}

/* The accelerometer is SDL's sensor API: CoreMotion on iOS, SensorManager
 * on Android. No sensor (or a failed open) leaves the stick in charge. */
static void tilt_open(void)
{
    int i, n;
    if (s_accel_tried || s_steer_mode == STEER_STICK)
        return;
    s_accel_tried = 1;
    if (!SDL_WasInit(SDL_INIT_SENSOR) && SDL_InitSubSystem(SDL_INIT_SENSOR) != 0) {
        fprintf(stderr, "  [INPUT] tilt steering: sensor init failed (%s)\n", SDL_GetError());
        s_steer_mode = STEER_STICK;
        return;
    }
    n = SDL_NumSensors();
    for (i = 0; i < n; i++) {
        if (SDL_SensorGetDeviceType(i) != SDL_SENSOR_ACCEL)
            continue;
        if ((s_accel = SDL_SensorOpen(i)) != NULL) {
            fprintf(stderr, "  [INPUT] tilt steering: %s\n", SDL_SensorGetDeviceName(i));
            tilt_recenter();
            return;
        }
    }
    fprintf(stderr, "  [INPUT] tilt steering: no accelerometer, using the stick\n");
    s_steer_mode = STEER_STICK;
}

/* Gravity projected onto the screen's horizontal axis, in g. That is the
 * left/right roll in landscape. The neutral is an average of valid gravity
 * samples, never the first (often zero/stale) reading. */
static float tilt_read(void)
{
    float d[3], v, mag;
    SDL_DisplayOrientation o;
    Uint32 now, dt;
    if (!s_accel || SDL_SensorGetData(s_accel, d, 3) != 0)
        return s_tilt;
    mag = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (mag < 5.0f || mag > 15.0f)
        return s_tilt;                              /* not a gravity sample */
    o = SDL_GetDisplayOrientation(0);
    if (o == SDL_ORIENTATION_UNKNOWN)
        o = SDL_ORIENTATION_LANDSCAPE;
    if (o != s_tilt_orient) {
        s_tilt_orient = o;
        tilt_recenter();
    }
    switch (o) {
    case SDL_ORIENTATION_PORTRAIT:          v = -d[0]; break;
    case SDL_ORIENTATION_PORTRAIT_FLIPPED:  v =  d[0]; break;
    case SDL_ORIENTATION_LANDSCAPE_FLIPPED: v = -d[1]; break;
    case SDL_ORIENTATION_LANDSCAPE:
    default:                                v =  d[1]; break;
    }
    v /= 9.80665f;
    if (s_tilt_cal_samples < TILT_CAL_SAMPLES) {
        s_tilt_cal_sum += v;
        s_tilt_cal_samples++;
        if (s_tilt_cal_samples == TILT_CAL_SAMPLES)
            s_tilt_zero = s_tilt_cal_sum / (float)TILT_CAL_SAMPLES;
        return 0.0f;
    }
    v -= s_tilt_zero;
    if (s_tilt_invert)
        v = -v;
    if (v > 0.0f)
        v = v <= s_tilt_dead ? 0.0f
                            : (v - s_tilt_dead) / (s_tilt_range - s_tilt_dead);
    else
        v = -v <= s_tilt_dead ? 0.0f
                             : -((-v - s_tilt_dead) / (s_tilt_range - s_tilt_dead));
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    now = SDL_GetTicks();
    dt = now - s_tilt_last_ms;
    s_tilt_last_ms = now;
    if (!dt || dt > 100) dt = 16;
    {
        /* 110 ms rather than 40: hand tremor and phone shake reached the
         * wheels with the lighter filter, which felt twitchy rather than
         * responsive (2026-11-18). */
        float a = (float)dt / (float)(dt + 110);
        if (a > 0.35f) a = 0.35f;
        s_tilt += (v - s_tilt) * a;
    }
    return s_tilt;
}

/* A little gain at small travel, full lock unchanged. */
static float steer_curve(float v)
{
    float a;
    if (v > 1.0f) v = 1.0f;
    if (v < -1.0f) v = -1.0f;
    if (s_steer_sens == 1.0f || v == 0.0f)
        return v;
    a = powf(fabsf(v), 1.0f / s_steer_sens);
    return v < 0.0f ? -a : a;
}

/* Called from the input poll, so tilt keeps steering while the finger is
 * still (touch events alone would leave it frozen). */
static void tilt_update(void)
{
    float lx, t;
    int driving;
    if (s_steer_mode == STEER_STICK || !s_accel)
        return;
    driving = xbox_keyboard_driving && xbox_keyboard_driving();
    if (driving && !s_tilt_was_driving && s_tilt_auto_recenter)
        tilt_recenter();                            /* race starts: new neutral */
    s_tilt_was_driving = driving;
    t = tilt_read();
    lx = s_down[C_STICK] ? s_stick_x : 0.0f;
    if (s_steer_mode == STEER_TILT)
        lx = t;
    else
        lx += t;
    lx = steer_curve(lx);
    SDL_AtomicLock(&s_lock);
    s_pad.lx = (SHORT)(lx * 32767.0f);
    SDL_AtomicUnlock(&s_lock);
}

static SDL_Haptic *s_haptic;
static int s_haptic_tried;
static void pad_haptic(int strong)
{
#if defined(__IPHONEOS__)
    ios_haptic_tick(strong);
#else
    if (!s_haptic_tried) {
        s_haptic_tried = 1;
        if (!SDL_WasInit(SDL_INIT_HAPTIC) && SDL_InitSubSystem(SDL_INIT_HAPTIC) != 0)
            return;
        if (SDL_NumHaptics() > 0) {
            s_haptic = SDL_HapticOpen(0);
            if (s_haptic && SDL_HapticRumbleInit(s_haptic) != 0) {
                SDL_HapticClose(s_haptic);
                s_haptic = NULL;
            }
        }
    }
    if (s_haptic)
        SDL_HapticRumblePlay(s_haptic, strong ? 0.65f : 0.30f, 45);
#endif
}


static void publish(void)
{
    WORD b = 0;
    BYTE an[8];
    int i;
    float lx = 0.0f, ly = 0.0f;
    memset(an, 0, sizeof an);
    for (i = 0; i < C_COUNT; i++)
        s_down[i] = 0;
    s_dirs = 0;
    for (i = 0; i < s_nfing; i++)
        if (s_fing[i].ctl >= 0) {
            s_down[s_fing[i].ctl] = 1;
            if (s_fing[i].ctl == C_DPAD)
                s_dirs |= s_fing[i].dir;
        }
    /* Labels press the button they name. The 2026-11-04 swap (lettered A
     * pressing the Xbox's B) made the pad's A act as "back" in menus; a
     * phone's right-hand button is the confirm one, as on the Xbox pad. */
    if (s_down[C_A]) an[XBOX_BUTTON_A] = 255;
    if (s_down[C_B]) an[XBOX_BUTTON_B] = 255;
    if (s_down[C_X]) an[XBOX_BUTTON_X] = 255;
    if (s_down[C_Y]) an[XBOX_BUTTON_Y] = 255;
    if (s_down[C_WHITE]) an[XBOX_BUTTON_WHITE] = 255;
    if (s_down[C_BLACK]) an[XBOX_BUTTON_BLACK] = 255;
    if (s_down[C_START]) b |= XBOX_GAMEPAD_START;
    if (s_down[C_BACK]) b |= XBOX_GAMEPAD_BACK;
    if (s_dirs & D_UP) b |= XBOX_GAMEPAD_DPAD_UP;
    if (s_dirs & D_DOWN) b |= XBOX_GAMEPAD_DPAD_DOWN;
    if (s_dirs & D_LEFT) b |= XBOX_GAMEPAD_DPAD_LEFT;
    if (s_dirs & D_RIGHT) b |= XBOX_GAMEPAD_DPAD_RIGHT;
    if (s_down[C_STICK]) {
        lx = s_stick_x;
        ly = s_stick_y;
    }
    if (s_steer_mode != STEER_STICK) {
        float t = tilt_read();
        if (s_steer_mode == STEER_TILT)
            lx = t;
        else
            lx += t;
    }
    lx = steer_curve(lx);
    if (s_pedal_mode == PED_STICK) {
        /* up / down: gas / brake, as far as pushed (a little dead zone) */
        if (ly < -0.15f)
            an[XBOX_BUTTON_RTRIGGER] = (BYTE)(ly <= -0.85f ? 255 : (-ly - 0.15f) / 0.7f * 255.0f);
        if (ly > 0.15f)
            an[XBOX_BUTTON_LTRIGGER] = (BYTE)(ly >= 0.85f ? 255 : (ly - 0.15f) / 0.7f * 255.0f);
    } else if (s_pedal_mode == PED_SLIDERS) {
        an[XBOX_BUTTON_LTRIGGER] = (BYTE)(s_lt_val * 255.0f + 0.5f);
        an[XBOX_BUTTON_RTRIGGER] = (BYTE)(s_rt_val * 255.0f + 0.5f);
    } else {
        /* Buttons or bars: full travel, as a pad's triggers are. */
        if (s_down[C_LT]) an[XBOX_BUTTON_LTRIGGER] = 255;
        if (s_down[C_RT] || s_latch_rt) an[XBOX_BUTTON_RTRIGGER] = 255;
    }
    /* Auto-accelerate: the throttle held, but only while the title is racing
     * (never in a menu, where the trigger might mean something else). */
    if (s_auto_gas && xbox_keyboard_driving && xbox_keyboard_driving())
        an[XBOX_BUTTON_RTRIGGER] = 255;
#if defined(__IPHONEOS__) || defined(__ANDROID__)
    /* Physical touch-stick travel, independent of tilt and pedal layout.
     * Detents on engagement and outer travel; hysteresis avoids chatter. */
    {
        float travel=s_down[C_STICK] ? sqrtf(s_stick_x*s_stick_x+s_stick_y*s_stick_y) : 0.0f;
        int band=travel>=0.85f?2:travel>=0.15f?1:0;
        if(s_stick_band==2 && travel>=0.75f) band=2;
        if(s_stick_band==1 && travel>=0.10f && band==0) band=1;
        if(s_shown && s_haptics && s_stick_haptics && band>s_stick_band)
            pad_haptic(band==2);
        s_stick_band=band;
    }
    /* Touch haptics for every pedal mode: a light click when a pedal engages,
     * a firmer one when it reaches full from part travel. Applies to stick
     * pedals, sliders, and the LT/RT buttons or bars. */
    {
        int rtv = an[XBOX_BUTTON_RTRIGGER], ltv = an[XBOX_BUTTON_LTRIGGER];
        int rb = !rtv ? 0 : (rtv >= 250 ? 2 : 1);
        int lb = !ltv ? 0 : (ltv >= 250 ? 2 : 1);
        int strong, strong_l;
        if (s_band_rt == 2 && rtv >= 230)
            rb = 2;                     /* keep full through thumb jitter */
        if (s_band_lt == 2 && ltv >= 230)
            lb = 2;
        strong = rb == 2 && s_band_rt < 2;
        strong_l = lb == 2 && s_band_lt < 2;
        if (s_shown && s_haptics) {
            if (rb != s_band_rt) { pad_haptic(strong); s_band_rt = rb; }
            if (lb != s_band_lt) { pad_haptic(strong_l); s_band_lt = lb; }
        } else {
            s_band_rt = rb;
            s_band_lt = lb;
        }
    }
#endif
    SDL_AtomicLock(&s_lock);
    s_pad.buttons = b;
    memcpy(s_pad.analog, an, sizeof an);
    s_pad.lx = (SHORT)(lx * 32767.0f);
    s_pad.ly = (SHORT)(-ly * 32767.0f);                /* Xbox: up is + */
    SDL_AtomicUnlock(&s_lock);
    s_dirty = 1;
}

/* In-game actions: the neutral follows the phone, not the app launch pose. */
void xbox_touchpad_recenter_tilt(void)
{
    tilt_recenter();
}

/* Port Options' test: fire the same tick a full pedal uses. */
void xbox_touchpad_test_haptics(void)
{
#if defined(__IPHONEOS__) || defined(__ANDROID__)
    pad_haptic(1);
#endif
}

static void stick_to(float x, float y)
{
    float r = s_stick_r;
    float cx = s_ctl[C_STICK].x + r, cy = s_ctl[C_STICK].y + r;
    float dx = (x - cx) / r, dy = (y - cy) / r, len = sqrtf(dx * dx + dy * dy);
    if (len > 1.0f) {
        dx /= len;
        dy /= len;
        len = 1.0f;
    }
    if (len <= s_deadzone) {
        /* Inside the dead zone the stick is at rest: no twitch from a thumb
         * resting near the centre. Outside it, rescale so full lock is
         * still reached at the ring's edge. */
        dx = dy = 0.0f;
    } else if (s_deadzone > 0.0f) {
        float k = (len - s_deadzone) / (1.0f - s_deadzone);
        dx = dx / len * k;
        dy = dy / len * k;
    }
    s_stick_x = dx;
    s_stick_y = dy;
}

/* An edge slider: the finger's height sets the travel, full at the top. */
static void slider_to(int ctl, float y)
{
    const Ctl *c = &s_ctl[ctl];
    float v = c->h > 0 ? 1.0f - (y - c->y) / c->h : 0.0f;
    v = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
    if (ctl == C_LT)
        s_lt_val = v;
    else
        s_rt_val = v;
}

static int finger(SDL_FingerID id)
{
    int i;
    for (i = 0; i < s_nfing; i++)
        if (s_fing[i].id == id)
            return i;
    return -1;
}

/* iOS has no keyboard or mouse and the player cannot "touch to reveal" the
 * controls they are looking for; the pad belongs on screen from the start. */
static int touch_only_device(void)
{
#if defined(__IPHONEOS__)
    return 1;
#else
    return 0;
#endif
}

static void update_shown(void)
{
    int on = s_mode == 1 || (s_mode == -1 && !s_controller && !s_keyboard_last
                             && (s_touched || touch_only_device()
                                 || SDL_GetNumTouchDevices() > 0));
    if (on != s_shown) {
        s_shown = on;
        s_dirty = 1;
        if (!on) {                      /* nothing stays held when it goes */
            s_nfing = 0;
            s_latch_rt = 0;
            s_lt_val = s_rt_val = 0.0f;
            s_lt_out = s_rt_out = 0.0f;
            s_ramp_last = 0;
            publish();
        }
    }
}

void xbox_touchpad_handle(const SDL_Event *e)
{
    int f;
    float x, y;
    settings();
    if (!s_mode)
        return;
    switch (e->type) {
    case SDL_KEYDOWN:
        if (!s_keyboard_last) {
            s_keyboard_last = 1;
            update_shown();
        }
        return;
    case SDL_FINGERDOWN:
    case SDL_FINGERMOTION:
    case SDL_FINGERUP:
        break;
    default:
        return;
    }
    if (s_keyboard_last || !s_touched) {
        s_keyboard_last = 0;
        s_touched = 1;
        update_shown();
    }
    if (!s_shown || s_sw <= 0)
        return;
    x = e->tfinger.x * (float)s_sw;
    y = e->tfinger.y * (float)s_sh;
    f = finger(e->tfinger.fingerId);
    if (e->type == SDL_FINGERDOWN) {
        if (f < 0) {
            if (s_nfing == MAX_FINGERS)
                return;
            f = s_nfing++;
            s_fing[f].id = e->tfinger.fingerId;
        }
        s_fing[f].ctl = hit(x, y);
        s_fing[f].dir = s_fing[f].ctl == C_DPAD ? ring_dirs(x, y) : 0;
        if (s_fing[f].ctl == C_STICK)
            stick_to(x, y);
        if (s_pedal_mode == PED_SLIDERS
            && (s_fing[f].ctl == C_LT || s_fing[f].ctl == C_RT))
            slider_to(s_fing[f].ctl, y);
        if (s_latch && s_pedal_mode != PED_STICK && s_pedal_mode != PED_SLIDERS
            && s_fing[f].ctl == C_RT)
            s_latch_rt = !s_latch_rt;               /* tap the gas on or off */
        publish();
    } else if (e->type == SDL_FINGERMOTION && f >= 0) {
        if (s_fing[f].ctl == C_STICK) {
            stick_to(x, y);
            publish();
        } else if (s_fing[f].ctl == C_DPAD) {
            int dir = ring_dirs(x, y);              /* round the ring */
            if (dir != s_fing[f].dir) {
                s_fing[f].dir = dir;
                publish();
            }
        } else if (s_pedal_mode == PED_SLIDERS
                   && (s_fing[f].ctl == C_LT || s_fing[f].ctl == C_RT)) {
            slider_to(s_fing[f].ctl, y);            /* drag for more travel */
            publish();
        } else {
            /* slide between buttons (not onto the stick or its ring) */
            int c = hit(x, y);
            if (c != C_STICK && c != C_DPAD && c != s_fing[f].ctl) {
                s_fing[f].ctl = c;
                if (s_pedal_mode == PED_SLIDERS && (c == C_LT || c == C_RT))
                    slider_to(c, y);
                publish();
            }
        }
    } else if (e->type == SDL_FINGERUP && f >= 0) {
        /* A lifted slider springs back to rest; a latched trigger stays. */
        if (s_pedal_mode == PED_SLIDERS) {
            if (s_fing[f].ctl == C_LT) s_lt_val = 0.0f;
            if (s_fing[f].ctl == C_RT) s_rt_val = 0.0f;
        }
        s_fing[f] = s_fing[--s_nfing];
        publish();
    }
}

void xbox_touchpad_controller(int connected)
{
    settings();
    if (!s_mode || connected == s_controller)
        return;
    s_controller = connected;
    update_shown();
}

/* The app went to the background: drop every held control so the game does
 * not come back with a finger still on the gas or the stick. */
void xbox_touchpad_suspend(void)
{
    s_nfing = 0;
    s_latch_rt = 0;
    s_lt_val = s_rt_val = 0.0f;
    s_lt_out = s_rt_out = 0.0f;
    s_ramp_last = 0;
    s_stick_x = s_stick_y = 0.0f;
#if defined(__IPHONEOS__) || defined(__ANDROID__)
    s_band_rt = s_band_lt = s_stick_band = 0;
#endif
    publish();
}

/* The in-game Port Options changed one of the pad settings: re-read the env,
 * rebuild the layout on the next frame, and drop anything held. */
void xbox_touchpad_reload(void)
{
    s_mode = -2;                    /* settings() re-parses on the next call */
    s_sw = s_sh = 0;                /* a fresh layout on the next image */
    s_nfing = 0;
    s_latch_rt = 0;
    s_lt_val = s_rt_val = 0.0f;
    s_lt_out = s_rt_out = 0.0f;
    s_ramp_last = 0;
    s_dirty = 1;
    settings();
    update_shown();
    publish();
}

int xbox_touchpad_shown(void)
{
    settings();
    if (s_mode && !s_controller && !s_keyboard_last && !s_shown)
        update_shown();
    return s_shown;
}

void xbox_touchpad_state(XBOX_INPUT_STATE *st)
{
    int i, v;
    Uint32 now = SDL_GetTicks();
    float step = 0.0f;
    if (!s_shown)
        return;
    tilt_update();
    if (s_ramp_ms > 0) {
        if (!s_ramp_last)
            s_ramp_last = now;
        step = (float)(now - s_ramp_last) / (float)s_ramp_ms;
        if (step > 1.0f)
            step = 1.0f;
    }
    SDL_AtomicLock(&s_lock);
    st->Gamepad.wButtons |= s_pad.buttons;
    for (i = 0; i < 8; i++) {
        BYTE target = s_pad.analog[i], value = target;
        if (i == XBOX_BUTTON_LTRIGGER || i == XBOX_BUTTON_RTRIGGER) {
            float *out = i == XBOX_BUTTON_LTRIGGER ? &s_lt_out : &s_rt_out;
            float want = (float)target / 255.0f;
            if (s_ramp_ms > 0) {
                /* RECOMP_TOUCH_RAMP: the travel climbs (or falls) in steps,
                 * so a press is not an instant 0-to-full jolt. A zero-length
                 * step keeps the last value rather than snapping to target. */
                if (want > *out) {
                    *out += step;
                    if (*out > want) *out = want;
                } else if (want < *out) {
                    *out -= step;
                    if (*out < want) *out = want;
                }
                value = (BYTE)(*out * 255.0f + 0.5f);
            } else {
                *out = want;
            }
        }
        if (value > st->Gamepad.bAnalogButtons[i])
            st->Gamepad.bAnalogButtons[i] = value;
    }
    if (s_ramp_ms > 0)
        s_ramp_last = now;
    v = st->Gamepad.sThumbLX + s_pad.lx;
    st->Gamepad.sThumbLX = (SHORT)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    v = st->Gamepad.sThumbLY
        + (xbox_keyboard_driving && xbox_keyboard_driving() ? 0 : s_pad.ly);
    st->Gamepad.sThumbLY = (SHORT)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    SDL_AtomicUnlock(&s_lock);
}

/* ── The picture ─────────────────────────────────────────────────────── */

static uint32_t *s_img;
static int s_iw, s_ih;
static float s_k;                       /* image pixels per screen pixel */

/* Source-over in straight alpha: grey level g at coverage a (0..1). */
static void put(int x, int y, unsigned char g, float a)
{
    unsigned char *p;
    float da, oa, v;
    if (x < 0 || y < 0 || x >= s_iw || y >= s_ih || a <= 0.0f)
        return;
    p = (unsigned char *)&s_img[y * s_iw + x];
    da = p[3] / 255.0f;
    oa = a + da * (1.0f - a);
    if (oa <= 0.0f)
        return;
    v = (g * a + p[0] * da * (1.0f - a)) / oa;
    p[0] = p[1] = p[2] = (unsigned char)v;
    p[3] = (unsigned char)(oa * 255.0f);
}

/* A filled circle or rounded box (corner radius rr), anti-aliased; ring > 0
 * draws only an outline that thick. Image pixels. */
static void shape(float x, float y, float w, float h, float rr, float ring,
                  unsigned char g, float a)
{
    int x0 = (int)floorf(x) - 1, y0 = (int)floorf(y) - 1;
    int x1 = (int)ceilf(x + w) + 1, y1 = (int)ceilf(y + h) + 1, px, py;
    float cx = x + w / 2, cy = y + h / 2, hx = w / 2 - rr, hy = h / 2 - rr;
    for (py = y0; py <= y1; py++)
        for (px = x0; px <= x1; px++) {
            float qx = fabsf(px + 0.5f - cx) - hx, qy = fabsf(py + 0.5f - cy) - hy;
            float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
            float in = (qx > qy ? qx : qy);
            float dist = sqrtf(ox * ox + oy * oy) + (in < 0 ? in : 0) - rr;  /* <0 inside */
            float cov = 0.5f - dist;
            if (ring > 0) {
                float cin = 0.5f - (-dist - ring);
                cov = (cov < 1 ? cov : 1) * (1 - (cin < 0 ? 0 : cin > 1 ? 1 : cin));
            }
            if (cov > 0)
                put(px, py, g, a * (cov > 1 ? 1 : cov));
        }
}

/* ── Strokes: the labels' letters and the d-pad's arrows ──
 * Line segments with round ends, anti-aliased by distance. */
#define MAX_SEG 160
static float s_seg[MAX_SEG][4];
static int s_nseg;

static void seg(float x0, float y0, float x1, float y1)
{
    if (s_nseg < MAX_SEG) {
        s_seg[s_nseg][0] = x0; s_seg[s_nseg][1] = y0;
        s_seg[s_nseg][2] = x1; s_seg[s_nseg][3] = y1;
        s_nseg++;
    }
}

/* An elliptic arc as segments, angles in degrees (0 right, 90 down). */
static void arc(float cx, float cy, float rx, float ry, float a0, float a1, float ox, float oy,
                float k)
{
    int i, n = (int)(fabsf(a1 - a0) / 15.0f) + 2;     /* ~15 degrees a segment */
    float px = 0, py = 0;
    for (i = 0; i <= n; i++) {
        float t = (a0 + (a1 - a0) * i / n) * 3.14159265f / 180.0f;
        float x = ox + (cx + rx * cosf(t)) * k, y = oy + (cy + ry * sinf(t)) * k;
        if (i)
            seg(px, py, x, y);
        px = x;
        py = y;
    }
}

/* Draw the queued segments, stroke width sw, grey g at alpha a. */
static void strokes(float sw, unsigned char g, float a)
{
    float x0 = 1e30f, y0 = 1e30f, x1 = -1e30f, y1 = -1e30f, r = sw / 2;
    int i, px, py;
    for (i = 0; i < s_nseg; i++) {
        x0 = fminf(x0, fminf(s_seg[i][0], s_seg[i][2]));
        x1 = fmaxf(x1, fmaxf(s_seg[i][0], s_seg[i][2]));
        y0 = fminf(y0, fminf(s_seg[i][1], s_seg[i][3]));
        y1 = fmaxf(y1, fmaxf(s_seg[i][1], s_seg[i][3]));
    }
    for (py = (int)floorf(y0 - r) - 1; py <= (int)ceilf(y1 + r) + 1; py++)
        for (px = (int)floorf(x0 - r) - 1; px <= (int)ceilf(x1 + r) + 1; px++) {
            float qx = px + 0.5f, qy = py + 0.5f, best = 1e30f;
            for (i = 0; i < s_nseg; i++) {
                float ax = s_seg[i][0], ay = s_seg[i][1];
                float bx = s_seg[i][2] - ax, by = s_seg[i][3] - ay;
                float l2 = bx * bx + by * by;
                float t = l2 > 0 ? ((qx - ax) * bx + (qy - ay) * by) / l2 : 0;
                float dx, dy;
                t = t < 0 ? 0 : t > 1 ? 1 : t;
                dx = qx - (ax + t * bx);
                dy = qy - (ay + t * by);
                best = fminf(best, dx * dx + dy * dy);
            }
            {
                float cov = r + 0.5f - sqrtf(best);
                if (cov > 0)
                    put(px, py, g, a * (cov > 1 ? 1 : cov));
            }
        }
    s_nseg = 0;
}

/* One capital at (ox, oy), height k; its advance (width) in units of k. A
 * geometric sans serif: straight strokes, round bowls. */
static float glyph(char c, float ox, float oy, float k)
{
#define L(a, b, c2, d) seg(ox + (a) * k, oy + (b) * k, ox + (c2) * k, oy + (d) * k)
    switch (c) {
    case 'A': L(0, 1, 0.31f, 0); L(0.31f, 0, 0.62f, 1); L(0.12f, 0.64f, 0.50f, 0.64f); return 0.62f;
    case 'B':
        L(0, 0, 0, 1); L(0, 0, 0.30f, 0); L(0, 0.48f, 0.32f, 0.48f); L(0, 1, 0.33f, 1);
        arc(0.30f, 0.24f, 0.24f, 0.24f, -90, 90, ox, oy, k);
        arc(0.33f, 0.74f, 0.26f, 0.26f, -90, 90, ox, oy, k);
        return 0.59f;
    case 'C': arc(0.36f, 0.5f, 0.36f, 0.5f, -50, -310, ox, oy, k); return 0.66f;
    case 'E': L(0.52f, 0, 0, 0); L(0, 0, 0, 1); L(0, 1, 0.52f, 1); L(0, 0.5f, 0.44f, 0.5f); return 0.52f;
    case 'H': L(0, 0, 0, 1); L(0.56f, 0, 0.56f, 1); L(0, 0.5f, 0.56f, 0.5f); return 0.56f;
    case 'I': L(0, 0, 0, 1); return 0.0f;
    case 'K': L(0, 0, 0, 1); L(0.54f, 0, 0, 0.62f); L(0.19f, 0.42f, 0.58f, 1); return 0.58f;
    case 'L': L(0, 0, 0, 1); L(0, 1, 0.48f, 1); return 0.48f;
    case 'R':
        L(0, 1, 0, 0); L(0, 0, 0.32f, 0); L(0, 0.5f, 0.32f, 0.5f); L(0.26f, 0.5f, 0.56f, 1);
        arc(0.32f, 0.25f, 0.25f, 0.25f, -90, 90, ox, oy, k);
        return 0.57f;
    case 'S':
        arc(0.29f, 0.25f, 0.27f, 0.25f, -30, -270, ox, oy, k);
        arc(0.29f, 0.75f, 0.29f, 0.25f, -90, 150, ox, oy, k);
        return 0.58f;
    case 'T': L(0, 0, 0.6f, 0); L(0.3f, 0, 0.3f, 1); return 0.6f;
    case 'W': L(0, 0, 0.19f, 1); L(0.19f, 1, 0.39f, 0.2f); L(0.39f, 0.2f, 0.59f, 1); L(0.59f, 1, 0.78f, 0); return 0.78f;
    case 'X': L(0, 0, 0.58f, 1); L(0.58f, 0, 0, 1); return 0.58f;
    case 'Y': L(0, 0, 0.31f, 0.52f); L(0.62f, 0, 0.31f, 0.52f); L(0.31f, 0.52f, 0.31f, 1); return 0.62f;
    /* Digits for the frame-rate counter. */
    case '0': L(0, 0, 0.56f, 0); L(0, 1, 0.56f, 1); L(0, 0, 0, 1); L(0.56f, 0, 0.56f, 1); return 0.62f;
    case '1': L(0.40f, 0, 0.40f, 1); return 0.46f;
    case '2': L(0, 0, 0.56f, 0); L(0.56f, 0, 0.56f, 0.5f); L(0.56f, 0.5f, 0, 0.5f);
              L(0, 0.5f, 0, 1); L(0, 1, 0.56f, 1); return 0.62f;
    case '3': L(0, 0, 0.56f, 0); L(0.56f, 0, 0.56f, 1); L(0.56f, 1, 0, 1);
              L(0, 0.5f, 0.56f, 0.5f); return 0.62f;
    case '4': L(0, 0, 0, 0.5f); L(0, 0.5f, 0.56f, 0.5f); L(0.56f, 0, 0.56f, 1); return 0.62f;
    case '5': L(0.56f, 0, 0, 0); L(0, 0, 0, 0.5f); L(0, 0.5f, 0.56f, 0.5f);
              L(0.56f, 0.5f, 0.56f, 1); L(0.56f, 1, 0, 1); return 0.62f;
    case '6': L(0.56f, 0, 0, 0); L(0, 0, 0, 1); L(0, 1, 0.56f, 1);
              L(0.56f, 1, 0.56f, 0.5f); L(0.56f, 0.5f, 0, 0.5f); return 0.62f;
    case '7': L(0, 0, 0.56f, 0); L(0.56f, 0, 0.30f, 1); return 0.62f;
    case '8': L(0, 0, 0.56f, 0); L(0, 1, 0.56f, 1); L(0, 0, 0, 1);
              L(0.56f, 0, 0.56f, 1); L(0, 0.5f, 0.56f, 0.5f); return 0.62f;
    case '9': L(0, 0, 0.56f, 0); L(0, 0, 0, 0.5f); L(0.56f, 0, 0.56f, 1);
              L(0.56f, 1, 0, 1); L(0, 0.5f, 0.56f, 0.5f); return 0.62f;
    default: return 0.5f;
    }
#undef L
}

/* A label centred on (cx, cy), capitals h tall -- smaller if it would be
 * wider than maxw. */
static void text(const char *s, float cx, float cy, float h, float maxw, unsigned char g, float a)
{
    float gap = 0.30f, w = 0, x;
    const char *p;
    for (p = s; *p; p++) {                  /* measure: draw nothing */
        int keep = s_nseg;
        w += glyph(*p, 0, 0, 1) + (p[1] ? gap : 0);
        s_nseg = keep;
    }
    if (w * h > maxw)
        h = maxw / w;
    x = cx - w * h / 2;
    for (p = s; *p; p++)
        x += (glyph(*p, x, cy - h / 2, h) + gap) * h;
    strokes(h * 0.13f, g, a);
}

/* A d-pad arrow: a chevron pointing (dx, dy) from (cx, cy), size s. */
static void chevron(float cx, float cy, float dx, float dy, float s)
{
    float px = -dy, py = dx;                /* across */
    seg(cx - px * s * 0.6f - dx * s * 0.25f, cy - py * s * 0.6f - dy * s * 0.25f,
        cx + dx * s * 0.35f, cy + dy * s * 0.35f);
    seg(cx + dx * s * 0.35f, cy + dy * s * 0.35f,
        cx + px * s * 0.6f - dx * s * 0.25f, cy + py * s * 0.6f - dy * s * 0.25f);
}

static void draw(void)
{
    const unsigned char fill = 110, edge = 215, ink = 245;
    int i;
    for (i = 0; i < C_COUNT; i++) {
        const Ctl *c = &s_ctl[i];
        float x, y, w, h, rr, a;
        int pressed;
        if (c->w <= 0 || c->h <= 0)
            continue;                       /* not laid out in this pedal mode */
        x = c->x * s_k; y = c->y * s_k; w = c->w * s_k; h = c->h * s_k;
        rr = c->round ? w / 2 : h * 0.5f;
        pressed = s_down[i] || (i == C_RT && s_latch_rt);
        a = s_alpha * (pressed ? 0.9f : 0.5f);
        if (i == C_DPAD) {
            /* the ring and its four arrows */
            static const float dir[4][3] = { { 0, -1, D_UP }, { 0, 1, D_DOWN },
                                             { -1, 0, D_LEFT }, { 1, 0, D_RIGHT } };
            float cx = x + w / 2, cy = y + h / 2, rm = (s_ring_in + s_ring_out) / 2 * s_k;
            float band = (s_ring_out - s_ring_in) * s_k;
            int k;
            shape(x, y, w, h, w / 2, band, fill, s_alpha * 0.32f);
            for (k = 0; k < 4; k++) {
                int on = (s_dirs & (int)dir[k][2]) != 0;
                if (on)
                    shape(cx + dir[k][0] * rm - band * 0.42f, cy + dir[k][1] * rm - band * 0.42f,
                          band * 0.84f, band * 0.84f, band * 0.42f, 0, edge, s_alpha * 0.35f);
                chevron(cx + dir[k][0] * rm, cy + dir[k][1] * rm, dir[k][0], dir[k][1], band * 0.42f);
                strokes(band * 0.13f + 1.0f, ink, s_alpha * (on ? 1.0f : 0.7f));
            }
            continue;
        }
        if (i == C_STICK) {
            float kr = w * 0.23f;
            float kx = x + w / 2 + s_stick_x * (w / 2 - kr) * (s_down[i] ? 1 : 0);
            float ky = y + h / 2 + s_stick_y * (h / 2 - kr) * (s_down[i] ? 1 : 0);
            shape(x, y, w, h, rr, 0, fill, s_alpha * 0.4f);
            shape(x, y, w, h, rr, 1.5f * s_k + 1, edge, s_alpha * 0.6f);
            shape(kx - kr, ky - kr, 2 * kr, 2 * kr, kr, 0, edge, s_alpha * (s_down[i] ? 0.9f : 0.6f));
            continue;
        }
        if (c->pill_len > 0) {
            /* a tilted pill: a rim, then the colour inside it */
            float cx = x + w / 2, cy = y + h / 2, r2 = 0.70710678f;
            float hl = (c->pill_len - c->pill_th) / 2 * s_k * r2, th = c->pill_th * s_k;
            seg(cx - hl, cy - hl, cx + hl, cy + hl);
            strokes(th, edge, s_alpha * (pressed ? 0.95f : 0.6f));
            seg(cx - hl, cy - hl, cx + hl, cy + hl);
            strokes(th - 2 * (1.5f * s_k + 1), c->tint,
                    s_alpha * (pressed ? 1.0f : c->tint ? 0.6f : 0.75f));
            continue;
        }
        if (s_pedal_mode == PED_SLIDERS && (i == C_LT || i == C_RT)) {
            /* An edge strip: the track, and the travel filled up from the
             * bottom (gas on the right, brake on the left). */
            float v = i == C_LT ? s_lt_val : s_rt_val;
            shape(x, y, w, h, w * 0.5f, 0, fill, s_alpha * 0.30f);
            shape(x, y, w, h, w * 0.5f, 1.5f * s_k + 1, edge, s_alpha * 0.55f);
            if (v > 0.02f) {
                float fh = (h - 4) * v;
                if (fh > 2)
                    shape(x + 2, y + h - 2 - fh, w - 4, fh, w * 0.38f, 0, edge,
                          s_alpha * 0.75f);
            }
            if (c->label)
                text(c->label, x + w / 2, y + h * 0.085f, h * 0.06f, w * 0.72f,
                     ink, s_alpha < 0.9f ? 0.95f : 1.0f);
            continue;
        }
        shape(x, y, w, h, rr, 0, pressed ? edge : fill, a);
        shape(x, y, w, h, rr, 1.5f * s_k + 1, edge, s_alpha * 0.6f);
        if (c->label)
            text(c->label, x + w / 2, y + h / 2, h * 0.36f, w * 0.72f,
                 ink, s_alpha < 0.9f ? 0.95f : 1.0f);
    }
}

static uint32_t s_fps_t0;
static int s_fps_frames, s_fps_value = -1;

/* The frame rate, top centre: measured from present calls (one per flip). */
static void draw_fps(void)
{
    char buf[8];
    float k = s_k > 0.0f ? s_k : 1.0f;
    float hgt = (float)s_sh * 0.030f * k;
    float cx = (float)s_sw * 0.5f * k;
    float cy = (float)s_sh * 0.055f * k;
    float est;
    if (hgt < 10.0f * k) hgt = 10.0f * k;
    snprintf(buf, sizeof buf, "%d", s_fps_value < 0 ? 0 : s_fps_value);
    est = (float)strlen(buf) * hgt * 0.78f;
    shape(cx - est * 0.5f - hgt * 0.35f, cy - hgt * 0.75f,
          est + hgt * 0.7f, hgt * 1.5f, hgt * 0.4f, 0, 0, 0.35f);
    text(buf, cx, cy, hgt, (float)s_sw * 0.2f * k, 245, 0.95f);
}

static void compose(void)
{
    memset(s_img, 0, (size_t)s_iw * s_ih * 4);
    if (xbox_touchpad_shown())
        draw();
    if (s_fps_on)
        draw_fps();
}

int xbox_touchpad_image(int sw, int sh, const uint32_t **rgba, int *w, int *h)
{
    int shown = xbox_touchpad_shown();
    if ((!shown && !s_fps_on) || sw <= 0 || sh <= 0)
        return -1;
    if (s_fps_on) {
        Uint32 now = SDL_GetTicks();
        if (!s_fps_t0)
            s_fps_t0 = now;
        if (now - s_fps_t0 > 2000) {          /* resumed: do not compute a 0 */
            s_fps_t0 = now;
            s_fps_frames = 0;
        } else {
            s_fps_frames++;
            if (now - s_fps_t0 >= 500) {
                Uint32 span = now - s_fps_t0;
                int v = (int)(((uint64_t)s_fps_frames * 1000u + span / 2) / span);
                if (v != s_fps_value) {
                    s_fps_value = v;
                    s_dirty = 1;
                }
                s_fps_frames = 0;
                s_fps_t0 = now;
            }
        }
    } else if (s_fps_value != -1) {
        s_fps_value = -1;
        s_dirty = 1;
    }
    if (sw != s_sw || sh != s_sh) {
        int iw, ih;
        layout(sw, sh);
        /* At most ~1280 wide: the overlay is soft shapes, linear-filtered. */
        s_k = sw > 1280 ? 1280.0f / (float)sw : 1.0f;
        iw = (int)(sw * s_k + 0.5f);
        ih = (int)(sh * s_k + 0.5f);
        if (iw != s_iw || ih != s_ih) {
            free(s_img);
            s_img = (uint32_t *)malloc((size_t)iw * ih * 4);
            s_iw = s_img ? iw : 0;
            s_ih = s_img ? ih : 0;
        }
    }
    if (!s_img)
        return -1;
    *rgba = s_img;
    *w = s_iw;
    *h = s_ih;
    if (!s_dirty)
        return 0;
    s_dirty = 0;
    compose();
    return 1;
}

#endif
