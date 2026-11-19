/*
 * android_media.c -- Android media bridge for the in-game radio.
 *
 * Android, unlike iOS, has a public cross-app media API: every player that
 * shows in the notification shade publishes a MediaSession (Spotify, YouTube
 * Music, browsers, podcasts). MediaSessionManager.getActiveSessions() gives
 * us its metadata, playback state and transport controls.
 *
 * The one requirement is notification access: Android only hands the session
 * list to an app that is an enabled NotificationListenerService, so
 * MediaSessionListener.java exists (empty on purpose) and the user grants
 * "Notification access" once. Without it the feature is unavailable and the
 * Port Options row says so.
 *
 * The contract is exactly linux_media.c's, so recomp_manual.c is unchanged.
 * Nothing here blocks the render thread: one poll per second on a worker.
 */
#include <SDL.h>
#include <jni.h>
#include <android/log.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

#ifndef MC3_APP
#  define MC3_APP "mcdub3"
#endif

#define TRACK_PREFIX "NOW PLAYING: "

/* ---- shared state (the windows_media_* contract) ----------------------- */

static atomic_int media_enabled, media_gameplay, media_known_menu, media_native_hud;
static atomic_int media_volume_request = -1, media_volume_actual = -1;
static atomic_uint media_skip_sequence;
static atomic_int media_skip_next;
/* Game-audio level while music is the soundtrack: 0 normal, 1 quieter,
 * 2 quiet (mc3_output_gain, apu_xaudio2.c). */
static atomic_int media_game_gain;
static pthread_mutex_t media_lock = PTHREAD_MUTEX_INITIALIZER;
static wchar_t media_text[512];
static wchar_t media_track[512];
static uint64_t media_until;
static pthread_t media_thread;
static int media_started;
static int media_last_enabled;
static unsigned media_handled_skip;
static char media_selected[256], media_paused[256];
static int media_no_access;

static uint64_t now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u;
}

/* The game's native radio title font is printable ASCII. */
static void ascii_fold(const char *in, char *out, size_t cap)
{
    size_t n = 0;
    if (!out || cap == 0) return;
    if (!in) in = "";
    for (; *in && n + 1 < cap; in++) {
        unsigned char c = (unsigned char)*in;
        if (c >= 32 && c < 127) out[n++] = (char)c;
    }
    out[n] = 0;
}

static void publish_utf8(const char *text)
{
    char ascii[1024];
    wchar_t w[512];
    const wchar_t *track;
    size_t n;

    ascii_fold(text, ascii, sizeof ascii);
    for (n = 0; ascii[n] && n < 511; n++) w[n] = (wchar_t)(unsigned char)ascii[n];
    w[n] = 0;

    pthread_mutex_lock(&media_lock);
    track = !wcsncmp(w, L"NOW PLAYING: ", 13) ? w + 13 : w;
    if (wcscmp(media_track, track)) {
        wcsncpy(media_track, track, 511);
        media_track[511] = 0;
        media_until = now_ms() + 6000;
        atomic_store(&media_native_hud, 0);
    }
    wcsncpy(media_text, w, 511);
    media_text[511] = 0;
    pthread_mutex_unlock(&media_lock);
}

static void clear_banner(void)
{
    pthread_mutex_lock(&media_lock);
    media_text[0] = 0;
    media_track[0] = 0;
    media_until = 0;
    pthread_mutex_unlock(&media_lock);
}

/* ---- JNI helpers -------------------------------------------------------- */

static JNIEnv *jenv(void)
{
    return (JNIEnv *)SDL_AndroidGetJNIEnv();
}

static jobject jactivity(void)
{
    return (jobject)SDL_AndroidGetActivity();
}

static void jdrop(JNIEnv *env, jobject o)
{
    if (o) (*env)->DeleteLocalRef(env, o);
}

