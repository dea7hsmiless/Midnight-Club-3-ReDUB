/*
 * ios_host.c -- iOS boot glue, settings and host stand-ins.
 *
 * Modeled on android_host.c (ADR 0010): SDL starts the program as SDL_main
 * on a thread of its own, so the folder, settings and log are set up here
 * before the title boots.
 *
 *   folder    the app's writable container: <app>_env.txt beside the
 *             documents folder (NSHomeDirectory), KEY=VALUE per line,
 *             applied with setenv before boot, so every RECOMP_* / MC3_*
 *             switch works without a rebuild
 *   settings  the Port Options menu the title draws in-game: the same
 *             windows_port_setting_change/_label contract the Windows
 *             build implements in windows_settings.c, backed by the env
 *             file instead of a Win32 window (there is no window to
 *             PostMessage; the renderer reads the atomics directly)
 *   log       ring-buffered like the Switch's, drained to the container
 *
 * Also defines the two externs nv2a_pb_replay.c expects from the front end
 * (g_fe_cursor, fe_menu_get_pb_state): the front-end cursor is a guest
 * variable, so on iOS the push-buffer replay derives its menu state from
 * the guest state machine alone until a proper bridge exists.
 */
#ifndef __APPLE__
/* Only built for iOS; keep the file harmless elsewhere. */
void ios_host_unused(void);
void ios_host_unused(void) { }
#else

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <pthread.h>
#include <mach-o/dyld.h>          /* _NSGetExecutablePath: the bundle beside it */
#include <sys/stat.h>
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE

#include "game.h"   /* MC3_APP, MC3_GAME_TITLE, MC3_SD_DIR (config/<game>) */
#ifndef MC3_APP
#define MC3_APP "mcdub3"
#endif
#ifndef MC3_GAME_TITLE
#define MC3_GAME_TITLE "Midnight Club 3 Remix"
#endif
#ifndef MC3_SD_DIR
#define MC3_SD_DIR "ReDUB"
#endif

/* ===================================================================== */
/* Settings -- the Port Options contract                                 */
/* ===================================================================== */

/* Same indices and meaning as windows_settings.c (the title's port_build
 * adds 16 rows with ids 0x7F00.., in its own fixed order). Values are
 * atomics the renderer/settings readers already consult via their env
 * names; on iOS every change is committed to the env file immediately,
 * so a change survives a restart like on Windows. */
static const char *scale_values[] = {"1", "1.5", "2", "3", "5"};
static atomic_int fullscreen, scale_index = 2, swap_interval, fxaa = 1,
                       aniso = 4, sharp = 2, overlay;
static atomic_int gamma_index = 2, brightness_index = 4;
static atomic_int pc_music, music_hud = 1;
static atomic_int frame_rate = 30;
static int music_restore = 100;

/* The in-game Port Options can change the touch pad and vibration live. */
extern void xbox_touchpad_reload(void);
extern void xbox_touchpad_recenter_tilt(void);
extern void xbox_touchpad_test_haptics(void);
extern void xbox_rumble_strength_reset(void);

/* Touch settings live in the env file; the pad reads them there directly. */
static int env_int(const char *key, int fallback)
{
    const char *v = getenv(key);
    return v && *v ? atoi(v) : fallback;
}
static int env_on(const char *key, int fallback)
{
    const char *v = getenv(key);
    return v && *v ? *v != '0' : fallback;
}
static const char *env_pedals(void)
{
    const char *v = getenv("RECOMP_TOUCH_PEDALS");
    return v && *v ? v : "stick";
}

float windows_port_gamma_get(void) { return 0.8f + atomic_load(&gamma_index) * 0.1f; }
float windows_port_brightness_get(void) { return 0.8f + atomic_load(&brightness_index) * 0.05f; }
float windows_port_music_restore(void) { return music_restore / 100.0f; }
void windows_port_music_backup(float volume)
{
    int n = (int)(volume * 100 + 0.5f);
    if (n >= 0 && n <= 100) music_restore = n;
}
int windows_port_pc_music_get(void) { return atomic_load(&pc_music); }
int windows_port_music_hud_get(void) { return atomic_load(&music_hud); }
float windows_port_sharpen_get(void) { return atomic_load(&sharp) * 0.25f; }
int windows_port_fxaa_get(void) { return atomic_load(&fxaa); }
int windows_port_overlay_get(void) { return atomic_load(&overlay); }
int windows_port_vsync_get(void) { return atomic_load(&swap_interval); }

