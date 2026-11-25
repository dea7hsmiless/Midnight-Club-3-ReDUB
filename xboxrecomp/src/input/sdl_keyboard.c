/* Optional title-owned text editor. SDL work stays on the window thread;
 * callbacks only queue/edit bounded text and never call guest code. */
#include <SDL.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../platform/host_main.h"
typedef void (*xbox_text_callback)(const char *);
typedef void (*xbox_text_key_callback)(int);
static _Atomic(xbox_text_callback) s_text_callback;
static _Atomic(xbox_text_key_callback) s_text_key_callback;
static atomic_int s_text_requested;
void xbox_text_input_set(int on,xbox_text_callback text,xbox_text_key_callback key)
{
    atomic_store(&s_text_callback,text);atomic_store(&s_text_key_callback,key);
    atomic_store(&s_text_requested,on);
}
static void text_input_on_main(void *arg)
{
    if(*(int *)arg)SDL_StartTextInput();else SDL_StopTextInput();
}
void xbox_text_input_frame(void)
{
    static int applied;
    int on=atomic_load(&s_text_requested);
    if(on==applied)return;
    applied=on;
#if (!defined(_WIN32) || defined(XBOXRECOMP_SDL_HOST)) && !defined(__SWITCH__)
    extern void xbox_sdl_keyboard_reset(void);xbox_sdl_keyboard_reset();
#endif
    xbox_host_main_call(text_input_on_main,&on);
}
int xbox_text_input_event(const SDL_Event *e)
{
    if(!atomic_load(&s_text_requested))return 0;
    if(e->type==SDL_TEXTINPUT) {
        xbox_text_callback fn=atomic_load(&s_text_callback);if(fn)fn(e->text.text);
        return 1;
    }
    if(e->type==SDL_KEYDOWN) {
        xbox_text_key_callback fn=atomic_load(&s_text_key_callback);
        int key=e->key.keysym.sym==SDLK_BACKSPACE?1:
                (e->key.keysym.sym==SDLK_RETURN||e->key.keysym.sym==SDLK_KP_ENTER)?2:
                e->key.keysym.sym==SDLK_ESCAPE?3:0;
        if(fn&&key)fn(key);
        return 1; /* typing must not also press the game's face buttons */
    }
    return e->type==SDL_KEYUP || e->type==SDL_TEXTEDITING;
}

/*
 * sdl_keyboard.c -- the keyboard as player 1 on Linux (SDL), as the Windows
 * build already has it (xinput_device.c, RECOMP_KEYBOARD=1).
 *
 * The SDL renderers drained the window's events and dropped them, and
 * xbox_FramebufferKeyDown is a stub returning 0 outside Windows, so on Linux
 * RECOMP_KEYBOARD did nothing: a title waiting on PRESS START could only be
 * passed with a real controller (Midnight Club 3, 2026-10-16). Now the
 * renderers hand key events here and xbox_InputGetState reads them as pad 1.
 *
 * Laid out so the buttons are where their names say and the arrows drive
 * (2026-10-21; the WASD layout of 2026-10-17 put A/B on J/K, which made
 * menus confusing, and left the Switch's L/R unmapped). The d-pad -- songs
 * and map in an MC3 race -- moves to I J K L so the arrows can steer. The
 * Windows build keeps its Z X A S scheme (xinput_device.c):
 *
 *   A B X Y       A B X Y                 Enter / Backspace  START / BACK
 *   left / right  left stick (steer, menus)
 *   up            right trigger (gas) + left stick up (menus; not while
 *                 driving where the title says so: xbox_keyboard_driving)
 *   down          left trigger (brake) + left stick down (likewise)
 *   I J K L       d-pad up / left / down / right
 *   Q / E         white / black (the Switch's L / R; also Shift / Ctrl)
 *   Z / C         left / right thumb click
 *   T F G H or numpad 8 4 2 6           right stick (camera)
 *   Space         A (MC3's handbrake: the Switch's A)
 *   Tab           BACK too (select, reachable while driving); so is
 *                 Android's Back (the gesture, Esc on some keyboards)
 *
 * Keys arrive only while the game window has the focus; losing it releases
 * everything, so a key held while switching away does not stay down.
 * Written by the window thread, read by the USB thread, one byte each.
 */
