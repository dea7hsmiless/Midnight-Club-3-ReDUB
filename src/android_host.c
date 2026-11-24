/*
 * android_host.c -- Android boot glue (ADR 0010), the counterpart of
 * switch_nx.c on the Switch.
 *
 *   folder    the app's own external files folder, through SDL:
 *             /storage/emulated/0/Android/data/<app>/files -- what a user
 *             can reach by USB or a file manager; the working directory
 *   disc      the first holding default.xbe of <folder>/game-<edition>/,
 *             <folder>/game/, then on internal storage and every SD card
 *             <MC3_SD_DIR>/ and switch/<MC3_SD_DIR>/ (the Switch build's
 *             place: the same card under switchroot), game-<edition>/
 *             before game/, then game-<any edition>/; the start screen's
 *             choice (android/LauncherActivity.java), else MC3_GAME_DIR,
 *             overrides. Outside <folder> it takes "All files access"
 *   saves     <folder>/save/ (MC3_SAVE_DIR overrides)
 *   settings  <MC3_APP>_env.txt in <folder>, then in <MC3_SD_DIR>/ on
 *             internal storage (a file manager can reach that one), KEY=VALUE
 *             per line, '#' starts a comment; applied with setenv before
 *             boot, as on the Switch
 *   log       <folder>/<MC3_APP>_log.txt (the run before it: _log_prev.txt),
 *             and every line to logcat under the tag <MC3_APP> -- Android
 *             sends stdout and stderr nowhere otherwise
 */
#ifdef __ANDROID__
#include <SDL.h>
#include <android/log.h>
#include <dirent.h>
#include <jni.h>
#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <wchar.h>

#ifndef MC3_APP
#  define MC3_APP "mcdub3"
#endif
#ifndef MC3_PACKAGE_EDITION
#  define MC3_PACKAGE_EDITION "usa"
#endif

extern void xbox_touchpad_reload(void);
extern void xbox_touchpad_recenter_tilt(void);
extern void xbox_touchpad_test_haptics(void);
extern void xbox_InputSetRumbleStrength(int percent);
#ifndef MC3_ONLINE_SERVER
#  define MC3_ONLINE_SERVER "162.35.56.66"
#endif
static char s_dir[512];
static int  s_pipe[2] = { -1, -1 };
static FILE *s_log;

static void android_media_settings_init(void);   /* below */

/* stdout and stderr, line by line, into the log file and logcat. */
static void *log_pump(void *arg)
{
    char buf[4096], line[4096];
    size_t have = 0;
    ssize_t n;
    (void)arg;
    while ((n = read(s_pipe[0], buf, sizeof buf)) > 0) {
        ssize_t i;
        if (s_log) {
            fwrite(buf, 1, (size_t)n, s_log);
            fflush(s_log);
        }
        for (i = 0; i < n; i++) {
            if (buf[i] == '\n' || have == sizeof line - 1) {
                line[have] = '\0';
                __android_log_write(ANDROID_LOG_INFO, MC3_APP, line);
                have = 0;
                if (buf[i] == '\n')
                    continue;
            }
            line[have++] = buf[i];
        }
    }
    return NULL;
}

static void log_open(void)
{
    char path[600], prev[600];
    pthread_t th;
    snprintf(path, sizeof path, "%s/" MC3_APP "_log.txt", s_dir);
    snprintf(prev, sizeof prev, "%s/" MC3_APP "_log_prev.txt", s_dir);
    rename(path, prev);                      /* the run before, kept */
    s_log = fopen(path, "w");
    if (pipe(s_pipe) != 0)
        return;
    dup2(s_pipe[1], STDOUT_FILENO);
    dup2(s_pipe[1], STDERR_FILENO);
    setvbuf(stdout, NULL, _IOLBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);
    if (pthread_create(&th, NULL, log_pump, NULL) == 0)
        pthread_detach(th);
}

