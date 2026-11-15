/**
 * Xbox Input Compatibility Layer
 *
 * Translates the Xbox controller API to a host gamepad backend.
 * Handles the structural differences between the Xbox gamepad
 * (analog buttons as bytes, separate trigger channels) and the host
 * (digital face buttons, trigger axes).
 *
 *   _WIN32 -> Windows XInput
 *   POSIX  -> SDL2 GameController
 */

#include "xinput_xbox.h"
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

static atomic_int g_rumble_strength = -1;

int xbox_InputGetRumbleStrength(void)
{
    int strength = atomic_load(&g_rumble_strength);
    if (strength < 0) {
        const char *v = getenv("MC3_RUMBLE_STRENGTH");
        strength = v ? atoi(v) : 100;
        if (strength < 0) strength = 0;
        if (strength > 200) strength = 200;
        atomic_store(&g_rumble_strength, strength);
    }
    return strength;
}

void xbox_InputSetRumbleStrength(int percent)
{
    if (percent < 0) percent = 0;
    if (percent > 200) percent = 200;
    atomic_store(&g_rumble_strength, percent);
}

static WORD rumble_scale(WORD value)
{
    int strength = xbox_InputGetRumbleStrength();
    {unsigned scaled=((unsigned)value*(unsigned)strength+50u)/100u;return (WORD)(scaled>65535u?65535u:scaled);}
}

/* ======================================================================== */
#if defined(_WIN32) && !defined(XBOXRECOMP_SDL_HOST)
/* ====================  XInput backend  ================================== */
/* ======================================================================== */

#include <xinput.h>
#pragma comment(lib, "xinput.lib")

static BOOL  g_controller_connected[XBOX_MAX_CONTROLLERS] = { FALSE };
static DWORD g_last_packet[XBOX_MAX_CONTROLLERS] = { 0 };

/* ---- keyboard, when there is no pad --------------------------------------
 *
 * A title that waits on PRESS START is unreachable on a machine with no
 * controller plugged in, which is most machines someone brings this up on.
 * The whole point of a recompilation is to be able to look at the thing
 * running, and a build nobody can press a button in cannot be looked at.
 *
 * Off by default, because a keyboard silently acting as player 1 is
 * surprising when a real pad is what you meant to use. RECOMP_KEYBOARD=1
 * turns it on. It only ever answers for port 0, and is merged on top of a
 * pad connected there, so a real controller keeps working.
 *
 * The keys are the ones a Dreamcast or Saturn emulator would pick, which is
 * the closest thing to a convention here:
 *
 *   arrows        d-pad             Enter      START
 *   Z X A S       A B X Y           Backspace  BACK
 *   Q E           white black       1 3        triggers
 *   numpad 8/2/4/6 left thumb       I/K/J/L    right thumb
 *
 * Keys come from the framebuffer window, which only receives them while it
 * has the focus, so typing in another window does not drive the game.
 */
static BOOL keyboard_enabled(void)
{
    static int on = -1;
    if (on < 0) {
        const char *v = getenv("RECOMP_KEYBOARD");
        on = (v && *v && *v != '0') ? 1 : 0;
    }
    return on ? TRUE : FALSE;
}

/* The framebuffer window records what is held; see src/video/fb_present.c.
 *
 * GetAsyncKeyState was the first attempt and reads nothing here -- under
 * Wine it answers about a state a GDI-drawing guest never touches. Going
 * through the window is also the better gate: a window only receives keys
 * while it has the focus, so there is no separate focus check to get wrong
 * and no way for typing in another application to drive the game.
 *
 * Declared rather than included so src/input does not depend on the video
 * module's include path. */
extern int xbox_FramebufferKeyDown(int vk);

static BOOL key_down(int vk)
{
    return xbox_FramebufferKeyDown(vk) ? TRUE : FALSE;
}

/* A thumb axis from two keys, at the full deflection a digital key implies. */
static SHORT axis_from_keys(int negative, int positive)
{
    int v = 0;
    if (key_down(negative)) v -= 32767;
    if (key_down(positive)) v += 32767;
    return (SHORT)v;
}