/* ---- env file ---------------------------------------------------------- */

static char env_path[2048];

static int save_env_pair(const char *key, const char *value)
{
    char tmp[2056];
    FILE *out;
    int last = '\n';
    if (!env_path[0]) return 0;
    snprintf(tmp, sizeof tmp, "%s.tmp", env_path);
    out = fopen(tmp, "wb");
    if (!out) return 0;
    {
        char line[4096];
        FILE *in = fopen(env_path, "rb");
        if (in) {
            while (fgets(line, sizeof line, in)) {
                const char *p = line;
                while (*p == ' ' || *p == '\t') p++;
                if (strncmp(p, key, strlen(key)) == 0 && p[strlen(key)] == '=') continue;
                fputs(line, out);
                if (line[0]) last = (unsigned char)line[strlen(line) - 1];
            }
            fclose(in);
        }
    }
    if (last != '\n') fputs("\n", out);
    fprintf(out, "%s=%s\n", key, value);
    /* A full disk or an I/O error must not be renamed over the good file. */
    { int bad = ferror(out); if (fclose(out) != 0 || bad) { remove(tmp); return 0; } }
    /* rename() replaces atomically on Darwin; a failed write never
     * truncates the player's settings. */
    if (rename(tmp, env_path) != 0) { remove(tmp); return 0; }
    setenv((char *)key, (char *)value, 1);   /* the runtime reads env live */
    return 1;
}

static void load_env_file(void)
{
    FILE *in;
    char line[4096];
    if (!env_path[0]) return;
    in = fopen(env_path, "rb");
    if (!in) return;
    while (fgets(line, sizeof line, in)) {
        char *p = line, *eq;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#') continue;
        eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        {
            char *name_end = eq;
            char *value = eq + 1;
            char *eol;
            while (name_end > p && (name_end[-1] == ' ' || name_end[-1] == '\t')) name_end--;
            *name_end = 0;
            eol = value + strlen(value);
            while (eol > value && (eol[-1] == '\n' || eol[-1] == '\r')) *--eol = 0;
            while (*value == ' ' || *value == '\t') value++;
            {
                size_t n = strlen(value);
                if (n >= 2 && (value[0] == '"' || value[0] == '\'') && value[n - 1] == value[0]) {
                    value[n - 1] = 0;
                    value++;
                }
            }
            if (eq != name_end && *p && *value) setenv(p, value, 1);
        }
    }
    fclose(in);
}

/* Apply the env-file values into the atomics, so the menu's indices agree
 * with what actually booted (the renderer also reads the env names). */
static void settings_from_env(void)
{
    const char *v;
    if ((v = getenv("MC3_FULLSCREEN")))   atomic_store(&fullscreen, atoi(v) != 0);
    if ((v = getenv("RECOMP_GL_SCALE"))) {
        for (int i = 0; i < 5; i++)
            if (strcmp(v, scale_values[i]) == 0) atomic_store(&scale_index, i);
    }
    if ((v = getenv("RECOMP_GL_VSYNC")))         atomic_store(&swap_interval, atoi(v));
    if ((v = getenv("RECOMP_GL_FXAA")))           atomic_store(&fxaa, atoi(v));
    if ((v = getenv("RECOMP_GL_ANISO")))         atomic_store(&aniso, atoi(v));
    if ((v = getenv("RECOMP_GL_SHARPEN")))        atomic_store(&sharp, (int)(atof(v)*4+0.5));
    if ((v = getenv("MC3_FPS_OVERLAY")))   atomic_store(&overlay, atoi(v));
    if ((v = getenv("MC3_PC_MUSIC")))       atomic_store(&pc_music, atoi(v));
    if ((v = getenv("MC3_PC_MUSIC_HUD")))   atomic_store(&music_hud, atoi(v));
    if ((v = getenv("MC3_GAMMA")))         atomic_store(&gamma_index, (int)((atof(v) - 0.8f) * 10 + 0.5f));
    if ((v = getenv("MC3_BRIGHTNESS")))    atomic_store(&brightness_index, (int)((atof(v) - 0.8f) * 20 + 0.5f));
    if ((v = getenv("MC3_FRAME_RATE")))    atomic_store(&frame_rate, atoi(v) >= 60 ? 60 : 30);
}