static void env_file_apply(const char *dir)
{
    char path[600], line[512];
    FILE *f;
    snprintf(path, sizeof path, "%s/" MC3_APP "_env.txt", dir);
    if (!(f = fopen(path, "r")))
        return;
    while (fgets(line, sizeof line, f)) {
        char *key = line, *eq, *end;
        while (*key == ' ' || *key == '\t')
            key++;
        if (*key == '#' || *key == '\n' || *key == '\r' || !*key)
            continue;
        if (!(eq = strchr(key, '=')))
            continue;
        *eq = '\0';
        {   /* a hand edit with a space before '=' or a quoted value must
             * mean the same here as on Windows and Linux */
            char *name_end = eq;
            while (name_end > key && (name_end[-1] == ' ' || name_end[-1] == '\t'))
                name_end--;
            *name_end = '\0';
        }
        end = eq + 1 + strcspn(eq + 1, "\r\n");
        *end = '\0';
        {
            char *value = eq + 1;
            while (*value == ' ' || *value == '\t') value++;
            size_t n = strlen(value);
            if (n >= 2 && (value[0] == '"' || value[0] == '\'') && value[n - 1] == value[0]) {
                value[n - 1] = '\0';
                value++;
            }
            setenv(key, value, 1);
            printf("[android] %s=%s\n", key, value);
        }
    }
    fclose(f);
}

/* The folder everything lives in (after android_boot). */
const char *android_app_dir(void)
{
    return s_dir;
}

/* MainActivity's static helpers: "All files access" (Android 11+), which a
 * disc outside the app's own folder needs. */
static int activity_call(const char *name)
{
    JNIEnv *env = (JNIEnv *)SDL_AndroidGetJNIEnv();
    jobject act = (jobject)SDL_AndroidGetActivity();
    jclass cls;
    jmethodID m;
    int r = 0;
    if (!env || !act)
        return 0;
    cls = (*env)->GetObjectClass(env, act);
    m = cls ? (*env)->GetStaticMethodID(env, cls, name, "()Z") : NULL;
    if (m)
        r = (*env)->CallStaticBooleanMethod(env, cls, m) ? 1 : 0;
    if ((*env)->ExceptionCheck(env))
        (*env)->ExceptionClear(env);
    if (cls)
        (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, act);
    return r;
}

static int has_disc(const char *dir)
{
    char path[700];
    snprintf(path, sizeof path, "%s/default.xbe", dir);
    return access(path, R_OK) == 0;
}

/* The disc: the app's folder, then MC3_SD_DIR/ (a folder of its own on the
 * device's storage) and switch/MC3_SD_DIR/ (where the Switch build keeps it:
 * the same SD card under switchroot) on internal storage and every SD card;
 * game-<edition>/ before game/ in each. */
static int find_disc(char *out, size_t cap)
{
    static const char *const sub[] = {
        MC3_SD_DIR "/game-" MC3_PACKAGE_EDITION, MC3_SD_DIR "/game",
        "switch/" MC3_SD_DIR "/game-" MC3_PACKAGE_EDITION, "switch/" MC3_SD_DIR "/game",
    };
    char roots[16][300];
    int nroots = 0, r, k;
    DIR *d;
    snprintf(out, cap, "%s/game-" MC3_PACKAGE_EDITION, s_dir);
    if (has_disc(out))
        return 1;
    snprintf(out, cap, "%s/game", s_dir);
    if (has_disc(out))
        return 1;
    snprintf(roots[nroots++], sizeof roots[0], "/storage/emulated/0");
    if ((d = opendir("/storage")) != NULL) {
        struct dirent *e;
        while ((e = readdir(d)) != NULL && nroots < 16) {
            if (e->d_name[0] == '.' || !strcmp(e->d_name, "self")
                || !strcmp(e->d_name, "emulated"))
                continue;
            snprintf(roots[nroots++], sizeof roots[0], "/storage/%s", e->d_name);
        }
        closedir(d);
    }
    for (r = 0; r < nroots; r++)
        for (k = 0; k < (int)(sizeof sub / sizeof sub[0]); k++) {
            snprintf(out, cap, "%s/%s", roots[r], sub[k]);
            if (has_disc(out))
                return 1;
        }
    /* Another edition's disc (game-eur/ for the usa package, ...): the boot
     * check takes every edition this lift accepts (xbe_editions.h). */
    {
        char dirs[3 * 16][330];
        int nd = 0, i;
        snprintf(dirs[nd++], sizeof dirs[0], "%s", s_dir);
        for (r = 0; r < nroots && nd + 2 <= (int)(sizeof dirs / sizeof dirs[0]); r++) {
            snprintf(dirs[nd++], sizeof dirs[0], "%s/" MC3_SD_DIR, roots[r]);
            snprintf(dirs[nd++], sizeof dirs[0], "%s/switch/" MC3_SD_DIR, roots[r]);
        }
        for (i = 0; i < nd; i++) {
            struct dirent *e;
            if (!(d = opendir(dirs[i])))
                continue;
            while ((e = readdir(d)) != NULL)
                if (!strncmp(e->d_name, "game-", 5)) {
                    snprintf(out, cap, "%s/%s", dirs[i], e->d_name);
                    if (has_disc(out)) {
                        closedir(d);
                        return 1;
                    }
                }
            closedir(d);
        }
    }
    return 0;
}