static void keyboard_state(XBOX_INPUT_STATE *pState)
{
    static DWORD packet;
    WORD b = 0;

    memset(pState, 0, sizeof(*pState));

    if (key_down(VK_UP))     b |= XBOX_GAMEPAD_DPAD_UP;
    if (key_down(VK_DOWN))   b |= XBOX_GAMEPAD_DPAD_DOWN;
    if (key_down(VK_LEFT))   b |= XBOX_GAMEPAD_DPAD_LEFT;
    if (key_down(VK_RIGHT))  b |= XBOX_GAMEPAD_DPAD_RIGHT;
    if (key_down(VK_RETURN)) b |= XBOX_GAMEPAD_START;
    if (key_down(VK_BACK))   b |= XBOX_GAMEPAD_BACK;
    if (key_down(VK_SHIFT))  b |= XBOX_GAMEPAD_LEFT_THUMB;
    if (key_down(VK_CONTROL))b |= XBOX_GAMEPAD_RIGHT_THUMB;
    pState->Gamepad.wButtons = b;

    /* Analog on the console, so a key is 255 rather than a flag -- a title
     * that reads these as a pressure never sees a press if they are 1. */
    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_A]        = key_down('Z') ? 255 : 0;
    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_B]        = key_down('X') ? 255 : 0;
    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_X]        = key_down('A') ? 255 : 0;
    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_Y]        = key_down('S') ? 255 : 0;
    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_WHITE]    = key_down('Q') ? 255 : 0;
    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_BLACK]    = key_down('E') ? 255 : 0;
    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_LTRIGGER] = key_down('1') ? 255 : 0;
    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_RTRIGGER] = key_down('3') ? 255 : 0;

    /* W/A/S/D would collide with the face buttons above, so the left thumb
     * shares the arrow keys' row on the numeric pad instead. */
    pState->Gamepad.sThumbLX = axis_from_keys(VK_NUMPAD4, VK_NUMPAD6);
    pState->Gamepad.sThumbLY = axis_from_keys(VK_NUMPAD2, VK_NUMPAD8);
    pState->Gamepad.sThumbRX = axis_from_keys('J', 'L');
    pState->Gamepad.sThumbRY = axis_from_keys('K', 'I');

    /* The title's input layer looks for button edges, so the packet number
     * has to move whenever the state does or a press is never noticed. */
    pState->dwPacketNumber = ++packet;
}

void xbox_InputInit(void)
{
    for (DWORD i = 0; i < XBOX_MAX_CONTROLLERS; i++) {
        XINPUT_STATE state;
        DWORD result = XInputGetState(i, &state);
        g_controller_connected[i] = (result == ERROR_SUCCESS);
    }
}