/* Set by a title's own code (see keyboard_stick_y); defined on every
 * platform so the title links where there is no SDL keyboard. */
int (*xbox_keyboard_driving)(void);

#if (!defined(_WIN32) || defined(XBOXRECOMP_SDL_HOST)) && !defined(__SWITCH__)

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "xinput_xbox.h"

static volatile unsigned char s_down[SDL_NUM_SCANCODES];

/* RECOMP_KEYBOARD_TRACE=1: each key as the pad layer gets it (a keyboard
 * that does nothing: do its keys arrive, and as what?). */
void xbox_sdl_keyboard_event(int scancode, int down)
{
    static int trace = -1;
    if (trace < 0) {
        const char *v = getenv("RECOMP_KEYBOARD_TRACE");
        trace = v && *v == '1';
    }
    if (trace && scancode >= 0 && scancode < SDL_NUM_SCANCODES
        && s_down[scancode] != (down ? 1 : 0))
        fprintf(stderr, "  [KEYBOARD] %s %s\n",
                SDL_GetScancodeName((SDL_Scancode)scancode), down ? "down" : "up");
    if (scancode >= 0 && scancode < SDL_NUM_SCANCODES)
        s_down[scancode] = down ? 1 : 0;
}

void xbox_sdl_keyboard_reset(void)
{
    memset((void *)s_down, 0, sizeof s_down);
}

int xbox_sdl_keyboard_down(int scancode)
{
    return scancode >= 0 && scancode < SDL_NUM_SCANCODES && s_down[scancode];
}

/* One window event, from the renderer's event loop (nv2a_gl, nv2a_vk). */
void xbox_sdl_keyboard_handle(const SDL_Event *e)
{
    switch (e->type) {
    case SDL_KEYDOWN:
    case SDL_KEYUP:
        xbox_sdl_keyboard_event(e->key.keysym.scancode, e->type == SDL_KEYDOWN);
        break;
    case SDL_WINDOWEVENT:
        if (e->window.event == SDL_WINDOWEVENT_FOCUS_LOST)
            xbox_sdl_keyboard_reset();
        break;
    default:
        break;
    }
}

int xbox_sdl_keyboard_enabled(void)
{
    static int on = -1;
    if (on < 0) {
        const char *v = getenv("RECOMP_KEYBOARD");
        on = (v && *v && *v != '0') ? 1 : 0;
    }
    return on;
}

/* Up/down move the left stick as well as the triggers, for menus -- but a
 * title may give stick up/down a job of its own while driving: the MC3
 * Remix raises and pulls in its race camera (2026-10-25), which the gas key
 * then did too. A title can say when the player is driving
 * (xbox_keyboard_driving, set by its own code); then up/down are the
 * triggers alone. RECOMP_KEYBOARD_STICK_Y=0 never moves the stick, =1
 * always does. */
static int keyboard_stick_y(void)
{
    static int mode = -2;
    if (mode == -2) {
        const char *v = getenv("RECOMP_KEYBOARD_STICK_Y");
        mode = v && *v == '0' ? 0 : v && *v == '1' ? 1 : -1;
    }
    if (mode >= 0)
        return mode;
    return !(xbox_keyboard_driving && xbox_keyboard_driving());
}

static int held(int sc)
{
    return sc >= 0 && sc < SDL_NUM_SCANCODES && s_down[sc];
}

static SHORT axis(int negative, int positive)
{
    int v = 0;
    if (held(negative)) v -= 32767;
    if (held(positive)) v += 32767;
    return (SHORT)v;
}

/* One axis from two key pairs (numpad or letters), clamped to the stick. */
static SHORT axis2(int neg1, int pos1, int neg2, int pos2)
{
    int v = axis(neg1, pos1) + axis(neg2, pos2);
    return (SHORT)(v > 32767 ? 32767 : v < -32767 ? -32767 : v);
}