/* MediaSessionManager.getActiveSessions(null). Caller clears the exception. */
static jobject session_list(JNIEnv *env, jobject activity)
{
    jclass ctx_cls, mgr_cls;
    jmethodID get_service, get_sessions;
    jstring name;
    jobject mgr, list;

    if (!env || !activity) return NULL;
    ctx_cls = (*env)->GetObjectClass(env, activity);
    if (!ctx_cls) return NULL;
    get_service = (*env)->GetMethodID(env, ctx_cls, "getSystemService",
                                      "(Ljava/lang/String;)Ljava/lang/Object;");
    if (!get_service) {
        (*env)->ExceptionClear(env);
        jdrop(env, ctx_cls);
        return NULL;
    }
    name = (*env)->NewStringUTF(env, "media_session");
    mgr = (*env)->CallObjectMethod(env, activity, get_service, name);
    jdrop(env, name);
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionClear(env);
        jdrop(env, ctx_cls);
        return NULL;
    }
    if (!mgr) {
        jdrop(env, ctx_cls);
        return NULL;
    }
    mgr_cls = (*env)->GetObjectClass(env, mgr);
    get_sessions = (*env)->GetMethodID(env, mgr_cls, "getActiveSessions",
                                       "(Landroid/content/ComponentName;)Ljava/util/List;");
    if (!get_sessions) {
        (*env)->ExceptionClear(env);
        jdrop(env, mgr_cls);
        jdrop(env, mgr);
        jdrop(env, ctx_cls);
        return NULL;
    }
    list = (*env)->CallObjectMethod(env, mgr, get_sessions, NULL);
    if ((*env)->ExceptionCheck(env)) {
        /* SecurityException: no notification access. */
        (*env)->ExceptionClear(env);
        list = NULL;
        media_no_access = 1;
    } else {
        media_no_access = 0;
    }
    jdrop(env, mgr_cls);
    jdrop(env, mgr);
    jdrop(env, ctx_cls);
    return list;
}



/* Runs a transport command on the session that matches pkg (or the first
 * playing one when pkg is empty). Each call re-lists sessions: a controller
 * kept from an earlier poll may belong to a dead session. */
static int session_command(const char *pkg, int action)
{
    JNIEnv *env = jenv();
    jobject act = jactivity();
    jobject list;
    jclass list_cls;
    jmethodID size_m, get_m;
    jint count, i;
    int done = 0;

    if (!env || !act) return 0;
    list = session_list(env, act);
    if (!list) {
        jdrop(env, act);
        return 0;
    }
    list_cls = (*env)->GetObjectClass(env, list);
    size_m = (*env)->GetMethodID(env, list_cls, "size", "()I");
    get_m = (*env)->GetMethodID(env, list_cls, "get", "(I)Ljava/lang/Object;");
    if (!size_m || !get_m) {
        (*env)->ExceptionClear(env);
        jdrop(env, list_cls);
        jdrop(env, list);
        jdrop(env, act);
        return 0;
    }
    count = (*env)->CallIntMethod(env, list, size_m);

    for (i = 0; i < count && !done; i++) {
        jobject ctl = (*env)->CallObjectMethod(env, list, get_m, i);
        jclass ctl_cls;
        jmethodID get_pkg, get_tc;
        jobject jpkg, tc;
        const char *name = NULL;
        int match = 0;

        if (!ctl) continue;
        ctl_cls = (*env)->GetObjectClass(env, ctl);
        get_pkg = (*env)->GetMethodID(env, ctl_cls, "getPackageName", "()Ljava/lang/String;");
        jpkg = get_pkg ? (*env)->CallObjectMethod(env, ctl, get_pkg) : NULL;
        if (jpkg) {
            name = (*env)->GetStringUTFChars(env, (jstring)jpkg, NULL);
            if (name) match = !pkg || !*pkg || !strcmp(name, pkg);
        }
        if (match) {
            get_tc = (*env)->GetMethodID(env, ctl_cls, "getTransportControls",
                                         "()Landroid/media/session/MediaController$TransportControls;");
            tc = get_tc ? (*env)->CallObjectMethod(env, ctl, get_tc) : NULL;
            if (tc) {
                jclass tc_cls = (*env)->GetObjectClass(env, tc);
                const char *which = action == 1 ? "skipToNext"
                                 : action == 2 ? "skipToPrevious"
                                 : action == 3 ? "play" : "pause";
                jmethodID m = (*env)->GetMethodID(env, tc_cls, which, "()V");
                if (m) {
                    (*env)->CallVoidMethod(env, tc, m);
                    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
                    else done = 1;
                }
                jdrop(env, tc_cls);
                jdrop(env, tc);
            }
        }
        if (name) (*env)->ReleaseStringUTFChars(env, (jstring)jpkg, name);
        jdrop(env, jpkg);
        jdrop(env, ctl_cls);
        jdrop(env, ctl);
    }
    jdrop(env, list_cls);
    jdrop(env, list);
    jdrop(env, act);
    return done;
}