void android_boot(void)
{
    const char *d = SDL_AndroidGetExternalStoragePath();
    char path[700];
    if (!d || !d[0])
        d = SDL_AndroidGetInternalStoragePath();
    snprintf(s_dir, sizeof s_dir, "%s", d ? d : ".");
    mkdir(s_dir, 0775);
    if (chdir(s_dir) != 0)
        __android_log_print(ANDROID_LOG_WARN, MC3_APP, "chdir %s: %s", s_dir, strerror(errno));
    log_open();
    printf("[android] " MC3_APP " starting in %s\n", s_dir);
    /* Back (the gesture, a pad's or keyboard's) is the Xbox's Back, not
     * "leave the game". */
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    {   /* The settings file has to exist on a fresh install: created here,
         * beside the game data, before anything reads it. */
        char env[700];
        FILE *f;
        snprintf(env, sizeof env, "%s/" MC3_APP "_env.txt", s_dir);
        if ((f = fopen(env, "rb")) == NULL && (f = fopen(env, "wb")) != NULL) {
            fputs("# " MC3_APP " settings. One NAME=value per line.\n", f);
            fputs("# Online play is on by default; MC3_ONLINE=0 turns it off.\n", f);
            fputs("# The room is picked in game: Options > ROOM.\n", f);
            fclose(f);
            __android_log_print(ANDROID_LOG_INFO, MC3_APP, "settings: created %s", env);
        } else if (f) {
            fclose(f);
        }
    }
    env_file_apply(s_dir);
    env_file_apply("/storage/emulated/0/" MC3_SD_DIR);   /* reachable by a file manager */
    {   /* Online play on unless the player turned it off, the same default
         * iOS has always had. */
        const char *off = getenv("MC3_ONLINE");
        if (off && off[0] == '0') {
            unsetenv("MC3_ROAM_JOIN");
            unsetenv("MC3_NETPLAY");
        } else {
            if (!getenv("MC3_ROAM_JOIN"))
                setenv("MC3_ROAM_JOIN", MC3_ONLINE_SERVER, 1);
            if (!getenv("MC3_NETPLAY"))
                setenv("MC3_NETPLAY", "1", 1);
        }
    }
    {   /* the start screen's choice (android/LauncherActivity.java) wins */
        const char *l = getenv("MC3_LAUNCHER_GAME_DIR");
        if (l && *l)
            setenv("MC3_GAME_DIR", l, 1);
        if ((l = getenv("MC3_LAUNCHER_TOUCH_PAD")) != NULL && *l)
            setenv("RECOMP_TOUCH_PAD", l, 1);  /* auto, 1 always, 0 never */
        if ((l = getenv("MC3_LAUNCHER_TOUCH_STEER")) != NULL && *l)
            setenv("RECOMP_TOUCH_STEER", l, 1);
        if ((l = getenv("MC3_LAUNCHER_TOUCH_SENS")) != NULL && *l)
            setenv("RECOMP_TOUCH_STEER_SENS", l, 1);
        if ((l = getenv("MC3_LAUNCHER_TOUCH_DEADZONE")) != NULL && *l)
            setenv("RECOMP_TOUCH_DEADZONE", l, 1);
        if ((l = getenv("MC3_LAUNCHER_TOUCH_HAPTICS")) != NULL && *l)
            setenv("RECOMP_TOUCH_HAPTICS", l, 1);
        if ((l = getenv("MC3_LAUNCHER_TOUCH_INVERT")) != NULL && *l)
            setenv("RECOMP_TOUCH_TILT_INVERT", l, 1);
    }
    android_media_settings_init();
    for (;;) {
        const char *g = getenv("MC3_GAME_DIR");
        static const SDL_MessageBoxButtonData buttons[] = {
            { SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT, 1, "Retry" },
            { 0, 2, "Allow storage access" },
            { SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT, 0, "Quit" },
        };
        SDL_MessageBoxData box;
        char msg[1600];
        int access_ok, hit = -1;
        if (g ? has_disc(g) : find_disc(path, sizeof path)) {
            if (!g)
                setenv("MC3_GAME_DIR", path, 1);
            printf("[android] disc: %s\n", getenv("MC3_GAME_DIR"));
            break;
        }
        /* No disc: say where it may go, rather than closing without a word. */
        access_ok = activity_call("hasAllFilesAccess");
        printf("[android] no disc found (%s; all files access %s)\n",
               g ? g : "searched", access_ok ? "on" : "off");
        snprintf(msg, sizeof msg,
                 "No game disc found.\n\n"
                 "Put the extracted disc (the folder holding default.xbe) in one of:\n\n"
                 "  " MC3_SD_DIR "/game-" MC3_PACKAGE_EDITION "/  on the device's storage or SD card\n"
                 "  switch/" MC3_SD_DIR "/game-" MC3_PACKAGE_EDITION "/  (as the Switch build has it)\n"
                 "  %s/game-" MC3_PACKAGE_EDITION "/\n\n"
                 "or name its folder in " MC3_SD_DIR "/" MC3_APP "_env.txt: MC3_GAME_DIR=/path/to/disc%s%s%s",
                 s_dir, g ? "\n\nMC3_GAME_DIR is set to " : "", g ? g : "",
                 access_ok ? "" : "\n\nOutside the app's folder the game needs \"All files access\".");
        memset(&box, 0, sizeof box);
        box.flags = SDL_MESSAGEBOX_ERROR;
        box.title = "Midnight Club 3";
        box.message = msg;
        box.numbuttons = access_ok ? 2 : 3;
        box.buttons = access_ok ? (const SDL_MessageBoxButtonData[]){ buttons[0], buttons[2] }
                                : buttons;
        if (SDL_ShowMessageBox(&box, &hit) != 0 || hit == 0)
            exit(0);
        if (hit == 2)
            activity_call("openAllFilesAccess");
    }
    if (!getenv("MC3_SAVE_DIR")) {
        snprintf(path, sizeof path, "%s/save", s_dir);
        mkdir(path, 0775);
        setenv("MC3_SAVE_DIR", path, 1);
    }
}