void ios_settings_refresh(void) { settings_from_env(); }

/* Online is on unless the player switched it off.
 *
 * MC3_ONLINE=0 is that switch, written by the launcher row and by Port
 * Options; it clears the address and the System Link transport whatever the
 * file also says. With no MC3_ONLINE at all the default room is used, so a
 * fresh install (and an existing one that never touched the switch) is
 * online without anyone editing the settings file. */
#ifndef MC3_ONLINE_SERVER
#  define MC3_ONLINE_SERVER "162.35.56.66"
#endif
static void apply_online_default(void)
{
    const char *off = getenv("MC3_ONLINE");
    if (off && off[0] == '0') {
        unsetenv("MC3_ROAM_JOIN");
        unsetenv("MC3_NETPLAY");
        fprintf(stderr, "[IOS] online: off (MC3_ONLINE=0)\n");
        return;
    }
    if (!getenv("MC3_ROAM_JOIN"))
        setenv("MC3_ROAM_JOIN", MC3_ONLINE_SERVER, 1);
    if (!getenv("MC3_NETPLAY"))
        setenv("MC3_NETPLAY", "1", 1);
}

/* The title calls this with the row id (event-0x7F00) from its own UI
 * thread; there is no window to bounce through, so commit directly. */
/* The room and Find a Room rows are shared code (src/games/remix/recomp_manual.c),
 * so every platform gets the same behaviour: a live room change, and a query
 * to the relay for the fullest room with a free slot. */
extern int port_online_label(int id, char *out, size_t size);
extern int port_online_change(int id, int step);

/* The shared online rows save the room number through the same env file. */
int windows_port_save_nickname(const char *name)
{
    return save_env_pair("MC3_ROAM_NAME", name);
}

int windows_port_save_room(int room)
{
    char v[16];
    snprintf(v, sizeof v, "%d", room);
    return save_env_pair("MC3_ROAM_ROOM", v);
}