DWORD xbox_InputGetState(DWORD dwPort, XBOX_INPUT_STATE *pState)
{
    XINPUT_STATE xi_state;
    DWORD result;

    if (dwPort >= XBOX_MAX_CONTROLLERS || !pState)
        return ERROR_DEVICE_NOT_CONNECTED;

    result = XInputGetState(dwPort, &xi_state);
    if (result != ERROR_SUCCESS) {
        g_controller_connected[dwPort] = FALSE;
        if (dwPort == 0 && keyboard_enabled()) {
            keyboard_state(pState);
            return ERROR_SUCCESS;
        }
        return result;
    }

    g_controller_connected[dwPort] = TRUE;
    g_last_packet[dwPort] = xi_state.dwPacketNumber;

    memset(pState, 0, sizeof(XBOX_INPUT_STATE));
    pState->dwPacketNumber = xi_state.dwPacketNumber;
    pState->Gamepad.wButtons = xi_state.Gamepad.wButtons & 0x00FF;

    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_A] =
        (xi_state.Gamepad.wButtons & XINPUT_GAMEPAD_A) ? 255 : 0;
    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_B] =
        (xi_state.Gamepad.wButtons & XINPUT_GAMEPAD_B) ? 255 : 0;
    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_X] =
        (xi_state.Gamepad.wButtons & XINPUT_GAMEPAD_X) ? 255 : 0;
    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_Y] =
        (xi_state.Gamepad.wButtons & XINPUT_GAMEPAD_Y) ? 255 : 0;
    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_BLACK] =
        (xi_state.Gamepad.wButtons & XINPUT_GAMEPAD_LEFT_SHOULDER) ? 255 : 0;
    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_WHITE] =
        (xi_state.Gamepad.wButtons & XINPUT_GAMEPAD_RIGHT_SHOULDER) ? 255 : 0;
    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_LTRIGGER] = xi_state.Gamepad.bLeftTrigger;
    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_RTRIGGER] = xi_state.Gamepad.bRightTrigger;

    pState->Gamepad.sThumbLX = xi_state.Gamepad.sThumbLX;
    pState->Gamepad.sThumbLY = xi_state.Gamepad.sThumbLY;
    pState->Gamepad.sThumbRX = xi_state.Gamepad.sThumbRX;
    pState->Gamepad.sThumbRY = xi_state.Gamepad.sThumbRY;

    /* Merge the keyboard on top rather than only standing in for a missing
     * pad.
     *
     * The first version put the keyboard behind XInput's failure, on the
     * assumption that with nothing plugged in the call would fail. It does
     * not: under Wine XInputGetState returns ERROR_SUCCESS and a gamepad
     * with every button at rest, so the fallback was unreachable and
     * pressing a key did nothing at all.
     *
     * Merging is also the better rule. A real pad keeps working -- its
     * buttons are already in pState and the keyboard only adds to them --
     * and there is no special case left to get wrong. */
    if (dwPort == 0 && keyboard_enabled()) {
        XBOX_INPUT_STATE kb;
        int i;
        keyboard_state(&kb);
        pState->Gamepad.wButtons |= kb.Gamepad.wButtons;
        for (i = 0; i < 8; i++)
            if (kb.Gamepad.bAnalogButtons[i] > pState->Gamepad.bAnalogButtons[i])
                pState->Gamepad.bAnalogButtons[i] = kb.Gamepad.bAnalogButtons[i];
        if (kb.Gamepad.sThumbLX) pState->Gamepad.sThumbLX = kb.Gamepad.sThumbLX;
        if (kb.Gamepad.sThumbLY) pState->Gamepad.sThumbLY = kb.Gamepad.sThumbLY;
        if (kb.Gamepad.sThumbRX) pState->Gamepad.sThumbRX = kb.Gamepad.sThumbRX;
        if (kb.Gamepad.sThumbRY) pState->Gamepad.sThumbRY = kb.Gamepad.sThumbRY;
        /* The input layer records edges, so an unchanged packet number is
         * read as the same state and the press never happens. */
        pState->dwPacketNumber = kb.dwPacketNumber;
    }

    return ERROR_SUCCESS;
}

DWORD xbox_InputSetState(DWORD dwPort, const XBOX_VIBRATION *pVibration)
{
    XINPUT_VIBRATION xi_vib;

    if (dwPort >= XBOX_MAX_CONTROLLERS || !pVibration)
        return ERROR_DEVICE_NOT_CONNECTED;

    xi_vib.wLeftMotorSpeed = rumble_scale(pVibration->wLeftMotorSpeed);
    xi_vib.wRightMotorSpeed = rumble_scale(pVibration->wRightMotorSpeed);
    return XInputSetState(dwPort, &xi_vib);
}

BOOL xbox_InputIsConnected(DWORD dwPort)
{
    if (dwPort >= XBOX_MAX_CONTROLLERS) return FALSE;
    return g_controller_connected[dwPort];
}

DWORD xbox_InputGetCapabilities(DWORD dwPort, DWORD dwFlags, XBOX_INPUT_CAPABILITIES *pCaps)
{
    XINPUT_CAPABILITIES xi_caps;
    DWORD result;

    if (dwPort >= XBOX_MAX_CONTROLLERS || !pCaps)
        return ERROR_DEVICE_NOT_CONNECTED;

    result = XInputGetCapabilities(dwPort, dwFlags, &xi_caps);
    if (result != ERROR_SUCCESS) return result;

    memset(pCaps, 0, sizeof(XBOX_INPUT_CAPABILITIES));
    pCaps->Type = xi_caps.Type;
    pCaps->SubType = xi_caps.SubType;
    pCaps->Flags = xi_caps.Flags;
    return ERROR_SUCCESS;
}