/* The shared renderer carries the desktop music HUD. The real bridge is
 * android_media.c (MediaSessionManager); this side owns the Port Options
 * state and persists it in the same env file as the other settings. */
static atomic_int s_pc_music, s_music_hud = 1;
static int s_music_restore = 100;

static void android_save_int(const char *key, int value);   /* below */
extern void windows_media_set(int enabled);
extern int android_media_has_access(void);
extern void android_media_open_access(void);
extern void mc3_media_set_gain(int step);

int windows_port_pc_music_get(void)
{
    return atomic_load(&s_pc_music);
}

int windows_port_music_hud_get(void)
{
    return atomic_load(&s_music_hud);
}

void windows_port_music_backup(float volume)
{
    int n = (int)(volume * 100.0f + 0.5f);
    if (n >= 0 && n <= 100) {
        s_music_restore = n;
        android_save_int("MC3_PC_MUSIC_RESTORE_VOLUME", n);
    }
}

float windows_port_music_restore(void)
{
    return s_music_restore / 100.0f;
}

/* Called from android_boot once the env file is applied: the media bridge
 * must know the choice before the first frame, and the log records whether
 * notification access (the one permission the API needs) is granted. */
static void android_media_settings_init(void)
{
    const char *v;
    if ((v = getenv("MC3_PC_MUSIC")) != NULL)
        atomic_store(&s_pc_music, atoi(v) != 0);
    if ((v = getenv("MC3_PC_MUSIC_HUD")) != NULL)
        atomic_store(&s_music_hud, atoi(v) != 0);
    if ((v = getenv("MC3_PC_MUSIC_RESTORE_VOLUME")) != NULL) {
        int n = atoi(v);
        if (n >= 0 && n <= 100)
            s_music_restore = n;
    }
    if ((v = getenv("MC3_GAME_GAIN")) != NULL)
        mc3_media_set_gain(atoi(v));
    windows_media_set(atomic_load(&s_pc_music));
    fprintf(stderr, "[MEDIA] phone music %s (notification access %s)\n",
            atomic_load(&s_pc_music) ? "on" : "off",
            android_media_has_access() ? "granted" : "not granted");
}