void windows_port_setting_change(int id)
{
    char v[16], vbuf[16];
    if (port_online_change(id, 0)) return;
    switch (id) {
    case 0: {  /* on-screen controls: auto -> always -> off -> auto */
        int m = env_int("RECOMP_TOUCH_PAD", -1);
        const char *nv = m == -1 ? "1" : m == 1 ? "0" : "-1";
        if (save_env_pair("RECOMP_TOUCH_PAD", nv)) xbox_touchpad_reload();
    } return;
    case 1: {  /* pedals */
        const char *p = env_pedals(), *nv;
        if (!strncmp(p, "triggers", 8))      nv = "bars";
        else if (!strncmp(p, "bars", 4))     nv = "stick";
        else if (!strncmp(p, "stick", 5))    nv = "sliders";
        else                                 nv = "triggers";
        if (save_env_pair("RECOMP_TOUCH_PEDALS", nv)) xbox_touchpad_reload();
    } return;
    case 2: {int n = (atomic_load(&scale_index) + 1) % 5; atomic_store(&scale_index, n);
             save_env_pair("RECOMP_GL_SCALE", scale_values[n]);} return;
    case 3: {int n = (atomic_load(&fxaa) + 1) % 4; atomic_store(&fxaa, n);
             snprintf(v, sizeof v, "%d", n); save_env_pair("RECOMP_GL_FXAA", v);} return;
    case 4: {int n = atomic_load(&aniso); n = n ? (n >= 16 ? 1 : n * 2) : 1; atomic_store(&aniso, n);
             snprintf(v, sizeof v, "%d", n); save_env_pair("RECOMP_GL_ANISO", v);} return;
    case 5: {  /* steering dead zone, percent */
        static const int pct[] = {0, 3, 5, 8, 12};
        int cur = env_int("RECOMP_TOUCH_DEADZONE", 5), i, n;
        for (i = 0; i < 5 && pct[i] != cur; i++) { }
        n = pct[(i + 1) % 5];
        snprintf(vbuf, sizeof vbuf, "%d", n);
        if (save_env_pair("RECOMP_TOUCH_DEADZONE", vbuf)) xbox_touchpad_reload();
    } return;
    case 6: {int n = (atomic_load(&sharp) + 1) % 5; atomic_store(&sharp, n);
             snprintf(v, sizeof v, "%.2f", n * 0.25); save_env_pair("RECOMP_GL_SHARPEN", v);} return;
    case 7: {  /* throttle ramp */
        static const int ms[] = {0, 150, 300};
        int cur = env_int("RECOMP_TOUCH_RAMP", 0), i, n;
        for (i = 0; i < 3 && ms[i] != cur; i++) { }
        n = ms[(i + 1) % 3];
        snprintf(vbuf, sizeof vbuf, "%d", n);
        if (save_env_pair("RECOMP_TOUCH_RAMP", vbuf)) xbox_touchpad_reload();
    } return;
    case 8: {int n = !env_on("RECOMP_TOUCH_LATCH", 0);
             if (save_env_pair("RECOMP_TOUCH_LATCH", n ? "1" : "0")) xbox_touchpad_reload();} return;
    case 9: {int n = !env_on("RECOMP_TOUCH_AUTO_GAS", 0);
             if (save_env_pair("RECOMP_TOUCH_AUTO_GAS", n ? "1" : "0")) xbox_touchpad_reload();} return;
    case 10:{int n = !env_on("RECOMP_TOUCH_HAPTICS", 1);
             if (save_env_pair("RECOMP_TOUCH_HAPTICS", n ? "1" : "0")) xbox_touchpad_reload();} return;
    case 11:{  /* vibration strength */
        static const int st[] = {0, 50, 75, 100, 125, 150};
        int cur = env_int("MC3_RUMBLE_STRENGTH", 100), i, n;
        for (i = 0; i < 6 && st[i] != cur; i++) { }
        n = st[(i + 1) % 6];
        snprintf(vbuf, sizeof vbuf, "%d", n);
        if (save_env_pair("MC3_RUMBLE_STRENGTH", vbuf)) xbox_rumble_strength_reset();
    } return;
    case 12: {  /* display shape: auto -> 4:3 -> stretch -> auto */
        const char *cur = getenv("RECOMP_GL_ASPECT");
        const char *nv = (!cur || !*cur || !strcmp(cur, "auto")) ? "4:3"
                       : !strcmp(cur, "4:3") ? "fill" : "auto";
        save_env_pair("RECOMP_GL_ASPECT", nv);   /* nv2a reads it at present init */
    } return;
    case 13:{int n = (atomic_load(&gamma_index) + 1) % 7;
             snprintf(v, sizeof v, "%.1f", 0.8f + n * 0.1f);
             if (save_env_pair("MC3_GAMMA", v)) atomic_store(&gamma_index, n);} return;
    case 14:{int n = (atomic_load(&brightness_index) + 1) % 9;
             snprintf(v, sizeof v, "%.2f", 0.8f + n * 0.05f);
             if (save_env_pair("MC3_BRIGHTNESS", v)) atomic_store(&brightness_index, n);} return;
    case 16:{int n = atomic_load(&frame_rate) >= 60 ? 30 : 60;
             snprintf(v, sizeof v, "%d", n);
             if (save_env_pair("MC3_FRAME_RATE", v)) {
                 atomic_store(&frame_rate, n);
                 fprintf(stderr, "[PORT] frame rate %d saved (restart to apply)\n", n);
             }} return;
    case 17: xbox_touchpad_recenter_tilt(); return;
    case 18: xbox_touchpad_test_haptics(); return;
    case 19:{
        const char *cur = getenv("RECOMP_TOUCH_STEER");
        const char *nv = (!cur || !*cur || !strcmp(cur, "stick")) ? "tilt"
                       : !strcmp(cur, "tilt") ? "both" : "stick";
        if (save_env_pair("RECOMP_TOUCH_STEER", nv)) xbox_touchpad_reload();
    } return;
    case 20:{
        static const char *sv[] = {"1", "1.35", "1.7", "2"};
        const char *cur = getenv("RECOMP_TOUCH_STEER_SENS");
        int i, n = 0;
        for (i = 0; i < 4; i++) if (cur && !strcmp(cur, sv[i])) n = i;
        n = (n + 1) % 4;
        if (save_env_pair("RECOMP_TOUCH_STEER_SENS", sv[n])) xbox_touchpad_reload();
    } return;
    case 21:{
        int n = !env_on("RECOMP_TOUCH_TILT_INVERT", 0);
        if (save_env_pair("RECOMP_TOUCH_TILT_INVERT", n ? "1" : "0"))
            xbox_touchpad_reload();
    } return;
    case 22:{
        /* Online is on unless the player switched it off (ios_host.c applies
         * the same default at boot). One switch, three keys: the state, the
         * address and the System Link transport. Restart to apply. */
        const char *off = getenv("MC3_ONLINE");
        int on = !(off && off[0] == '0');      /* currently on */
        if (save_env_pair("MC3_ONLINE", on ? "0" : "1")) {
            if (on) {
                save_env_pair("MC3_ROAM_JOIN", "");
                save_env_pair("MC3_NETPLAY", "0");
            } else {
                save_env_pair("MC3_ROAM_JOIN", MC3_ONLINE_SERVER);
                save_env_pair("MC3_NETPLAY", "1");
            }
        }
        fprintf(stderr, "[PORT] online %s saved (restart to apply)\n", on ? "off" : "on");
    } return;
    default: return;
    }
}