/* One snapshot: the playing session's title/artist, else the first one. */
static int session_snapshot(char *title, size_t tc_cap, char *artist, size_t ac_cap,
                            char *pkg, size_t pc_cap, int *playing)
{
    JNIEnv *env = jenv();
    jobject act = jactivity();
    jobject list;
    jclass list_cls;
    jmethodID size_m, get_m;
    jint count, i;
    int found = 0, found_playing = 0;

    if (title && tc_cap) title[0] = 0;
    if (artist && ac_cap) artist[0] = 0;
    if (pkg && pc_cap) pkg[0] = 0;
    if (playing) *playing = 0;
    if (!env || !act) return 0;

    list = session_list(env, act);
    if (!list) {
        jdrop(env, act);
        return 0;
    }
    list_cls = (*env)->GetObjectClass(env, list);
    size_m = (*env)->GetMethodID(env, list_cls, "size", "()I");
    get_m = (*env)->GetMethodID(env, list_cls, "get", "(I)Ljava/lang/Object;");
    if (!size_m || !get_m) {
        (*env)->ExceptionClear(env);
        jdrop(env, list_cls);
        jdrop(env, list);
        jdrop(env, act);
        return 0;
    }
    count = (*env)->CallIntMethod(env, list, size_m);

    for (i = 0; i < count; i++) {
        jobject ctl = (*env)->CallObjectMethod(env, list, get_m, i);
        jclass ctl_cls, state_cls, md_cls;
        jmethodID get_pkg, get_state, get_md, get_string;
        jobject jpkg, state, md;
        jint state_code = 0;
        int is_playing;
        const char *name;

        if (!ctl) continue;
        ctl_cls = (*env)->GetObjectClass(env, ctl);
        get_pkg = (*env)->GetMethodID(env, ctl_cls, "getPackageName", "()Ljava/lang/String;");
        get_state = (*env)->GetMethodID(env, ctl_cls, "getPlaybackState",
                                        "()Landroid/media/PlaybackState;");
        jpkg = get_pkg ? (*env)->CallObjectMethod(env, ctl, get_pkg) : NULL;
        state = get_state ? (*env)->CallObjectMethod(env, ctl, get_state) : NULL;
        if (state) {
            state_cls = (*env)->GetObjectClass(env, state);
            jmethodID get_code = (*env)->GetMethodID(env, state_cls, "getState", "()I");
            if (get_code) state_code = (*env)->CallIntMethod(env, state, get_code);
            jdrop(env, state_cls);
        }
        is_playing = state_code == 3;   /* PlaybackState.STATE_PLAYING */

        if (!found || (is_playing && !found_playing)) {
            md = NULL;
            {
                jmethodID get_md_id = (*env)->GetMethodID(env, ctl_cls, "getMetadata",
                                                          "()Landroid/media/MediaMetadata;");
                if (get_md_id) md = (*env)->CallObjectMethod(env, ctl, get_md_id);
            }
            if (md) {
                md_cls = (*env)->GetObjectClass(env, md);
                get_string = (*env)->GetMethodID(env, md_cls, "getString",
                                                 "(Ljava/lang/String;)Ljava/lang/String;");
                if (get_string) {
                    static const char *const keys[2] = {
                        "android.media.metadata.TITLE",
                        "android.media.metadata.ARTIST"
                    };
                    char *dsts[2] = { title, artist };
                    size_t caps[2] = { tc_cap, ac_cap };
                    for (int k = 0; k < 2; k++) {
                        jstring key = (*env)->NewStringUTF(env, keys[k]);
                        jstring val = (jstring)(*env)->CallObjectMethod(env, md, get_string, key);
                        jdrop(env, key);
                        if (val) {
                            const char *s = (*env)->GetStringUTFChars(env, val, NULL);
                            if (s && dsts[k] && caps[k])
                                snprintf(dsts[k], caps[k], "%s", s);
                            if (s) (*env)->ReleaseStringUTFChars(env, val, s);
                            jdrop(env, val);
                        }
                    }
                }
                jdrop(env, md_cls);
                jdrop(env, md);
            }
            if (pkg && pc_cap && jpkg) {
                name = (*env)->GetStringUTFChars(env, (jstring)jpkg, NULL);
                if (name) {
                    snprintf(pkg, pc_cap, "%s", name);
                    (*env)->ReleaseStringUTFChars(env, (jstring)jpkg, name);
                }
            }
            if (playing) *playing = is_playing;
            found = 1;
            found_playing = is_playing;
        }
        jdrop(env, jpkg);
        jdrop(env, ctl_cls);
        jdrop(env, ctl);
        if (found_playing) break;
    }
    jdrop(env, list_cls);
    jdrop(env, list);
    jdrop(env, act);
    return found;
}