/* ---- rebindable keys -----------------------------------------------------
 *
 * The defaults are the layout this port has always used: I/J/K/L for the
 * d-pad (so the left hand never leaves W/A/S/D country), arrows for steering
 * and the triggers, the numeric pad for the camera. Each action can be
 * rebound from the settings file, one key per line:
 *
 *   MC3_KEY_A=SPACE              an SDL key name (SDL_GetScancodeFromName)
 *   MC3_KEY_DPAD_UP=W            letters, digits, F1-F12, RETURN, LCTRL, ...
 *   MC3_KEY_RT=119               or a number, if a name is not enough
 *   MC3_KEY_LT=-1                -1 (or none) leaves the action unbound
 *
 * Read once, on the first state query. A name SDL does not know keeps the
 * default and says so: a typo that silently unbinds a button is a support
 * ticket, not a feature. The alternates the layout already had (Space for A,
 * Shift for WHITE, Ctrl for BLACK, Tab for BACK, the F/H/G/T camera keys)
 * stay, so binding these cannot take a working key away.
 */
enum {
    K_DPAD_UP, K_DPAD_DOWN, K_DPAD_LEFT, K_DPAD_RIGHT,
    K_START, K_BACK, K_LSTICK, K_RSTICK,
    K_A, K_B, K_X, K_Y, K_WHITE, K_BLACK, K_LT, K_RT,
    K_LS_LEFT, K_LS_RIGHT, K_LS_UP, K_LS_DOWN,
    K_RS_LEFT, K_RS_RIGHT, K_RS_UP, K_RS_DOWN,
    K_COUNT
};

static const struct { const char *name; int sc; } s_key_defaults[K_COUNT] = {
    { "DPAD_UP",      SDL_SCANCODE_I },  { "DPAD_DOWN",    SDL_SCANCODE_K },
    { "DPAD_LEFT",    SDL_SCANCODE_J },  { "DPAD_RIGHT",   SDL_SCANCODE_L },
    { "START",        SDL_SCANCODE_RETURN },
    { "BACK",         SDL_SCANCODE_BACKSPACE },
    { "LSTICK",       SDL_SCANCODE_Z },  { "RSTICK",       SDL_SCANCODE_C },
    { "A",            SDL_SCANCODE_A },  { "B",            SDL_SCANCODE_B },
    { "X",            SDL_SCANCODE_X },  { "Y",            SDL_SCANCODE_Y },
    { "WHITE",        SDL_SCANCODE_Q },  { "BLACK",        SDL_SCANCODE_E },
    { "LT",           SDL_SCANCODE_DOWN }, { "RT",         SDL_SCANCODE_UP },
    { "LSTICK_LEFT",  SDL_SCANCODE_LEFT }, { "LSTICK_RIGHT", SDL_SCANCODE_RIGHT },
    { "LSTICK_UP",    SDL_SCANCODE_UP },  { "LSTICK_DOWN", SDL_SCANCODE_DOWN },
    { "RSTICK_LEFT",  SDL_SCANCODE_KP_4 },{ "RSTICK_RIGHT", SDL_SCANCODE_KP_6 },
    { "RSTICK_UP",    SDL_SCANCODE_KP_8 },{ "RSTICK_DOWN", SDL_SCANCODE_KP_2 },
};

static int s_key[K_COUNT];
static int s_key_ready;

static int same_word(const char *a, const char *b)
{
    for (; *a && *b; a++, b++) {
        int ca = *a >= 'A' && *a <= 'Z' ? *a + 32 : *a;
        int cb = *b >= 'A' && *b <= 'Z' ? *b + 32 : *b;
        if (ca != cb) return 0;
    }
    return *a == 0 && *b == 0;
}

static void key_init(void)
{
    s_key_ready = 1;
    for (int i = 0; i < K_COUNT; i++) {
        char env[48];
        const char *v;
        s_key[i] = s_key_defaults[i].sc;
        snprintf(env, sizeof env, "MC3_KEY_%s", s_key_defaults[i].name);
        v = getenv(env);
        if (!v || !*v) continue;
        if (same_word(v, "none") || (v[0] == '-' && v[1] == '1' && !v[2])) {
            s_key[i] = -1;
        } else {
            char *end;
            long n = strtol(v, &end, 10);
            if (end != v && *end == 0) {
                s_key[i] = (int)n;
            } else {
                SDL_Scancode sc = SDL_GetScancodeFromName(v);
                if (sc == SDL_SCANCODE_UNKNOWN) {
                    fprintf(stderr, "  [INPUT] %s=%s: SDL does not know that key; "
                                    "keeping %s\n", env, v,
                            SDL_GetScancodeName((SDL_Scancode)s_key[i]));
                    continue;
                }
                s_key[i] = (int)sc;
            }
        }
        fprintf(stderr, "  [INPUT] keyboard %s is %s\n", s_key_defaults[i].name,
                s_key[i] < 0 ? "unbound" : SDL_GetScancodeName((SDL_Scancode)s_key[i]));
    }
}