/* ======================================================================== */
#else /* !_WIN32 */
/* ====================  SDL2 GameController backend  ===================== */
/* ======================================================================== */

#include <SDL.h>
#include <stdio.h>
#include "../platform/host_main.h"

static SDL_GameController *g_pads[XBOX_MAX_CONTROLLERS];
static BOOL  g_controller_connected[XBOX_MAX_CONTROLLERS];
static DWORD g_packet[XBOX_MAX_CONTROLLERS];
#if defined(_WIN32) && defined(XBOXRECOMP_SDL_HOST)
static SDL_Joystick *g_wheel;
static int wheel_enabled(void){const char *v=getenv("MC3_WHEEL");return v&&*v=='1';}
static int wheel_device(void){const char *v=getenv("MC3_WHEEL_DEVICE");return v&&strcmp(v,"auto")?atoi(v):-1;}
static int wheel_setting(const char *key,int fallback){const char *v=getenv(key);int n=v?atoi(v):fallback;return n<0?fallback:n;}
static int wheel_invert(const char *key){const char *v=getenv(key);return v&&*v=='1';}
static int wheel_rumble_strength(void){const char *v=getenv("MC3_WHEEL_RUMBLE");int n=v?atoi(v):100;return n<0?0:n>200?200:n;}
static void wheel_open(void)
{
    if(!wheel_enabled()){if(g_wheel){SDL_JoystickClose(g_wheel);g_wheel=NULL;}return;}
    if(g_wheel&&!SDL_JoystickGetAttached(g_wheel)){fprintf(stderr,"  [INPUT] raw wheel removed\n");SDL_JoystickClose(g_wheel);g_wheel=NULL;}
    if(!g_wheel){
        int count=SDL_NumJoysticks(),selected=wheel_device(),candidate=-1;
        if(selected>=0&&selected<count)candidate=selected;
        else {
            for(int i=0;i<count;i++)if(SDL_JoystickGetDeviceType(i)==SDL_JOYSTICK_TYPE_WHEEL){candidate=i;break;}
            if(candidate<0)for(int i=0;i<count;i++)if(!SDL_IsGameController(i)){
                SDL_Joystick *probe=SDL_JoystickOpen(i);int axes=probe?SDL_JoystickNumAxes(probe):0;if(probe)SDL_JoystickClose(probe);
                if(axes>=3){candidate=i;break;}
            }
        }
        if(candidate>=0){SDL_Joystick *j=SDL_JoystickOpen(candidate);if(j&&SDL_JoystickNumAxes(j)>=3){g_wheel=j;fprintf(stderr,"  [INPUT] raw wheel: %s (%d axes, %d buttons)\n",SDL_JoystickName(j)?SDL_JoystickName(j):"?",SDL_JoystickNumAxes(j),SDL_JoystickNumButtons(j));}else if(j)SDL_JoystickClose(j);}
    }
    if(g_wheel){
        SDL_JoystickID wid=SDL_JoystickInstanceID(g_wheel);
        for(int slot=0;slot<XBOX_MAX_CONTROLLERS;slot++)if(g_pads[slot]&&SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pads[slot]))==wid){SDL_GameControllerClose(g_pads[slot]);g_pads[slot]=NULL;g_controller_connected[slot]=FALSE;}
        g_controller_connected[0]=TRUE;
    }
}
static SHORT wheel_steer(void)
{
    int axis=wheel_setting("MC3_WHEEL_STEER_AXIS",0),v=0,dead=wheel_setting("MC3_WHEEL_DEADZONE",1638);
    if(dead>32766)dead=32766;
    if(axis<SDL_JoystickNumAxes(g_wheel))v=SDL_JoystickGetAxis(g_wheel,axis);
    if(wheel_invert("MC3_WHEEL_STEER_INVERT"))v=-v;
    if(v> -dead&&v<dead)return 0;
    if(v>0)v=(v-dead)*32767/(32767-dead);else v=(v+dead)*32768/(32768-dead);
    if(v>32767)v=32767;if(v< -32768)v=-32768;return (SHORT)v;
}
static BYTE wheel_pedal(const char *key,int fallback,const char *invert)
{
    int axis=wheel_setting(key,fallback),v=0;if(axis<SDL_JoystickNumAxes(g_wheel))v=SDL_JoystickGetAxis(g_wheel,axis);
    if(wheel_invert(invert))v=-v;if(v< -32768)v=-32768;if(v>32767)v=32767;return (BYTE)((v+32768)>>8);
}
static void wheel_state(XBOX_INPUT_STATE *pState)
{
    static const WORD mapped[]={0,0,0,0,0,0,XBOX_GAMEPAD_BACK,XBOX_GAMEPAD_START,XBOX_GAMEPAD_LEFT_THUMB,XBOX_GAMEPAD_RIGHT_THUMB};
    memset(pState,0,sizeof *pState);pState->dwPacketNumber=++g_packet[0];SDL_JoystickUpdate();
    pState->Gamepad.sThumbLX=wheel_steer();
    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_RTRIGGER]=wheel_pedal("MC3_WHEEL_THROTTLE_AXIS",1,"MC3_WHEEL_THROTTLE_INVERT");
    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_LTRIGGER]=wheel_pedal("MC3_WHEEL_BRAKE_AXIS",2,"MC3_WHEEL_BRAKE_INVERT");
    for(int i=0;i<SDL_JoystickNumButtons(g_wheel)&&i<16;i++)if(SDL_JoystickGetButton(g_wheel,i)){
        if(i<4)pState->Gamepad.bAnalogButtons[i]=255;
        if(i>=6&&i<(int)(sizeof mapped/sizeof mapped[0]))pState->Gamepad.wButtons|=mapped[i];
        if(i==4)pState->Gamepad.bAnalogButtons[XBOX_BUTTON_BLACK]=255;if(i==5)pState->Gamepad.bAnalogButtons[XBOX_BUTTON_WHITE]=255;
    }
    if(SDL_JoystickNumHats(g_wheel)>0){Uint8 h=SDL_JoystickGetHat(g_wheel,0);if(h&SDL_HAT_UP)pState->Gamepad.wButtons|=XBOX_GAMEPAD_DPAD_UP;if(h&SDL_HAT_DOWN)pState->Gamepad.wButtons|=XBOX_GAMEPAD_DPAD_DOWN;if(h&SDL_HAT_LEFT)pState->Gamepad.wButtons|=XBOX_GAMEPAD_DPAD_LEFT;if(h&SDL_HAT_RIGHT)pState->Gamepad.wButtons|=XBOX_GAMEPAD_DPAD_RIGHT;}
}
#endif