/* The desktop menu policy: pause the player we own in a known menu, resume it
 * when driving resumes unless another player took over in the meantime. */
static void menu_policy(int menu, const char *active, const char *active_pkg, int playing)
{
    if (!atomic_load(&media_enabled)) return;
    if (menu && !media_paused[0] && active_pkg[0] && playing)
        if (session_command(active_pkg, 4)) {
            snprintf(media_paused, sizeof media_paused, "%s", active_pkg);
            fprintf(stderr, "[MEDIA] paused %s for the game menu\n", active_pkg);
        }
    if (!menu && media_paused[0]) {
        char other_pkg[256] = "";
        int other_playing = 0;
        int takeover = session_snapshot(NULL, 0, NULL, 0, other_pkg, sizeof other_pkg,
                                        &other_playing) &&
                       other_playing && strcmp(other_pkg, media_paused);
        if (!takeover && session_command(media_paused, 3))
            fprintf(stderr, "[MEDIA] resumed %s after the game menu\n", media_paused);
        media_paused[0] = 0;
    }
    (void)active;
}

/* ---- poll loop ---------------------------------------------------------- */

static void *media_worker(void *unused)
{
    (void)unused;
    while (1) {
        int enabled = atomic_load(&media_enabled);
        unsigned skip = atomic_load(&media_skip_sequence);

        if (enabled) {
            char title[256] = "", artist[256] = "", pkg[256] = "", line[640];
            int playing = 0;
            int found;

            found = session_snapshot(title, sizeof title, artist, sizeof artist,
                                     pkg, sizeof pkg, &playing);
            menu_policy(atomic_load(&media_known_menu), title, pkg, playing);

            if (skip != media_handled_skip) {
                media_handled_skip = skip;
                session_command(media_selected[0] ? media_selected : pkg,
                                atomic_load(&media_skip_next) ? 1 : 2);
            }

            if (found && title[0]) {
                snprintf(media_selected, sizeof media_selected, "%s", pkg);
                snprintf(line, sizeof line, "%s%s%s%s",
                         playing ? TRACK_PREFIX : "PAUSED: ",
                         title, artist[0] ? " - " : "", artist);
                publish_utf8(line);
            } else {
                media_selected[0] = 0;
                clear_banner();
            }
        } else if (media_last_enabled) {
            if (media_paused[0]) {
                session_command(media_paused, 3);
                media_paused[0] = 0;
            }
            media_selected[0] = 0;
            clear_banner();
        }
        media_last_enabled = enabled;

        {
            struct timespec pause = {1, 0};
            nanosleep(&pause, NULL);
        }
    }
    return NULL;
}

/* ---- settings helpers (called from android_host.c) ---------------------- */

int android_media_has_access(void)
{
    JNIEnv *env = jenv();
    jobject act = jactivity();
    jobject list;
    int ok;
    if (!env || !act) return 0;
    list = session_list(env, act);
    ok = !media_no_access;
    jdrop(env, list);
    jdrop(env, act);
    return ok;
}