static const char *android_scales[] = { "1", "1.5", "2", "3", "5" };
static const char *android_hud_scales[] = { "1", "2", "3", "5" };
static int s_scale = 2, s_hud = 1, s_aniso = 16, s_sharpen = 2, s_fxaa = 1;
static int s_overlay, s_gamma = 2, s_brightness = 4, s_vsync = 1, s_preset;
static int s_frame_rate = 30;

/* Replace one key in <app>_env.txt, keeping comments and other settings.
 * The rename commits the new file only after it is complete. */
static int android_env_set(const char *key, const char *value)
{
    char path[600], tmp[608], line[1024];
    size_t keylen = strlen(key);
    int last = '\n';
    FILE *in, *out;
    if (!s_dir[0])
        return 0;
    snprintf(path, sizeof path, "%s/" MC3_APP "_env.txt", s_dir);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if (!(out = fopen(tmp, "w")))
        return 0;
    if ((in = fopen(path, "r")) != NULL) {
        while (fgets(line, sizeof line, in)) {
            char *p = line;
            while (*p == ' ' || *p == '\t')
                p++;
            if (!strncmp(p, key, keylen)) {
                const char *q=p+keylen;while(*q==' '||*q=='\t')q++;
                if(*q=='=')continue;
            }
            fputs(line, out);
            size_t n = strlen(line);
            if (n)
                last = (unsigned char)line[n - 1];
        }
        if (ferror(in)) {
            fclose(in);
            fclose(out);
            remove(tmp);
            return 0;
        }
        fclose(in);
    }
    if (last != '\n')
        fputc('\n', out);
    fprintf(out, "%s=%s\n", key, value);
    int bad=ferror(out);if(fclose(out)!=0)bad=1;
    if (bad) {
        remove(tmp);
        return 0;
    }
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return 0;
    }
    return 1;
}

static void android_save_int(const char *key, int value)
{
    char text[16];
    snprintf(text, sizeof text, "%d", value);
    if (!android_env_set(key, text))
        fprintf(stderr, "[android] could not save %s\n", key);
}

int windows_port_overlay_get(void)
{
    return s_overlay;
}

float windows_port_gamma_get(void)
{
    return 0.8f + s_gamma * 0.1f;
}

float windows_port_brightness_get(void)
{
    return 0.8f + s_brightness * 0.05f;
}

float windows_port_sharpen_get(void)
{
    return s_sharpen * 0.25f;
}

int windows_port_fxaa_get(void)
{
    return s_fxaa;
}

int windows_port_vsync_get(void)
{
    return s_vsync;
}

/* The room and Find a Room rows are shared code (src/games/remix/recomp_manual.c),
 * so every platform gets the same behaviour: a live room change, and a query
 * to the relay for the fullest room with a free slot. */
extern int port_online_label(int id, char *out, size_t size);
extern int port_online_change(int id, int step);

/* The shared online rows save the room number through the same settings
 * storage every other Android setting uses. */
int windows_port_save_nickname(const char *name)
{
    return android_env_set("MC3_ROAM_NAME", name);
}

int windows_port_save_room(int room)
{
    android_save_int("MC3_ROAM_ROOM", room);
    return 1;
}