void windows_port_setting_label(int id, char *out, size_t size)
{
    if (port_online_label(id, out, size)) return;
    if (out && size) out[0] = 0;   /* unknown id: blank */
    if (id == 0) {
        const char *names[] = {"AUTO", "ALWAYS", "OFF"};
        int m = env_int("RECOMP_TOUCH_PAD", -1);
        snprintf(out, size, "TOUCH CONTROLS: %s", names[m == 1 ? 1 : m == 0 ? 2 : 0]);
    }
    if (id == 1) {
        const char *p = env_pedals();
        const char *n = !strncmp(p, "bars", 4)    ? "SHOULDER BARS"
                      : !strncmp(p, "stick", 5)   ? "STICK UP / DOWN"
                      : !strncmp(p, "sliders", 7) ? "EDGE SLIDERS"
                      :                             "LT / RT BUTTONS";
        snprintf(out, size, "PEDALS: %s", n);
    }
    if (id == 2) snprintf(out, size, "RENDER SCALE: %sX (RESTART)", scale_values[atomic_load(&scale_index)]);
    if (id == 3) {const char *names[] = {"OFF", "FXAA", "FXAA LOW", "FXAA HIGH"};
                  int f = atomic_load(&fxaa); snprintf(out, size, "ANTI-ALIASING: %s", names[f >= 0 && f <= 3 ? f : 1]);}
    if (id == 4) snprintf(out, size, "FILTERING: %dX (RESTART)", atomic_load(&aniso));
    if (id == 5) {
        int d = env_int("RECOMP_TOUCH_DEADZONE", 5);
        if (d == 0) snprintf(out, size, "STEER DEAD ZONE: OFF");
        else        snprintf(out, size, "STEER DEAD ZONE: %d%%", d);
    }
    if (id == 6) snprintf(out, size, "SHARPENING: %d%%", atomic_load(&sharp) * 25);
    if (id == 7) {
        int r = env_int("RECOMP_TOUCH_RAMP", 0);
        snprintf(out, size, "THROTTLE RAMP: %s", r <= 0 ? "OFF" : r <= 150 ? "150 MS" : "300 MS");
    }
    if (id == 8) snprintf(out, size, "LATCH GAS: %s", env_on("RECOMP_TOUCH_LATCH", 0) ? "ON" : "OFF");
    if (id == 9) snprintf(out, size, "AUTO-ACCELERATE: %s", env_on("RECOMP_TOUCH_AUTO_GAS", 0) ? "ON" : "OFF");
    if (id == 10) snprintf(out, size, "TOUCH HAPTICS: %s", env_on("RECOMP_TOUCH_HAPTICS", 1) ? "ON" : "OFF");
    if (id == 11) snprintf(out, size, "RUMBLE: %d%%", env_int("MC3_RUMBLE_STRENGTH", 100));
    if (id == 12) {
        const char *a = getenv("RECOMP_GL_ASPECT");
        const char *n = a && !strcmp(a, "4:3")  ? "4:3"
                      : a && !strcmp(a, "fill") ? "STRETCH"
                      :                           "AUTO";
        snprintf(out, size, "DISPLAY: %s (RESTART)", n);
    }
    if (id == 13) snprintf(out, size, "GAMMA: %.1f", 0.8f + atomic_load(&gamma_index) * 0.1f);
    if (id == 14) snprintf(out, size, "BRIGHTNESS: %.2f", 0.8f + atomic_load(&brightness_index) * 0.05f);
    if (id == 15) snprintf(out, size, "ADVANCED...");
    if (id == 16) snprintf(out, size, "FRAME RATE: %d FPS (RESTART)",
                           atomic_load(&frame_rate) >= 60 ? 60 : 30);
    if (id == 17) snprintf(out, size, "RECENTER TILT");
    if (id == 18) snprintf(out, size, "TEST HAPTICS");
    if (id == 19) {
        const char *m = getenv("RECOMP_TOUCH_STEER");
        snprintf(out, size, "STEERING: %s",
                 !m || !*m || !strcmp(m, "stick") ? "STICK"
                 : !strcmp(m, "tilt") ? "TILT" : "STICK + TILT");
    }
    if (id == 20) {
        const char *s = getenv("RECOMP_TOUCH_STEER_SENS");
        snprintf(out, size, "STEERING RESPONSE: %s",
                 !s || !strcmp(s, "1") ? "NORMAL"
                 : !strcmp(s, "1.35") ? "QUICK"
                 : !strcmp(s, "1.7") ? "SHARP" : "VERY SHARP");
    }
    if (id == 21)
        snprintf(out, size, "INVERT TILT: %s",
                 env_on("RECOMP_TOUCH_TILT_INVERT", 0) ? "ON" : "OFF");
    if (id == 22) {
        const char *off = getenv("MC3_ONLINE");
        int on = !(off && off[0] == '0');
        snprintf(out, size, "ONLINE: %s (RESTART)", on ? "ON" : "OFF");
    }
}

