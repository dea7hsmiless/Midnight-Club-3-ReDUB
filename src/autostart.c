/* autostart.c -- MC3_AUTOSTART=cruise: from boot straight into Arcade, Cruise.
 *
 * Input like a player's on pad 1, the way MC3_SKIP_MOVIES presses through
 * the intro movies: one press at a time, each once its screen is up, so the
 * game's own menus do the work. The screens are told apart by the Flash files
 * the front end opens (the kernel's path hook, xbox_path_hook):
 *
 *   flash\bg_savegame.xbck      title (PRESS START) and the profile choice
 *   UDATA\<title>\ listed       the profile screen filling its list
 *   *.mc3* read                 a profile exists (read at boot already)
 *   flash\bg_main.xbck          main menu
 *   flash\bg_arcade.xbck        Arcade
 *   flash\loadscreen_arcade*    the cruise loading
 *
 * Any other bg_* / loadscreen_* means a wrong turn: it stops. The title's
 * frame hook calls mc3_autostart_frame() once a game frame, with whether
 * the state machine is in a race or cruise (which ends it) and how many
 * Downs the main menu needs to reach Arcade (2: Career, Tokyo Challenge,
 * Arcade). Input of the player's own (host pad, keyboard) ends it at once.
 *
 * With no profile the game asks to make one: that is left to the player (it
 * stops and says so). From the next boot it goes through.
 *
 * Presses are held 3 game frames and at least 150 ms (the menus lost shorter
 * ones, sdl_keyboard.c), and wait for the front end to go quiet (no path
 * asked for 1 s) and the screen's own transition (3 s / 30 frames after its
 * file opened). The menus move with the d-pad.
 *
 * The mechanism is from mc3-web by Leandro Dettmer (MIT; commit 7a64713, the
 * last MIT-era commit). Their freeroam mode is not included: it needs their
 * Online main-menu rework (M3). */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

extern void (*xbox_path_hook)(const char *xbox_path);
extern int mc3_menu_item_count(void);      /* recomp_manual.c */
extern int mc3_port_options_open(void);    /* recomp_manual.c */
extern void mc3_menu_debug_dump(const char *tag);

/* MC3_AUTOSTART_PORT: the target index is the menu's own last item, not a
 * count of Downs. Options is the last item on the main menu, and the main
 * menu's order moves when an Online session reorders it, so the number of
 * items on screen is the only honest way to reach it. */
static int port_last_index(void)
{
    int n = mc3_menu_item_count();
    return n > 0 ? n - 1 : 6;
}
extern void usb_gamepad_set_held(int dev, uint8_t digital, uint8_t analog);
extern int  usb_gamepad_host_input(int dev);

#define PAD_UP    0x01u     /* digital: d-pad up */
#define PAD_DOWN  0x02u     /* digital: d-pad down */
#define PAD_RIGHT 0x08u     /* digital: d-pad right */
#define PAD_START 0x10u     /* digital */
#define PAD_A     0x01u     /* analog: A */

enum {
    SEEN_TITLE    = 1 << 0,
    SEEN_PROFILES = 1 << 1,
    SEEN_PROFILE  = 1 << 2,
    SEEN_MAIN     = 1 << 3,
    SEEN_ARCADE   = 1 << 4,
    SEEN_LOADING  = 1 << 5,
    SEEN_WRONG    = 1 << 6,
    SEEN_NETWORK  = 1 << 7,
    SEEN_OPTIONS  = 1 << 8,
};
enum { ST_BOOT, ST_TITLE, ST_PROFILE, ST_MAIN, ST_ARCADE, ST_LOADING, ST_NETWORK,
       ST_OPTIONS, ST_DONE };
static const char *const k_stage[] = {
    "boot", "title", "profile", "main menu", "Arcade", "loading", "Networking",
    "options", "done"
};

static int s_seen;                  /* SEEN_*, set from any thread */
static uint32_t s_path_ms;          /* when the last path was asked for */
static char s_wrong[48];

static uint32_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

/* MC3_ONLINE_SESSION=1: the walk goes in through Networking, not Arcade.
 * Read here rather than asking the game layer, which this file does not link
 * against (it is included into the game's translation unit, not the other way
 * round). */