void windows_port_setting_change(int id)
{
    if (port_online_change(id, 0)) return;
    switch (id) {
    case 1:
        s_vsync = s_vsync == 0 ? 1 : s_vsync == 1 ? -1 : 0;
        android_save_int("RECOMP_GL_VSYNC", s_vsync);
        break;
    case 2:
        s_scale = (s_scale + 1) % 5;
        android_env_set("RECOMP_GL_SCALE", android_scales[s_scale]);
        break;
    case 3:
        s_fxaa = s_fxaa == 0 ? 2 : s_fxaa == 2 ? 1 : s_fxaa == 1 ? 3 : 0;
        android_save_int("RECOMP_GL_FXAA", s_fxaa);
        break;
    case 4:
        s_aniso = s_aniso == 16 ? 4 : s_aniso == 4 ? 1 : 16;
        android_save_int("RECOMP_GL_ANISO", s_aniso);
        break;
    case 6:
        s_sharpen = (s_sharpen + 1) % 5;
        {
            static const char *values[] = { "0", "0.25", "0.5", "0.75", "1" };
            android_env_set("RECOMP_GL_SHARPEN", values[s_sharpen]);
        }
        break;
    case 7:
        s_overlay = !s_overlay;
        android_save_int("MC3_FPS_OVERLAY", s_overlay);
        break;
    case 8:
        s_scale = 2; s_hud = 1; s_fxaa = 1; s_aniso = 16; s_sharpen = 2;
        s_overlay = 0; s_gamma = 2; s_brightness = 4; s_vsync = 0; s_preset = 2;
        android_env_set("RECOMP_GL_SCALE", "2");
        android_env_set("RECOMP_GL_UI_SCALE", "2");
        android_save_int("RECOMP_GL_FXAA", 1);
        android_save_int("RECOMP_GL_ANISO", 16);
        android_env_set("RECOMP_GL_SHARPEN", "0.5");
        android_save_int("RECOMP_GL_VSYNC", 0);
        android_env_set("MC3_GRAPHICS_PRESET", "default");
        android_save_int("MC3_FPS_OVERLAY", 0);
        android_env_set("MC3_GAMMA", "1.0");
        android_env_set("MC3_BRIGHTNESS", "1.00");
        break;
    case 9: {
        int n = !atomic_load(&s_pc_music);
        if (android_env_set("MC3_PC_MUSIC", n ? "1" : "0")) {
            atomic_store(&s_pc_music, n);
            windows_media_set(n);
            if (n && !android_media_has_access())
                android_media_open_access();
            fprintf(stderr, "[MEDIA] phone music %s\n", n ? "on" : "off");
        }
        break;
    }
    case 10: {  /* game audio level while phone music plays */
        static const int steps[] = { 0, 1, 2 };
        const char *v = getenv("MC3_GAME_GAIN");
        int cur = v && *v ? atoi(v) : 0, i, n;
        for (i = 0; i < 3 && steps[i] != cur; i++) { }
        n = steps[(i + 1) % 3];
        {
            char text[8];
            snprintf(text, sizeof text, "%d", n);
            if (android_env_set("MC3_GAME_GAIN", text)) {
                mc3_media_set_gain(n);
                setenv("MC3_GAME_GAIN", text, 1);
                fprintf(stderr, "[MEDIA] game audio with music: step %d\n", n);
            }
        }
        break;
    }
    case 11: {
        int n = !atomic_load(&s_music_hud);
        if (android_env_set("MC3_PC_MUSIC_HUD", n ? "1" : "0"))
            atomic_store(&s_music_hud, n);
        break;
    }
    case 12:
        s_hud = (s_hud + 1) % 4;
        android_env_set("RECOMP_GL_UI_SCALE", android_hud_scales[s_hud]);
        break;
    case 13:
        s_gamma = (s_gamma + 1) % 7;
        {
            char text[16];
            snprintf(text, sizeof text, "%.1f", 0.8 + s_gamma * 0.1);
            android_env_set("MC3_GAMMA", text);
        }
        break;
    case 14:
        s_brightness = (s_brightness + 1) % 9;
        {
            char text[16];
            snprintf(text, sizeof text, "%.2f", 0.8 + s_brightness * 0.05);
            android_env_set("MC3_BRIGHTNESS", text);
        }
        break;
    case 16:
        s_frame_rate = s_frame_rate >= 60 ? 30 : 60;
        android_save_int("MC3_FRAME_RATE", s_frame_rate);
        {
            char text[16];
            snprintf(text, sizeof text, "%d", s_frame_rate);
            setenv("MC3_FRAME_RATE", text, 1);
        }
        fprintf(stderr, "[PORT] frame rate %d saved (restart to apply)\n", s_frame_rate);
        break;
    case 17:
        xbox_touchpad_recenter_tilt();
        break;
    case 18:
        xbox_touchpad_test_haptics();
        break;
    case 19: {
        const char *cur = getenv("RECOMP_TOUCH_STEER");
        const char *next = (!cur || !*cur || !strcmp(cur, "stick")) ? "tilt"
                         : !strcmp(cur, "tilt") ? "both" : "stick";
        if (android_env_set("RECOMP_TOUCH_STEER", next))
            xbox_touchpad_reload();
        break;
    }
    case 20: {
        static const char *values[] = { "1", "1.35", "1.7", "2" };
        const char *cur = getenv("RECOMP_TOUCH_STEER_SENS");
        int i, n = 0;
        for (i = 0; i < 4; i++)
            if (cur && !strcmp(cur, values[i]))
                n = i;
        n = (n + 1) % 4;
        if (android_env_set("RECOMP_TOUCH_STEER_SENS", values[n]))
            xbox_touchpad_reload();
        break;
    }
    case 21: {
        int on = !(getenv("RECOMP_TOUCH_TILT_INVERT") &&
                   getenv("RECOMP_TOUCH_TILT_INVERT")[0] != '0');
        android_save_int("RECOMP_TOUCH_TILT_INVERT", on);
        xbox_touchpad_reload();
        break;
    }
    case 22: {
        int on = !(getenv("RECOMP_TOUCH_HAPTICS") &&
                   getenv("RECOMP_TOUCH_HAPTICS")[0] != '0');
        android_save_int("RECOMP_TOUCH_HAPTICS", on);
        xbox_touchpad_reload();
        break;
    }
    case 15: {  /* controller vibration strength; the input layer re-reads it */
        static const int st[] = { 0, 50, 75, 100, 125, 150 };
        const char *v = getenv("MC3_RUMBLE_STRENGTH");
        int cur = v && *v ? atoi(v) : 100, i, n;
        for (i = 0; i < 6 && st[i] != cur; i++) { }
        n = st[(i + 1) % 6];
        {
            char text[8];
            snprintf(text, sizeof text, "%d", n);
            if (android_env_set("MC3_RUMBLE_STRENGTH", text)) {
                setenv("MC3_RUMBLE_STRENGTH", text, 1);
                xbox_InputSetRumbleStrength(n);
            }
        }
        break;
    }
    }
    if (id >= 1 && id <= 6) {
        s_preset = 0;
        android_env_set("MC3_GRAPHICS_PRESET", "custom");
    }
}