/* Open the attached game controllers into free ports, closing any that
 * went away. Called at start and again every second while a port is
 * empty, so a pad plugged in mid-game is picked up (SDL's device-added
 * events go to whichever thread pumps them, not here). */
static void open_controllers(void)
{
    int i, slot;
#if defined(_WIN32) && defined(XBOXRECOMP_SDL_HOST)
    wheel_open();
#endif
    for (slot = 0; slot < XBOX_MAX_CONTROLLERS; slot++)
        if (g_pads[slot] && !SDL_GameControllerGetAttached(g_pads[slot])) {
            fprintf(stderr, "  [INPUT] pad %d: %s removed\n", slot + 1,
                    SDL_GameControllerName(g_pads[slot]) ? SDL_GameControllerName(g_pads[slot]) : "?");
            SDL_GameControllerClose(g_pads[slot]);
            g_pads[slot] = NULL;
            g_controller_connected[slot] = FALSE;
        }
    for (i = 0; i < SDL_NumJoysticks(); i++) {
        SDL_JoystickID id;
        int open = 0;
        if (!SDL_IsGameController(i))
            continue;
        id = SDL_JoystickGetDeviceInstanceID(i);
#if defined(_WIN32) && defined(XBOXRECOMP_SDL_HOST)
        if(g_wheel&&id==SDL_JoystickInstanceID(g_wheel))continue;
#endif
        for (slot = 0; slot < XBOX_MAX_CONTROLLERS; slot++)
            if (g_pads[slot]
                && SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pads[slot])) == id)
                open = 1;
        if (open)
            continue;