static int kb(int action)
{
    if (!s_key_ready) key_init();
    return s_key[action];
}

/* MC3_KEY_TRACE=1: each action as the pad layer sees it. The key names in the
 * log say what the layout resolved to; this says whether a press actually
 * became a button, which is the half a rebind can break. */
static void key_trace(void)
{
    static int trace = -1;
    static int prev[K_COUNT];
    if (trace < 0) {
        const char *v = getenv("MC3_KEY_TRACE");
        trace = v && *v == '1';
    }
    if (!trace) return;
    for (int i = 0; i < K_COUNT; i++) {
        int now = held(kb(i));
        if (now != prev[i]) {
            fprintf(stderr, "  [KEYS] %s %s\n", s_key_defaults[i].name, now ? "down" : "up");
            prev[i] = now;
        }
    }
}

/* Merge the keyboard into a state (a real pad's, or an empty one): buttons
 * OR'ed, analog buttons at full pressure, sticks where a key is held. */
void xbox_sdl_keyboard_state(XBOX_INPUT_STATE *s)
{
    WORD b = 0;
    BYTE *an = s->Gamepad.bAnalogButtons;
    SHORT x;

    if (held(kb(K_DPAD_UP)))            b |= XBOX_GAMEPAD_DPAD_UP;
    if (held(kb(K_DPAD_DOWN)))          b |= XBOX_GAMEPAD_DPAD_DOWN;
    if (held(kb(K_DPAD_LEFT)))          b |= XBOX_GAMEPAD_DPAD_LEFT;
    if (held(kb(K_DPAD_RIGHT)))         b |= XBOX_GAMEPAD_DPAD_RIGHT;
    if (held(kb(K_START)) || held(SDL_SCANCODE_KP_ENTER))
                                        b |= XBOX_GAMEPAD_START;
    if (held(kb(K_BACK)) || held(SDL_SCANCODE_TAB)
        || held(SDL_SCANCODE_AC_BACK))  /* Android's Back */
                                        b |= XBOX_GAMEPAD_BACK;
    if (held(kb(K_LSTICK)))             b |= XBOX_GAMEPAD_LEFT_THUMB;
    if (held(kb(K_RSTICK)))             b |= XBOX_GAMEPAD_RIGHT_THUMB;
    s->Gamepad.wButtons |= b;

    if (held(kb(K_A)) || held(SDL_SCANCODE_SPACE)) an[XBOX_BUTTON_A] = 255;
    if (held(kb(K_B))) an[XBOX_BUTTON_B] = 255;
    if (held(kb(K_X))) an[XBOX_BUTTON_X] = 255;
    if (held(kb(K_Y))) an[XBOX_BUTTON_Y] = 255;
    if (held(kb(K_WHITE)) || held(SDL_SCANCODE_LSHIFT) || held(SDL_SCANCODE_RSHIFT))
        an[XBOX_BUTTON_WHITE] = 255;
    if (held(kb(K_BLACK)) || held(SDL_SCANCODE_LCTRL) || held(SDL_SCANCODE_RCTRL))
        an[XBOX_BUTTON_BLACK] = 255;
    if (held(kb(K_LT))) an[XBOX_BUTTON_LTRIGGER] = 255;
    if (held(kb(K_RT)))   an[XBOX_BUTTON_RTRIGGER] = 255;

    if ((x = axis(kb(K_LS_LEFT), kb(K_LS_RIGHT))) != 0) s->Gamepad.sThumbLX = x;
    if (keyboard_stick_y() && (x = axis(kb(K_LS_DOWN), kb(K_LS_UP))) != 0)
        s->Gamepad.sThumbLY = x;
    if ((x = axis2(kb(K_RS_LEFT), kb(K_RS_RIGHT), SDL_SCANCODE_F, SDL_SCANCODE_H)) != 0)
        s->Gamepad.sThumbRX = x;
    if ((x = axis2(kb(K_RS_DOWN), kb(K_RS_UP), SDL_SCANCODE_G, SDL_SCANCODE_T)) != 0)
        s->Gamepad.sThumbRY = x;

    key_trace();
}

#endif