void windows_port_setting_label(int id, char *out, size_t size)
{
    if (port_online_label(id, out, size)) return;
    if (out && size) out[0] = 0;   /* unknown id: blank, never stale */
    static const char *aa_names[] = { "OFF", "FXAA", "FXAA LOW", "FXAA HIGH" };
    if (id == 1)
        snprintf(out, size, "VSYNC: %s", s_vsync < 0 ? "ADAPTIVE" : s_vsync ? "ON" : "OFF");
    if (id == 2)
        snprintf(out, size, "RENDER SCALE: %sX (RESTART)", android_scales[s_scale]);
    if (id == 3)
        snprintf(out, size, "ANTI-ALIASING: %s", aa_names[s_fxaa >= 0 && s_fxaa <= 3 ? s_fxaa : 1]);
    if (id == 4)
        snprintf(out, size, "FILTERING: %dX (RESTART)", s_aniso);
    if (id == 6)
        snprintf(out, size, "SHARPENING: %d%%", s_sharpen * 25);
    if (id == 7)
        snprintf(out, size, "SHOW FPS: %s", s_overlay ? "ON" : "OFF");
    if (id == 8)
        snprintf(out, size, "RESTORE GRAPHICS DEFAULTS");
    if (id == 9) {
        int on = atomic_load(&s_pc_music), access = android_media_has_access();
        snprintf(out, size, "PHONE MUSIC (DRIVING): %s%s", on ? "ON" : "OFF",
                 !access ? (on ? " (NO ACCESS)" : " (NEEDS ACCESS)") : "");
    }
    if (id == 10) {
        static const char *names[] = { "NORMAL", "QUIETER", "QUIET" };
        const char *v = getenv("MC3_GAME_GAIN");
        int g = v && *v ? atoi(v) : 0;
        snprintf(out, size, "GAME AUDIO WITH MUSIC: %s", names[g >= 0 && g <= 2 ? g : 0]);
    }
    if (id == 11)
        snprintf(out, size, "SONG DISPLAY: %s", atomic_load(&s_music_hud) ? "RADIO" : "OFF");
    if (id == 12)
        snprintf(out, size, "HUD TEXTURES: %sX (RESTART)", android_hud_scales[s_hud]);
    if (id == 13)
        snprintf(out, size, "GAMMA: %.1f", windows_port_gamma_get());
    if (id == 14)
        snprintf(out, size, "BRIGHTNESS: %d%%", 80 + s_brightness * 5);
    if (id == 15) {
        const char *v = getenv("MC3_RUMBLE_STRENGTH");
        int n = v && *v ? atoi(v) : 100;
        snprintf(out, size, "CONTROLLER VIBRATION: %d%%", n);
    }
    if (id == 16)
        snprintf(out, size, "FRAME RATE: %d FPS (RESTART)", s_frame_rate);
    if (id == 17)
        snprintf(out, size, "RECENTER TILT");
    if (id == 18)
        snprintf(out, size, "TEST HAPTICS");
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
                 getenv("RECOMP_TOUCH_TILT_INVERT") &&
                 getenv("RECOMP_TOUCH_TILT_INVERT")[0] != '0' ? "ON" : "OFF");
    if (id == 22)
        snprintf(out, size, "TOUCH HAPTICS: %s",
                 getenv("RECOMP_TOUCH_HAPTICS") &&
                 getenv("RECOMP_TOUCH_HAPTICS")[0] != '0' ? "ON" : "OFF");
}