#if defined(__ANDROID__)
        {
            /* SDL lists a keyboard with a d-pad (the emulator's, many real
             * ones) as a joystick: no axes at all. Not a pad -- its keys
             * reach the keyboard (android/MainActivity.java). */
            static SDL_JoystickID skipped[8];
            static int nskip;
            int k, skip = 0;
            for (k = 0; k < nskip; k++)
                skip |= skipped[k] == id;
            if (!skip) {
                SDL_Joystick *j = SDL_JoystickOpen(i);
                if (j && SDL_JoystickNumAxes(j) == 0) {
                    fprintf(stderr, "  [INPUT] %s: no sticks, a keyboard; not a pad\n",
                            SDL_JoystickName(j) ? SDL_JoystickName(j) : "?");
                    if (nskip < 8)
                        skipped[nskip++] = id;
                    skip = 1;
                }
                if (j)
                    SDL_JoystickClose(j);
            }
            if (skip)
                continue;
        }
#endif
#if defined(_WIN32) && defined(XBOXRECOMP_SDL_HOST)
        for (slot = g_wheel?1:0; slot < XBOX_MAX_CONTROLLERS && g_pads[slot]; slot++) { }
#else
        for (slot = 0; slot < XBOX_MAX_CONTROLLERS && g_pads[slot]; slot++) { }
#endif
        if (slot == XBOX_MAX_CONTROLLERS)
            break;
        if ((g_pads[slot] = SDL_GameControllerOpen(i)) != NULL) {
            g_controller_connected[slot] = TRUE;
            fprintf(stderr, "  [INPUT] pad %d: %s\n", slot + 1,
                    SDL_GameControllerName(g_pads[slot]) ? SDL_GameControllerName(g_pads[slot]) : "?");
        }
    }
}

/* Lazily, from the first state read: nothing called xbox_InputInit on
 * Linux, so SDL's controller subsystem never started and a real pad (a
 * DualSense, 2026-10-21) was never read -- only the keyboard worked.
 * Joystick input is taken with the window unfocused too: the reader is
 * the emulated USB pad, not a window. */
static void input_poll_devices(void)
{
    static int inited;
    static Uint32 last;
    Uint32 now;
    if (!inited) {
        inited = 1;
        SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
        if (!SDL_WasInit(SDL_INIT_GAMECONTROLLER)
            && SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) {
            fprintf(stderr, "  [INPUT] no game controllers: %s\n", SDL_GetError());
            return;
        }
        open_controllers();
        last = SDL_GetTicks();
        return;
    }
    now = SDL_GetTicks();
    if (now - last >= 1000) {
        last = now;
        SDL_GameControllerUpdate();
        open_controllers();
    }
}

/* Where the main thread owns the window system (macOS,
 * platform/host_main.c) controllers are opened and rescanned there, after
 * each event pump; the readers below only read. */
static void input_main_tick(void) { input_poll_devices(); }

void xbox_InputInit(void)
{
    input_poll_devices();
    xbox_host_main_tick(input_main_tick);
}

#if defined(__SWITCH__)
#include "xinput_nx.h"
#else
/* The keyboard as player 1 (sdl_keyboard.c). */
int  xbox_sdl_keyboard_enabled(void);
void xbox_sdl_keyboard_state(XBOX_INPUT_STATE *s);
/* The on-screen pad (touch_pad.c). */
void xbox_touchpad_controller(int connected);
int  xbox_touchpad_shown(void);
void xbox_touchpad_state(XBOX_INPUT_STATE *s);
#endif