/* nv2a_pb_replay.c expects these from the front end. The front-end cursor
 * is a guest variable; until a proper bridge exists, a cursor of -1 makes
 * update_menu_title print the menu name only, and the state machine alone
 * decides the menu. */
int g_fe_cursor = -1;
int fe_menu_get_pb_state(void) { return -1; }

/* ===================================================================== */
/* Log -- a small RAM ring drained to the container                       */
/* ===================================================================== */

#define LOG_RING (4 * 1024 * 1024)
static char *s_ring;
static size_t s_head, s_tail;
static pthread_mutex_t s_log_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t s_log_cv = PTHREAD_COND_INITIALIZER;
static int s_log_quit;

static void ring_put(const char *p, size_t len)
{
    pthread_mutex_lock(&s_log_mu);
    for (size_t i = 0; i < len; i++) {
        s_ring[s_head] = p[i];
        s_head = (s_head + 1) % LOG_RING;
        if (s_head == s_tail) s_tail = (s_tail + 1) % LOG_RING;   /* overwrite oldest */
    }
    pthread_cond_signal(&s_log_cv);
    pthread_mutex_unlock(&s_log_mu);
}

static void log_write(const char *p, size_t len)
{
    if (s_ring) ring_put(p, len);
}

static void *log_pump(void *arg)
{
    FILE *out = NULL;
    char path[2048];
    (void)arg;
    if (env_path[0]) {
        snprintf(path, sizeof path, "%s_log.txt", env_path);
        out = fopen(path, "wb");
    }
    while (1) {
        char buf[16384];
        size_t n = 0;
        pthread_mutex_lock(&s_log_mu);
        while (s_tail == s_head && !s_log_quit) pthread_cond_wait(&s_log_cv, &s_log_mu);
        while (s_tail != s_head && n < sizeof buf - 1) {
            buf[n++] = s_ring[s_tail];
            s_tail = (s_tail + 1) % LOG_RING;
        }
        pthread_mutex_unlock(&s_log_mu);
        if (n && out) fwrite(buf, 1, n, out);
        if (!n && s_log_quit) break;
        if (out) fflush(out);
    }
    if (out) fclose(out);
    return NULL;
}

/* ===================================================================== */
/* Boot                                                                   */
/* ===================================================================== */

static void log_open(void)
{
    pthread_t th;
    s_ring = malloc(LOG_RING);
    if (!s_ring) return;
    pthread_create(&th, NULL, log_pump, NULL);
    pthread_detach(th);
}

/* First run: the settings file the app was built with becomes the player's, so
 * a fresh install starts with something to edit instead of an empty folder --
 * and a test build can arrive with the server line already in it. Same shape as
 * windows_host.c's seed_player_settings(); never overwrites an existing file. */