static int online_wanted(void)
{
    static int want = -1;
    if (want < 0) {
        const char *e = getenv("MC3_ONLINE_SESSION");
        want = e && e[0] == '1';
    }
    return want;
}

/* MC3_AUTOSTART_HOST=1: on the Networking screen, go to its second item
 * (System Link, the first being Xbox Live/Free Roam) and press A through the
 * host screens, so a test client can put a match up for another device to
 * join. The walk then stops pressing: a lobby is not "in game", and pressing
 * A in it would start or cancel the match. */
static int host_wanted(void)
{
    static int want = -1;
    if (want < 0) {
        const char *e = getenv("MC3_AUTOSTART_HOST");
        want = e && e[0] == '1';
    }
    return want;
}

/* MC3_AUTOSTART_STOP=port: walk to the game's Options page, open the last item
 * (Port Options) and park there, so the page can be looked at without racing
 * the front end with timed presses.
 *
 * MC3_AUTOSTART_PORT=room|find: go on from there, move the cursor to that row
 * and press it once -- the live room change, and the matchmaking request and
 * its answer. Used to check the online rows on a build that has no screen. */
static int port_wanted(void)
{
    static int want = -1;
    if (want < 0) {
        const char *e = getenv("MC3_AUTOSTART_STOP");
        want = e && !strcasecmp(e, "port");
    }
    return want;
}

/* 0: park; 1: press ROOM; 2: press FIND A ROOM. */
static int port_press(void)
{
    static int what = -1;
    if (what < 0) {
        const char *e = getenv("MC3_AUTOSTART_PORT");
        what = e && !strcasecmp(e, "room") ? 1
             : e && !strcasecmp(e, "find") ? 2 : 0;
    }
    return what;
}

static void on_path(const char *p)
{
    const char *base = p, *q;
    size_t n = strlen(p);
    int bit = 0;

    for (q = p; *q; q++)
        if (*q == '\\' || *q == '/')
            base = q + 1;
    if (!strcasecmp(base, "bg_savegame.xbck"))
        bit = SEEN_TITLE;
    else if (!strcasecmp(base, "bg_main.xbck"))
        bit = SEEN_MAIN;
    else if (!strcasecmp(base, "bg_arcade.xbck"))
        bit = SEEN_ARCADE;
    else if (!strcasecmp(base, "bg_network.xbck"))
        bit = SEEN_NETWORK;
    else if (!strcasecmp(base, "bg_options.xbck"))
        bit = SEEN_OPTIONS;
    else if (!strncasecmp(base, "loadscreen_arcade", 17))
        bit = SEEN_LOADING;
    else if (!strncasecmp(base, "bg_", 3) || !strncasecmp(base, "loadscreen_", 11)) {
        bit = SEEN_WRONG;
        snprintf(s_wrong, sizeof s_wrong, "%s", base);
    } else if (strstr(p, "\\UDATA\\") && n && p[n - 1] == '\\')
        bit = SEEN_PROFILES;
    else if (strstr(base, ".mc3"))
        bit = SEEN_PROFILE;
    __atomic_store_n(&s_path_ms, now_ms(), __ATOMIC_RELAXED);
    if (bit)
        __atomic_fetch_or(&s_seen, bit, __ATOMIC_RELAXED);
}

static const char *stamp(void);

int mc3_autostart_on(void)
{
    static int on = -1;
    if (on < 0) {
        const char *e = getenv("MC3_AUTOSTART");
        on = e && !strcasecmp(e, "cruise");
        if (e && *e && strcmp(e, "0") && !on)
            fprintf(stderr, "[AUTOSTART] %s MC3_AUTOSTART=%s: unknown, only \"cruise\"\n",
                    stamp(), e);
        if (on) {
            xbox_path_hook = on_path;
            fprintf(stderr, "[AUTOSTART] %s pressing the way to Arcade, Cruise on pad 1"
                    " (MC3_AUTOSTART); any input of yours stops it\n", stamp());
        }
    }
    return on;
}

/* The walker's current screen, for the front-end experiments: true while the
 * Networking screen is the one on display. */
int mc3_autostart_at_network(void);

static int s_stage = ST_BOOT;
static uint32_t s_stage_ms, s_press_ms, s_release_ms;
static unsigned s_stage_frames, s_press_frames, s_release_frames;
static int s_holding, s_presses, s_downs;
static int s_nav;                   /* Downs taken on the current sub-page */