DWORD xbox_InputGetState(DWORD dwPort, XBOX_INPUT_STATE *pState)
{
    if (dwPort >= XBOX_MAX_CONTROLLERS || !pState)
        return ERROR_DEVICE_NOT_CONNECTED;

#if defined(__SWITCH__)
    /* The console's own controllers, mapped by position (xinput_nx.c). */
    {
        uint16_t dig;
        uint8_t ana[8];
        int16_t th[4];
        int i;

        if (!xbox_nx_pad_read(dwPort, &dig, ana, th)) {
            g_controller_connected[dwPort] = FALSE;
            return ERROR_DEVICE_NOT_CONNECTED;
        }
        g_controller_connected[dwPort] = TRUE;
        memset(pState, 0, sizeof(XBOX_INPUT_STATE));
        pState->dwPacketNumber = ++g_packet[dwPort];
        pState->Gamepad.wButtons = dig;
        for (i = 0; i < 8; i++)
            pState->Gamepad.bAnalogButtons[i] = ana[i];
        pState->Gamepad.sThumbLX = th[0];
        pState->Gamepad.sThumbLY = th[1];
        pState->Gamepad.sThumbRX = th[2];
        pState->Gamepad.sThumbRY = th[3];
        return ERROR_SUCCESS;
    }
#endif

    if (dwPort == 0 && xbox_host_on_main())
        input_poll_devices();
#if defined(_WIN32) && defined(XBOXRECOMP_SDL_HOST)
    if(dwPort==0&&g_wheel&&SDL_JoystickGetAttached(g_wheel)){g_controller_connected[0]=TRUE;wheel_state(pState);return ERROR_SUCCESS;}
#endif
    SDL_GameController *c = g_pads[dwPort];
    if (!c || !SDL_GameControllerGetAttached(c)) {
#if !defined(__SWITCH__)
        /* No pad on port 0: the keyboard can be player 1 (sdl_keyboard.c),
         * and the on-screen pad (touch_pad.c). */
        if (dwPort == 0)
            xbox_touchpad_controller(0);
        if (dwPort == 0 && (xbox_sdl_keyboard_enabled() || xbox_touchpad_shown())) {
            g_controller_connected[0] = TRUE;
            memset(pState, 0, sizeof(XBOX_INPUT_STATE));
            pState->dwPacketNumber = ++g_packet[0];
            if (xbox_sdl_keyboard_enabled())
                xbox_sdl_keyboard_state(pState);
            xbox_touchpad_state(pState);
            return ERROR_SUCCESS;
        }
#endif
        g_controller_connected[dwPort] = FALSE;
        memset(pState,0,sizeof *pState);
        return ERROR_DEVICE_NOT_CONNECTED;
    }

    SDL_GameControllerUpdate();
    g_controller_connected[dwPort] = TRUE;

    memset(pState, 0, sizeof(XBOX_INPUT_STATE));
    pState->dwPacketNumber = ++g_packet[dwPort];

    WORD btn = 0;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_UP))    btn |= XBOX_GAMEPAD_DPAD_UP;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_DOWN))  btn |= XBOX_GAMEPAD_DPAD_DOWN;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_LEFT))  btn |= XBOX_GAMEPAD_DPAD_LEFT;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) btn |= XBOX_GAMEPAD_DPAD_RIGHT;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_START))      btn |= XBOX_GAMEPAD_START;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_BACK))       btn |= XBOX_GAMEPAD_BACK;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_LEFTSTICK))  btn |= XBOX_GAMEPAD_LEFT_THUMB;
    if (SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_RIGHTSTICK)) btn |= XBOX_GAMEPAD_RIGHT_THUMB;
    pState->Gamepad.wButtons = btn;

    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_A] =
        SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_A) ? 255 : 0;
    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_B] =
        SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_B) ? 255 : 0;
    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_X] =
        SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_X) ? 255 : 0;
    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_Y] =
        SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_Y) ? 255 : 0;
    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_BLACK] =
        SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_LEFTSHOULDER) ? 255 : 0;
    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_WHITE] =
        SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) ? 255 : 0;

    /* SDL trigger axes are 0..32767 -> Xbox analog button 0..255 */
    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_LTRIGGER] =
        (BYTE)(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT) >> 7);
    pState->Gamepad.bAnalogButtons[XBOX_BUTTON_RTRIGGER] =
        (BYTE)(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) >> 7);

    /* SDL Y axis points down; the Xbox Y axis points up -- invert.
     * Use (-1 - v) so v = -32768 does not overflow SHORT. */
    pState->Gamepad.sThumbLX = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTX);
    pState->Gamepad.sThumbLY =
        (SHORT)(-1 - SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_LEFTY));
    pState->Gamepad.sThumbRX = SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTX);
    pState->Gamepad.sThumbRY =
        (SHORT)(-1 - SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_RIGHTY));