static void seed_env_file(const char *home)
{
    char exe[4096], src[4200], dst[4200], buf[4096];
    uint32_t n = (uint32_t)sizeof exe;
    FILE *in, *out, *probe;
    size_t got;
    char *slash;

    if (!home || !*home) return;
    snprintf(dst, sizeof dst, "%s/Documents/%s_env.txt", home, MC3_APP);
    if ((probe = fopen(dst, "rb")) != NULL) { fclose(probe); return; }
    if (_NSGetExecutablePath(exe, &n) != 0) return;
    if ((slash = strrchr(exe, '/')) == NULL) return;
    *slash = 0;                                  /* the .app bundle */
    snprintf(src, sizeof src, "%s/%s_env.txt", exe, MC3_APP);
    if ((in = fopen(src, "rb")) == NULL) {
        /* No bundled example: create the file anyway, so the player always
         * has somewhere to look and the game always has somewhere to save. */
        if ((out = fopen(dst, "wb")) != NULL) {
            fputs("# " MC3_APP " settings. One NAME=value per line.\n", out);
            fputs("# Online play is on by default; MC3_ONLINE=0 turns it off.\n", out);
            fputs("# The room is picked in game: Options > ROOM.\n", out);
            fclose(out);
            fprintf(stderr, "[IOS] settings: created %s\n", dst);
        }
        return;
    }
    if ((out = fopen(dst, "wb")) == NULL) { fclose(in); return; }
    while ((got = fread(buf, 1, sizeof buf, in)) > 0)
        fwrite(buf, 1, got, out);
    fclose(in);
    fclose(out);
    fprintf(stderr, "[IOS] settings: created %s from the bundle\n", dst);
}

void ios_boot(void)
{
    const char *home;
    const char *doc;

    home = getenv("HOME");           /* the app sandbox container */
    doc = getenv("MC3_DOC_DIR");
    /* Same base for the seed and for every later read/write: MC3_DOC_DIR
     * could otherwise seed one file and use another. */
    seed_env_file(doc ? doc : home);
    if (home) {
        snprintf(env_path, sizeof env_path, "%s/Documents/%s_env.txt",
                 doc ? doc : home, MC3_APP);
    }
    load_env_file();
    apply_online_default();
    settings_from_env();
    log_open();
    if (home) {
        char log_path[2048];
        snprintf(log_path, sizeof log_path, "%s/Documents/mcdub3remix-launch.log", home);
        freopen(log_path, "a", stdout);
        freopen(log_path, "a", stderr);
        setvbuf(stdout, NULL, _IONBF, 0);
        setvbuf(stderr, NULL, _IONBF, 0);
    }
    fprintf(stderr, "[IOS] booted: %s %s (container %s)\n",
            MC3_APP, MC3_GAME_TITLE, home ? home : "?");
}

#if 0
/* stdio -> ring redirection (kept minimal: the runtime's own writes go
 * through fprintf(stderr) which the app's stderr already captures via
 * dup2 in the launcher; expand if per-write overhead shows up). */
#endif

/* ── the rest of the host contract ──────────────────────────────────────────
 * Symbols the engine core and the renderer expect from a host, which the
 * desktop builds get from windows_media.c / the settings dialogs. iOS has no
 * phone-music bridge and its settings live in the launcher, so these are the
 * honest answers rather than stubs that pretend to do something. */

/* Is the game in gameplay? Only the phone-music bridge asks, to duck the game's
 * audio under outside music. iOS has no such bridge: nothing ever ducks. */
int windows_media_gameplay_get(void) { return 0; }

/* Gain applied to the game's own audio while phone music plays: none. */
float mc3_output_gain(void) { return 1.0f; }

/* The in-game Port Options "attach" opens the desktop settings window. On iOS
 * the launcher owns the settings UI (it writes the same env file), so there is
 * nothing to attach to a window. */
struct SDL_Window;
void windows_port_settings_attach(struct SDL_Window *win) { (void)win; }

/* The input layer caches the rumble strength and re-reads MC3_RUMBLE_STRENGTH
 * when it is -1, which is what a change in the launcher needs to take effect. */
extern void xbox_InputSetRumbleStrength(int percent);
void xbox_rumble_strength_reset(void) { xbox_InputSetRumbleStrength(-1); }

#endif /* TARGET_OS_IPHONE */
#endif /* __APPLE__ */