void windows_port_settings_attach(SDL_Window *win)
{
    const char *v;
    (void)win;
    if ((v = getenv("RECOMP_GL_SCALE")) != NULL) {
        for (int i = 0; i < 5; i++)
            if (atof(v) == atof(android_scales[i]))
                s_scale = i;
    }
    if ((v = getenv("RECOMP_GL_UI_SCALE")) != NULL) {
        for (int i = 0; i < 4; i++)
            if (atoi(v) == atoi(android_hud_scales[i]))
                s_hud = i;
    }
    if ((v = getenv("RECOMP_GL_ANISO")) != NULL)
        s_aniso = atoi(v) == 4 ? 4 : atoi(v) == 1 ? 1 : 16;
    if ((v = getenv("RECOMP_GL_SHARPEN")) != NULL) {
        s_sharpen = (int)(atof(v) * 4 + 0.5);
        if (s_sharpen < 0) s_sharpen = 0;
        if (s_sharpen > 4) s_sharpen = 4;
    }
    if ((v = getenv("RECOMP_GL_FXAA")) != NULL) {
        s_fxaa = atoi(v);
        if (s_fxaa < 0 || s_fxaa > 3)
            s_fxaa = 1;
    }
    if ((v = getenv("MC3_FPS_OVERLAY")) != NULL)
        s_overlay = atoi(v) != 0;
    if ((v = getenv("RECOMP_GL_VSYNC")) != NULL)
        s_vsync = atoi(v);
    if ((v = getenv("MC3_GAMMA")) != NULL) {
        s_gamma = (int)((atof(v) - 0.8) * 10 + 0.5);
        if (s_gamma < 0) s_gamma = 0;
        if (s_gamma > 6) s_gamma = 6;
    }
    if ((v = getenv("MC3_BRIGHTNESS")) != NULL) {
        s_brightness = (int)((atof(v) - 0.8) * 20 + 0.5);
        if (s_brightness < 0) s_brightness = 0;
        if (s_brightness > 8) s_brightness = 8;
    }
    if ((v = getenv("MC3_GRAPHICS_PRESET")) != NULL) {
        s_preset = !strcmp(v,"default")?2:!strcmp(v,"performance")?1:!strcmp(v,"quality")?3:atoi(v);
        if (s_preset < 0 || s_preset > 3)
            s_preset = 0;
    }
    if ((v = getenv("MC3_FRAME_RATE")) == NULL || !v[0])
        v = getenv("MC3_FRAME_RATE_TEST_FPS");
    s_frame_rate = v && atoi(v) >= 60 ? 60 : 30;
}
#endif