int mc3_autostart_at_network(void)
{
    return mc3_autostart_on() && s_stage == ST_NETWORK;
}

/* Seconds since the walker started, as "123.4s ". Boot time is the thing
 * people report and the thing this session kept misjudging, and a log that
 * carries only the order of the stages cannot measure it. */
static const char *stamp(void)
{
    static uint32_t t0;
    static char buf[24];
    uint32_t now = now_ms();
    if (!t0)
        t0 = now;
    snprintf(buf, sizeof buf, "%6.1fs", (now - t0) / 1000.0);
    return buf;
}

static void finish(const char *why)
{
    if (s_holding)
        usb_gamepad_set_held(0, 0, 0);
    s_holding = 0;
    s_stage = ST_DONE;
    xbox_path_hook = NULL;
    fprintf(stderr, "[AUTOSTART] %s %s\n", stamp(), why);
}

static void press(uint8_t digital, uint8_t analog, uint32_t now)
{
    usb_gamepad_set_held(0, digital, analog);
    s_holding = 1;
    s_press_ms = now;
    s_press_frames = 0;
    s_presses++;
}

/* The screen has had its transition, the front end is quiet and the last
 * press is long enough ago. */
static int ready(uint32_t now, uint32_t settle_ms, uint32_t again_ms)
{
    return !s_holding
        && now - s_stage_ms >= settle_ms && s_stage_frames >= 30
        && now - __atomic_load_n(&s_path_ms, __ATOMIC_RELAXED) >= 1000
        && s_release_frames >= 5 && now - s_release_ms >= again_ms;
}

/* The same, for screens that keep reading files while they are up.
 *
 * The networking screen polls, so the "front end is quiet" clause above never
 * becomes true and no press is ever made -- the walk sits there for ever. These
 * screens only need their transition and the press spacing; the bounded press
 * count in the caller still stops a screen that never answers. */
static int ready_busy(uint32_t now, uint32_t settle_ms, uint32_t again_ms)
{
    return !s_holding
        && now - s_stage_ms >= settle_ms && s_stage_frames >= 30
        && s_release_frames >= 5 && now - s_release_ms >= again_ms;
}