#if !defined(__SWITCH__)
    if (dwPort == 0 && xbox_sdl_keyboard_enabled())
        xbox_sdl_keyboard_state(pState);            /* on top of the pad */
    if (dwPort == 0) {
        xbox_touchpad_controller(1);                /* a pad: the overlay goes */
        xbox_touchpad_state(pState);                /* unless shown anyway (RECOMP_TOUCH_PAD=1) */
    }
#endif
    return ERROR_SUCCESS;
}

DWORD xbox_InputSetState(DWORD dwPort, const XBOX_VIBRATION *pVibration)
{
    if (dwPort >= XBOX_MAX_CONTROLLERS || !pVibration)
        return ERROR_DEVICE_NOT_CONNECTED;

#if defined(__SWITCH__)
    /* The console's own controllers (xinput_nx.c), not SDL's. */
    xbox_nx_pad_rumble(dwPort, pVibration->wLeftMotorSpeed,
                       pVibration->wRightMotorSpeed);
    return ERROR_SUCCESS;
#else
#if defined(_WIN32) && defined(XBOXRECOMP_SDL_HOST) && SDL_VERSION_ATLEAST(2,0,18)
    if(dwPort==0&&g_wheel){int strength=wheel_rumble_strength();unsigned l=((unsigned)pVibration->wLeftMotorSpeed*(unsigned)strength+50u)/100u,r=((unsigned)pVibration->wRightMotorSpeed*(unsigned)strength+50u)/100u;if(l>65535u)l=65535u;if(r>65535u)r=65535u;if(SDL_JoystickRumble(g_wheel,(Uint16)l,(Uint16)r,1000)==0)return ERROR_SUCCESS;return ERROR_DEVICE_NOT_CONNECTED;}
#endif
    SDL_GameController *c = g_pads[dwPort];
    if (!c) return ERROR_DEVICE_NOT_CONNECTED;

    /* SDL rumble needs a duration; refresh for ~1s on each call (the game
     * polls vibration continuously). */
    if(SDL_GameControllerRumble(c, rumble_scale(pVibration->wLeftMotorSpeed),
                             rumble_scale(pVibration->wRightMotorSpeed), 1000)!=0){
        static unsigned reported;
        if(reported++<4)SDL_Log("Controller vibration failed on port %u: %s",(unsigned)dwPort,SDL_GetError());
        return ERROR_DEVICE_NOT_CONNECTED;
    }
    return ERROR_SUCCESS;
#endif
}

BOOL xbox_InputIsConnected(DWORD dwPort)
{
    if (dwPort >= XBOX_MAX_CONTROLLERS) return FALSE;
    return g_controller_connected[dwPort];
}

DWORD xbox_InputGetCapabilities(DWORD dwPort, DWORD dwFlags, XBOX_INPUT_CAPABILITIES *pCaps)
{
    (void)dwFlags;
    if (dwPort >= XBOX_MAX_CONTROLLERS || !pCaps)
        return ERROR_DEVICE_NOT_CONNECTED;
    if (!g_pads[dwPort]) {
#if defined(_WIN32) && defined(XBOXRECOMP_SDL_HOST)
        if(dwPort==0&&g_wheel){memset(pCaps,0,sizeof *pCaps);pCaps->Type=1;pCaps->SubType=0x10;return ERROR_SUCCESS;}
#endif
        return ERROR_DEVICE_NOT_CONNECTED;
    }

    memset(pCaps, 0, sizeof(XBOX_INPUT_CAPABILITIES));
    pCaps->Type    = 1;   /* XINPUT_DEVTYPE_GAMEPAD */
    pCaps->SubType = 1;   /* XINPUT_DEVSUBTYPE_GAMEPAD */
    pCaps->Flags   = 0;
    return ERROR_SUCCESS;
}

#endif /* _WIN32 */