void android_media_open_access(void)
{
    /* MainActivity.openNotificationAccess() (Java) */
    JNIEnv *env = jenv();
    jobject act = jactivity();
    jclass cls;
    jmethodID m;
    if (!env || !act) return;
    cls = (*env)->GetObjectClass(env, act);
    m = cls ? (*env)->GetStaticMethodID(env, cls, "openNotificationAccess", "()Z") : NULL;
    if (m) (*env)->CallStaticBooleanMethod(env, cls, m);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    jdrop(env, cls);
    jdrop(env, act);
}

/* ---- the windows_media_* contract -------------------------------------- */

void windows_media_set(int enabled)
{
    atomic_store(&media_enabled, enabled != 0);
    if (enabled && !media_started) {
        if (pthread_create(&media_thread, NULL, media_worker, NULL) == 0)
            pthread_detach(media_thread);
        media_started = 1;
    }
    if (enabled) {
        pthread_mutex_lock(&media_lock);
        if (media_track[0]) media_until = now_ms() + 6000;
        pthread_mutex_unlock(&media_lock);
    }
}

void windows_media_context(int context)
{
    atomic_store(&media_known_menu, context == 2);
    atomic_store(&media_gameplay, context == 1);
    if (context == 1) {
        pthread_mutex_lock(&media_lock);
        if (media_track[0]) media_until = now_ms() + 6000;
        pthread_mutex_unlock(&media_lock);
    }
}

int windows_media_gameplay_get(void) { return atomic_load(&media_gameplay); }

/* Port Options: how far the game's own audio drops while phone music plays. */
void mc3_media_set_gain(int step)
{
    if (step < 0) step = 0;
    if (step > 2) step = 2;
    atomic_store(&media_game_gain, step);
}

/* Called once per queued audio block from apu_xaudio2.c. */
float mc3_output_gain(void)
{
    static const float steps[3] = {1.0f, 0.5f, 0.35f};
    int step;
    if (!atomic_load(&media_enabled) || !atomic_load(&media_gameplay)) return 1.0f;
    step = atomic_load(&media_game_gain);
    if (step < 0 || step > 2) step = 0;
    return steps[step];
}
void windows_media_native_hud(int active) { atomic_store(&media_native_hud, active != 0); }
void windows_media_route_commit(const wchar_t *text, int native)
{
    (void)text;
    atomic_store(&media_native_hud, native != 0);
}

void windows_media_skip(int next)
{
    atomic_store(&media_skip_next, next != 0);
    atomic_fetch_add(&media_skip_sequence, 1);
}

int windows_media_banner_get(wchar_t *out, int count)
{
    int visible;
    if (!atomic_load(&media_enabled) || !atomic_load(&media_gameplay) || count < 1) return 0;
    pthread_mutex_lock(&media_lock);
    visible = now_ms() < media_until && media_text[0];
    if (visible) {
        wcsncpy(out, media_text, (size_t)count - 1);
        out[count - 1] = 0;
    }
    pthread_mutex_unlock(&media_lock);
    return visible;
}

int windows_media_get(wchar_t *out, int count)
{
    int visible;
    if (count < 1) return 0;
    pthread_mutex_lock(&media_lock);
    visible = atomic_load(&media_enabled) && atomic_load(&media_gameplay) &&
              !atomic_load(&media_native_hud) && now_ms() < media_until && media_text[0];
    if (visible) {
        wcsncpy(out, media_text, (size_t)count - 1);
        out[count - 1] = 0;
    }
    pthread_mutex_unlock(&media_lock);
    return visible;
}

/* No per-app volume on Android; the media stream is shared with the game. */
int windows_media_volume_get(void)
{
    return atomic_load(&media_volume_request) >= 0 ? atomic_load(&media_volume_request)
                                                   : atomic_load(&media_volume_actual);
}
void windows_media_volume_set(int percent)
{
    if (percent >= 0 && percent <= 100) atomic_store(&media_volume_request, percent);
}
int windows_media_available(void) { return 1; }