void mc3_autostart_frame(int in_game, int downs_to_arcade)
{
    uint32_t now;
    int seen, to;

    if (s_stage == ST_DONE || !mc3_autostart_on())
        return;
    now = now_ms();
    if (usb_gamepad_host_input(0)) {
        finish("stopped: input from you");
        return;
    }
    if (in_game) {
        /* state 2/3 begins with the cruise: from the loading screen, or
         * straight from Arcade's setup screens when that Flash file never
         * opens (MC3_SKIP_MOVIES=hard). */
        finish(s_stage == ST_LOADING ||
               ((s_stage == ST_ARCADE || s_stage == ST_NETWORK) && s_presses)
               ? "in Cruise" : "in game: stopped");
        return;
    }

    s_stage_frames++;
    s_press_frames++;
    s_release_frames++;
    /* A screen that keeps asking for files makes ready() permanently false,
     * so its press limit is never reached and the walk waits for ever with
     * nothing in the log. Boot is exempt (measured at up to 100 s) and the
     * loading screen has its own, longer limit. */
    if (s_stage != ST_BOOT && s_stage != ST_LOADING &&
        now - s_stage_ms > 120000) {
        char msg[80];
        snprintf(msg, sizeof msg, "stopped: %s never settled", k_stage[s_stage]);
        finish(msg);
        return;
    }
    if (s_holding && s_press_frames >= 3 && now - s_press_ms >= 150) {
        usb_gamepad_set_held(0, 0, 0);
        s_holding = 0;
        s_release_ms = now;
        s_release_frames = 0;
    }

    seen = __atomic_load_n(&s_seen, __ATOMIC_RELAXED);
    /* MC3_ONLINE_SESSION: the way in is the Networking screen, then its first
     * item (Xbox Live, which a session uses as Free Roam), then the setup
     * screens behind it. Those are screens this walker does not know by name,
     * so in a session it presses A through them instead of refusing them:
     * bounded by s_presses, and it still stops on the first screen that never
     * responds. Outside a session nothing changes. */
    if ((seen & SEEN_WRONG) && !(port_wanted() && (seen & SEEN_OPTIONS)) &&
        !(online_wanted() && (s_stage == ST_NETWORK || (seen & SEEN_NETWORK)))) {
        char msg[96];
        snprintf(msg, sizeof msg, "stopped: %s opened, not the way to Cruise", s_wrong);
        finish(msg);
        return;
    }
    to = seen & SEEN_LOADING ? ST_LOADING
       : seen & SEEN_ARCADE ? ST_ARCADE
       : (seen & SEEN_OPTIONS) && port_wanted() ? ST_OPTIONS
       : (seen & SEEN_NETWORK) && online_wanted() ? ST_NETWORK
       : seen & SEEN_MAIN ? ST_MAIN : s_stage;
    if (to == s_stage && s_stage == ST_BOOT && (seen & SEEN_TITLE))
        to = ST_TITLE;
    if (to == s_stage && s_stage == ST_TITLE && s_presses
        && ((seen & SEEN_PROFILES) || s_presses >= 2))
        to = ST_PROFILE;
    if (to != s_stage) {
        s_stage = to;
        s_stage_ms = now;
        s_stage_frames = 0;
        s_presses = 0;
        s_downs = 0;
        fprintf(stderr, "[AUTOSTART] %s %s\n", stamp(), k_stage[to]);
    }

    {   /* MC3_AUTOSTART_STOP=main|network: park on that screen instead of
         * pressing through it, so it can be inspected. */
        static int stop = -1;
        if (stop < 0) {
            const char *e = getenv("MC3_AUTOSTART_STOP");
            stop = e && !strcasecmp(e, "main") ? ST_MAIN
                 : e && !strcasecmp(e, "network") ? ST_NETWORK : -2;
        }
        if (port_wanted())
            stop = -2;          /* port mode walks past the main menu */
        if (stop == s_stage) {
            finish(stop == ST_MAIN ? "stopped: MC3_AUTOSTART_STOP=main"
                                   : "stopped: MC3_AUTOSTART_STOP=network");
            return;
        }
    }

    switch (s_stage) {
    case ST_BOOT:                   /* boot/legal logos: Start gets past them */
        /* These screens are not bg_* so the title detection stays at boot; on
         * a console they advance on their own or on a press. Without this the
         * walk sat on the legal screen for ever. */
        if (ready_busy(now, 8000, 6000)) {
            if (s_presses >= 5) {
                finish("stopped: no title screen");
                return;
            }
            press(PAD_START, 0, now);
        }
        break;
    case ST_TITLE:                  /* Start; the profile list follows */
        if (ready(now, 3000, 5000)) {
            if (s_presses >= 3) {
                finish("stopped: the title did not take Start");
                return;
            }
            __atomic_fetch_and(&s_seen, ~SEEN_PROFILES, __ATOMIC_RELAXED);
            press(PAD_START, 0, now);
        }
        break;
    case ST_PROFILE:                /* A on the first profile; bg_main follows */
        if (!(seen & SEEN_PROFILE)) {
            finish("stopped: no profile yet -- make one in the game; from the next boot it starts by itself");
            return;
        }
        if (ready(now, 2000, 6000)) {
            if (s_presses >= 4) {
                finish("stopped: the profile screen did not open the main menu");
                return;
            }
            press(0, PAD_A, now);
        }
        break;
    case ST_MAIN:                   /* Down to Arcade, A; bg_arcade follows */
        if (port_wanted()) {
            static int target = -1, waited;
            /* The main menu's list object does not report its item count the
             * way the options list does (it reads 0), so count to Options:
             * six Downs from Career on the shipped order. The item count is
             * used when it is readable, and MC3_MAIN_DOWNS still overrides. */
            if (target < 0) {
                const char *e = getenv("MC3_MAIN_DOWNS");
                int items = mc3_menu_item_count();
                if (e && *e) {
                    target = atoi(e);
                } else if (items > 0) {
                    target = items - 1;
                } else if (++waited >= 45) {
                    target = 6;
                    fprintf(stderr, "[AUTOSTART] %s main menu: no item count,"
                            " using the shipped order (Options is 6 Downs)\n",
                            stamp());
                }
            }
            if (target >= 0)
                downs_to_arcade = target;
        }
        if (s_downs < downs_to_arcade) {
            if (ready(now, 3000, 400)) {
                press(PAD_DOWN, 0, now);
                s_downs++;
            }
        } else if (s_downs == downs_to_arcade) {
            if (ready(now, 3000, 600)) {
                press(0, PAD_A, now);
                s_downs++;          /* past the count: A is pressed once */
            }
        } else if (now - s_press_ms > 20000) {
            finish("stopped: Arcade did not open");
            return;
        }
        break;
    case ST_ARCADE:                 /* A through Arcade's setup screens */
        if (ready(now, 3000, 3000)) {
            if (s_presses >= 6) {
                finish("stopped: Arcade did not start the cruise");
                return;
            }
            press(0, PAD_A, now);
        }
        break;
    case ST_NETWORK:                /* A through Networking's screens */
        /* Free Roam is behind the screen's first item; each press opens the
         * next screen, and the last of them loads the cruise (which the game
         * reports as in_game, ending this walk). A screen that never answers
         * still stops the walk, so this cannot spin for ever. */
        if (host_wanted()) {
            /* Right once: the Networking screen lays its two items out
             * side by side (Xbox Live, System Link), so Down did not move and
             * the walk entered free roam instead. Then A through create. */
            if (s_downs < 1) {
                if (ready_busy(now, 2500, 1500)) {
                    press(PAD_RIGHT, 0, now);
                    s_downs++;
                }
            } else if (s_downs < 5) {
                if (ready_busy(now, 2500, 2000)) {
                    press(0, PAD_A, now);
                    s_downs++;
                }
            } else {
                finish("hosting: match created, waiting for a player");
                return;
            }
            break;
        }
        if (ready_busy(now, 2500, 2500)) {
            if (s_presses >= 8) {
                finish("stopped: Networking did not start the cruise");
                return;
            }
            press(0, PAD_A, now);
        }
        break;
    case ST_OPTIONS:                /* Down to Port Options (the last item), A */
        if (s_downs < 7) {
            if (ready(now, 1500, 450)) {
                if (port_wanted()) {
                    char tag[32];
                    snprintf(tag, sizeof tag, "options down %d", s_downs);
                    mc3_menu_debug_dump(tag);
                }
                press(PAD_DOWN, 0, now);
                s_downs++;
            }
        } else if (s_downs < 8) {
            if (ready(now, 1500, 600)) {
                press(0, PAD_A, now);
                s_downs++;
                s_nav = 0;
            }
        } else if (!mc3_port_options_open()) {
            /* The A landed on a different item (the parent list's cursor
             * position varies): say so instead of pressing keys blindly on
             * whatever screen did open. */
            if (now - s_press_ms > 6000) {
                finish("stopped: Port Options did not open from the last item");
                return;
            }
        } else if (!port_press()) {
            finish("parked on Port Options");
            return;
        } else {
            /* The two online rows are the last two before BACK: 13 settings,
             * then ROOM, then FIND A ROOM. s_nav is the cursor, not the press
             * count -- s_presses is already 8 from getting here, which made
             * this press a setting row and toggled it.
             *
             * The page opens with the cursor where the parent screen left it
             * (observed: three Downs landed on ANTI-ALIASING, i.e. row 3), so
             * go to the top first: 20 Ups cover the page's 16 rows whether Up
             * clamps or wraps. */
            int row = port_press() == 1 ? 13 : 14;
            if (s_nav < 20) {
                if (ready(now, 900, 250)) {
                    press(PAD_UP, 0, now);
                    s_nav++;
                }
            } else if (s_nav < 20 + row) {
                if (ready(now, 900, 250)) {
                    press(PAD_DOWN, 0, now);
                    s_nav++;
                }
            } else if (s_nav == 20 + row) {
                if (ready(now, 900, 600)) {
                    press(0, PAD_A, now);
                    s_nav++;
                }
            } else {
                finish(port_press() == 1 ? "pressed ROOM" : "pressed FIND A ROOM");
                return;
            }
        }
        break;
    case ST_LOADING:                /* nothing to press: wait for the cruise */
        if (now - s_stage_ms > 300000)
            finish("stopped: the cruise did not start");
        break;
    }
}
