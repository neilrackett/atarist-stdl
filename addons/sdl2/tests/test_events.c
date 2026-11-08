/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The add-on's events on the host: STDL's events queued in
 * fake_stdl.c, pumped and read back as SDL2's. Keys with their
 * scancodes, key codes, modifiers, repeat flag and text; the mouse from
 * the logical size's top left; each joystick's state and events, and
 * its controller's buttons from STDL's held inputs, the device events
 * as STDL's count of them moves; the queue's ignore
 * list, flush, push and user events; SDL2's numbers where a program or
 * its saved key map would depend on them.
 */

#include "SDL.h"
#include "SDL_stdl.h"

#include <stdio.h>
#include <string.h>

#include "fake_stdl.h"

static int failures;

#define CHECK(c) do { \
        if (!(c)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            failures++; \
        } \
    } while (0)

/* the next event, or type 0, a VBL on: a pump at every call, as
 * every SDL_PollEvent pumped before the pump kept to a VBL */
static SDL_Event next(void)
{
    SDL_Event e;
    fake_vbl++;
    if (!SDL_PollEvent(&e)) {
        memset(&e, 0, sizeof(e));
    }
    return e;
}

/* a frame's drain: a VBL on, and everything it brings */
static void drain(void)
{
    SDL_Event e;
    fake_vbl++;
    while (SDL_PollEvent(&e)) {
    }
}

static void numbers(void)
{
    /* SDL2's own values, which saved key maps and binary layouts use */
    CHECK(SDL_SCANCODE_A == 4 && SDL_SCANCODE_RETURN == 40 &&
          SDL_SCANCODE_F1 == 58 && SDL_SCANCODE_UP == 82 &&
          SDL_SCANCODE_LCTRL == 224 && SDL_SCANCODE_RGUI == 231);
    CHECK(SDLK_UP == 0x40000052 && SDLK_F1 == 0x4000003A &&
          SDLK_RETURN == '\r' && SDLK_ESCAPE == 27 && SDLK_a == 'a');
    CHECK(KMOD_LSHIFT == 1 && KMOD_CTRL == 0xC0 && KMOD_ALT == 0x300 &&
          KMOD_CAPS == 0x2000);
    CHECK(SDL_QUIT == 0x100 && SDL_KEYDOWN == 0x300 &&
          SDL_TEXTINPUT == 0x303 && SDL_MOUSEBUTTONUP == 0x402 &&
          SDL_JOYBUTTONDOWN == 0x603 && SDL_JOYDEVICEADDED == 0x605 &&
          SDL_CONTROLLERBUTTONDOWN == 0x651 &&
          SDL_CONTROLLERDEVICEADDED == 0x653 && SDL_USEREVENT == 0x8000);
    CHECK(SDL_CONTROLLER_BUTTON_START == 6 &&
          SDL_CONTROLLER_BUTTON_DPAD_UP == 11 &&
          SDL_CONTROLLER_BUTTON_MAX == 15 &&
          SDL_CONTROLLER_AXIS_TRIGGERRIGHT == 5);
    CHECK(sizeof(SDL_Event) == 56);
}

static void keymap(void)
{
    static Uint8 seen[SDL_NUM_SCANCODES];
    unsigned sym;
    int clash = 0, unnamed = 0;
    for (sym = 0; sym < STDLK_LAST; sym++) {
        SDL_Event e;
        fake_key(1, sym, 0, 0);
        e = next();
        if (e.type != SDL_KEYDOWN) {
            /* a symbol STDL never sends */
            fake_key(0, sym, 0, 0);
            drain();
            continue;
        }
        if (e.key.keysym.scancode) {
            /* each scancode one key, and a key code for each */
            clash += seen[e.key.keysym.scancode]++ != 0;
            unnamed += e.key.keysym.sym == SDLK_UNKNOWN;
            CHECK(SDL_GetScancodeFromKey(e.key.keysym.sym) ==
                  e.key.keysym.scancode);
        }
        fake_key(0, sym, 0, 0);
        drain();
    }
    CHECK(clash == 0);
    CHECK(unnamed == 0);
    CHECK(SDL_GetKeyFromScancode(SDL_SCANCODE_A) == SDLK_a);
    CHECK(SDL_GetKeyFromScancode(SDL_SCANCODE_UP) == SDLK_UP);
    CHECK(SDL_GetScancodeFromKey(SDLK_SPACE) == SDL_SCANCODE_SPACE);
    CHECK(SDL_GetScancodeFromKey(SDLK_UNKNOWN) == SDL_SCANCODE_UNKNOWN);
}

static void keys(void)
{
    SDL_Event e;
    const Uint8 *state = SDL_GetKeyboardState(NULL);
    int n = 0;

    SDL_GetKeyboardState(&n);
    CHECK(n == SDL_NUM_SCANCODES);

    /* a key down, its text, a repeat, and up */
    fake_key(1, STDLK_a, STDL_KMOD_LSHIFT | 0x8000, 'A');
    e = next();
    CHECK(e.type == SDL_KEYDOWN && e.key.state == SDL_PRESSED);
    CHECK(e.key.keysym.sym == SDLK_a);
    CHECK(e.key.keysym.scancode == SDL_SCANCODE_A);
    CHECK(e.key.keysym.mod == KMOD_LSHIFT);    /* the joystick tag gone */
    CHECK(e.key.repeat == 0);
    CHECK(state[SDL_SCANCODE_A] == 1);
    e = next();
    CHECK(e.type == SDL_TEXTINPUT && strcmp(e.text.text, "A") == 0);
    fake_key(1, STDLK_a, 0, 'a');
    e = next();
    CHECK(e.type == SDL_KEYDOWN && e.key.repeat == 1);
    e = next();
    CHECK(e.type == SDL_TEXTINPUT && strcmp(e.text.text, "a") == 0);
    fake_key(0, STDLK_a, 0, 0);
    e = next();
    CHECK(e.type == SDL_KEYUP && e.key.state == SDL_RELEASED &&
          e.key.repeat == 0);
    CHECK(state[SDL_SCANCODE_A] == 0);
    CHECK(next().type == 0);

    /* keys without text, and keys with text stopped */
    fake_key(1, STDLK_UP, 0, 0);
    e = next();
    CHECK(e.type == SDL_KEYDOWN && e.key.keysym.sym == SDLK_UP &&
          e.key.keysym.scancode == SDL_SCANCODE_UP);
    CHECK(next().type == 0);
    fake_key(1, STDLK_F1, 0, 0);
    e = next();
    CHECK(e.key.keysym.sym == SDLK_F1);
    /* SDL2's two control keys that type nothing */
    fake_key(1, STDLK_PAUSE, 0, 0);
    e = next();
    CHECK(e.key.keysym.sym == SDLK_PAUSE && SDLK_PAUSE == 0x40000048);
    fake_key(1, STDLK_CLEAR, 0, 0);
    e = next();
    CHECK(e.key.keysym.sym == SDLK_CLEAR && SDLK_CLEAR == 0x4000009C);
    /* and the keypad's brackets, which STDL sends as ( and ) */
    fake_key(1, STDLK_LEFTPAREN, 0, '(');
    e = next();
    CHECK(e.key.keysym.sym == SDLK_KP_LEFTPAREN &&
          e.key.keysym.scancode == SDL_SCANCODE_KP_LEFTPAREN &&
          SDL_SCANCODE_KP_LEFTPAREN == 182);
    e = next();
    CHECK(e.type == SDL_TEXTINPUT && strcmp(e.text.text, "(") == 0);
    fake_key(1, STDLK_RETURN, 0, '\r');
    e = next();
    CHECK(e.key.keysym.sym == SDLK_RETURN);
    CHECK(next().type == 0);                   /* a control character */
    SDL_StopTextInput();
    CHECK(!SDL_IsTextInputActive());
    fake_key(1, STDLK_b, 0, 'b');
    e = next();
    CHECK(e.type == SDL_KEYDOWN);
    CHECK(next().type == 0);
    SDL_StartTextInput();
    CHECK(SDL_IsTextInputActive());

    fake_mod = STDL_KMOD_RCTRL | 0x8000;
    CHECK(SDL_GetModState() == KMOD_RCTRL);
    fake_mod = 0;
}

static void mouse(void)
{
    SDL_Event e;
    STDL_Event se;
    SDL_Window *w;
    SDL_Renderer *r;
    int x, y;

    memset(&se, 0, sizeof(se));
    se.type = STDL_MOUSEMOTION;
    se.motion.state = SDL_BUTTON_LMASK;
    se.motion.x = 100;
    se.motion.y = 50;
    se.motion.xrel = -3;
    se.motion.yrel = 4;

    /* no window: the screen's own */
    fake_event(&se);
    e = next();
    CHECK(e.type == SDL_MOUSEMOTION && e.motion.x == 100 &&
          e.motion.y == 50 && e.motion.xrel == -3 && e.motion.yrel == 4 &&
          e.motion.state == SDL_BUTTON_LMASK);

    /* a 256x224 window sits at 32, 8 */
    w = SDL_CreateWindow("t", 0, 0, 256, 224, 0);
    CHECK(w != NULL);
    fake_event(&se);
    e = next();
    CHECK(e.motion.x == 68 && e.motion.y == 42);
    r = SDL_CreateRenderer(w, -1, 0);
    CHECK(r != NULL);
    /* the renderer's logical size moves it */
    SDL_RenderSetLogicalSize(r, 320, 200);
    fake_event(&se);
    e = next();
    CHECK(e.motion.x == 100 && e.motion.y == 30);

    memset(&se, 0, sizeof(se));
    se.type = STDL_MOUSEBUTTONDOWN;
    se.button.button = STDL_BUTTON_RIGHT;
    se.button.x = 10;
    se.button.y = 25;
    fake_event(&se);
    e = next();
    CHECK(e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_RIGHT
          && e.button.state == SDL_PRESSED && e.button.clicks == 1 &&
          e.button.x == 10 && e.button.y == 5);
    se.type = STDL_MOUSEBUTTONUP;
    fake_event(&se);
    e = next();
    CHECK(e.type == SDL_MOUSEBUTTONUP && e.button.state == SDL_RELEASED);

    fake_mouse_x = 40;
    fake_mouse_y = 20;
    fake_mouse_buttons = SDL_BUTTON_LMASK | SDL_BUTTON_RMASK;
    CHECK(SDL_GetMouseState(&x, &y) == (SDL_BUTTON_LMASK | SDL_BUTTON_RMASK));
    CHECK(x == 40 && y == 0);
    CHECK(SDL_BUTTON(SDL_BUTTON_RIGHT) == 4);

    SDL_DestroyWindow(w);
}

static void joy_event(int which, int type, int index, int value)
{
    STDL_Event se;
    memset(&se, 0, sizeof(se));
    se.type = (Uint8)type;
    se.jaxis.which = (Uint8)which;
    /* and STDL's readers say what it says, as the core's do */
    if (type == STDL_JOYAXISMOTION) {
        se.jaxis.axis = (Uint8)index;
        se.jaxis.value = (Sint16)value;
        fake_axes[which][index] = value;
    } else if (type == STDL_JOYHATMOTION) {
        se.jhat.value = (Uint8)value;
        fake_hats[which] = value;
    } else {
        se.jbutton.button = (Uint8)index;
        se.jbutton.state = (Uint8)value;
    }
    fake_event(&se);
}

static void joy_axis(int axis, int value)
{
    joy_event(0, STDL_JOYAXISMOTION, axis, value);
}

/*
 * An input of joystick `which` held or let go, as STDL has it: its bit
 * (STDL_JoystickInputHeld, which the add-on reads), and the event STDL
 * sends with it - a button's for fire and the rest, a hat's with the
 * d-pad for a direction. STDL changes no input without one.
 */
static void held(int which, int input, int down)
{
    const uint32_t bit = 1u << input;
    uint32_t h;
    fake_helds[which] = down ? fake_helds[which] | bit
                             : fake_helds[which] & ~bit;
    h = fake_helds[which];
    if (input >= STDL_JOYSTICK_FIRE) {
        joy_event(which, down ? STDL_JOYBUTTONDOWN : STDL_JOYBUTTONUP,
                  input - STDL_JOYSTICK_FIRE, down);
    } else {
        joy_event(which, STDL_JOYHATMOTION, 0,
                  ((h >> STDL_JOYSTICK_UP) & 1) * SDL_HAT_UP |
                  ((h >> STDL_JOYSTICK_RIGHT) & 1) * SDL_HAT_RIGHT |
                  ((h >> STDL_JOYSTICK_DOWN) & 1) * SDL_HAT_DOWN |
                  ((h >> STDL_JOYSTICK_LEFT) & 1) * SDL_HAT_LEFT);
    }
}

/* the next pair says joystick i came (or went) */
static int pair(int i, int added)
{
    SDL_Event a = next(), b = next();
    return a.type == (Uint32)(added ? SDL_JOYDEVICEADDED : SDL_JOYDEVICEREMOVED)
        && a.jdevice.which == i
        && b.type == (Uint32)(added ? SDL_CONTROLLERDEVICEADDED
                                    : SDL_CONTROLLERDEVICEREMOVED)
        && b.cdevice.which == i;
}

static void joystick(void)
{
    SDL_Event e;
    SDL_Joystick *j;
    SDL_GameController *c;

    CHECK(SDL_NumJoysticks() == 1);
    CHECK(SDL_IsGameController(0) && !SDL_IsGameController(1));
    CHECK(SDL_JoystickOpen(1) == NULL);

    /* state is kept while nothing is open, and no events are sent */
    held(0, STDL_JOYSTICK_FIRE + 3, 1);
    joy_axis(1, 32767);
    joy_event(0, STDL_JOYHATMOTION, 0, SDL_HAT_DOWN);
    CHECK(next().type == 0);
    j = SDL_JoystickOpen(0);
    CHECK(j != NULL);
    CHECK(SDL_JoystickGetButton(j, 3) == 1);
    CHECK(SDL_JoystickGetAxis(j, 1) == 32767);
    /* out of range either way, nothing: button -1 is not the
     * direction before fire */
    fake_helds[0] |= 1u << (STDL_JOYSTICK_FIRE - 1);
    CHECK(SDL_JoystickGetButton(j, -1) == 0 && SDL_JoystickGetButton(j, 13) == 0);
    fake_helds[0] &= ~(1u << (STDL_JOYSTICK_FIRE - 1));
    CHECK(SDL_JoystickGetAxis(j, -1) == 0 && SDL_JoystickGetAxis(j, 6) == 0);
    CHECK(SDL_JoystickGetHat(j, 0) == SDL_HAT_DOWN);
    CHECK(SDL_JoystickNumAxes(j) == 2 && SDL_JoystickNumButtons(j) == 1 &&
          SDL_JoystickNumHats(j) == 0);
    fake_pads[0] = 1;
    CHECK(SDL_JoystickNumAxes(j) == 6 && SDL_JoystickNumButtons(j) == 13 &&
          SDL_JoystickNumHats(j) == 1);
    CHECK(SDL_JoystickInstanceID(j) == 0);

    joy_axis(0, -32768);
    e = next();
    CHECK(e.type == SDL_JOYAXISMOTION && e.jaxis.which == 0 &&
          e.jaxis.axis == 0 && e.jaxis.value == -32768);
    CHECK(SDL_JoystickGetAxis(j, 0) == -32768);
    joy_event(0, STDL_JOYHATMOTION, 0, SDL_HAT_LEFTUP);
    e = next();
    CHECK(e.type == SDL_JOYHATMOTION && e.jhat.value == SDL_HAT_LEFTUP);
    CHECK(SDL_JoystickGetHat(j, 0) == SDL_HAT_LEFTUP);
    held(0, STDL_JOYSTICK_FIRE + 3, 0);
    e = next();
    CHECK(e.type == SDL_JOYBUTTONUP && e.jbutton.button == 3 &&
          e.jbutton.state == SDL_RELEASED);
    CHECK(SDL_JoystickGetButton(j, 3) == 0);
    joy_event(0, STDL_JOYBUTTONDOWN, 13, 1);    /* past the last */
    CHECK(next().type == 0);

    /* ignored: not sent, and the button still STDL's */
    CHECK(SDL_JoystickEventState(SDL_IGNORE) == SDL_IGNORE);
    held(0, STDL_JOYSTICK_FIRE + 2, 1);
    CHECK(next().type == 0);
    CHECK(SDL_JoystickGetButton(j, 2) == 1);
    CHECK(SDL_JoystickEventState(SDL_QUERY) == SDL_IGNORE);
    SDL_JoystickEventState(SDL_ENABLE);

    /* the controller, its joystick closed so that only its own events
     * come: what is held at its opening is no news */
    SDL_JoystickClose(j);
    fake_helds[0] = 1u << STDL_JOYSTICK_SOUTH;
    c = SDL_GameControllerOpen(0);
    CHECK(c != NULL);
    CHECK(SDL_GameControllerGetJoystick(c) == j);
    CHECK(next().type == 0);
    CHECK(SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_A) == 1);
    {
        /* -1 is nothing, whatever is held */
        const unsigned was = fake_helds[0];

        fake_helds[0] = ~0u;
        CHECK(SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_INVALID) == 0 &&
              SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_INVALID) == 0);
        fake_helds[0] = was;
    }
    held(0, STDL_JOYSTICK_SOUTH, 0);
    held(0, STDL_JOYSTICK_EAST, 1);
    held(0, STDL_JOYSTICK_UP, 1);
    e = next();
    CHECK(e.type == SDL_CONTROLLERBUTTONUP &&
          e.cbutton.button == SDL_CONTROLLER_BUTTON_A &&
          e.cbutton.state == SDL_RELEASED);
    e = next();
    CHECK(e.type == SDL_CONTROLLERBUTTONDOWN &&
          e.cbutton.button == SDL_CONTROLLER_BUTTON_B);
    e = next();
    CHECK(e.type == SDL_CONTROLLERBUTTONDOWN &&
          e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_UP);
    CHECK(next().type == 0);                    /* no change, no event */
    CHECK(SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_DPAD_UP));
    held(0, STDL_JOYSTICK_EAST, 0);
    held(0, STDL_JOYSTICK_UP, 0);
    held(0, STDL_JOYSTICK_START, 1);
    held(0, STDL_JOYSTICK_THUMBR, 1);
    held(0, STDL_JOYSTICK_TR, 1);
    held(0, STDL_JOYSTICK_MODE, 1);
    {
        int seen = 0, n = 0;
        /* each change once: four down, two up */
        while ((e = next()).type && n++ < 64) {
            if (e.type == SDL_CONTROLLERBUTTONDOWN) {
                seen |= 1 << e.cbutton.button;
            }
        }
        CHECK(n == 6);
        CHECK(seen == (1 << SDL_CONTROLLER_BUTTON_START |
                       1 << SDL_CONTROLLER_BUTTON_RIGHTSTICK |
                       1 << SDL_CONTROLLER_BUTTON_RIGHTSHOULDER |
                       1 << SDL_CONTROLLER_BUTTON_GUIDE));
    }
    joy_axis(SDL_CONTROLLER_AXIS_TRIGGERLEFT, 1234);
    e = next();
    CHECK(e.type == SDL_CONTROLLERAXISMOTION &&
          e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT &&
          e.caxis.value == 1234);
    CHECK(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT)
          == 1234);

    /* rumble: SDL2's 16 bits to STDL's 8, the time held to 16 bits */
    fake_pads[0] = 0;
    CHECK(SDL_JoystickRumble(j, 0x1234, 0xABCD, 200) < 0);
    fake_pads[0] = 1;
    CHECK(SDL_GameControllerRumble(c, 0x1234, 0xABCD, 100000) == 0);
    CHECK(fake_rumble_low == 0x12 && fake_rumble_high == 0xAB &&
          fake_rumble_ms == 0xFFFF && fake_rumbles == 1);

    SDL_GameControllerClose(c);
    held(0, STDL_JOYSTICK_START, 0);
    CHECK(next().type == 0);                    /* closed: no events */
    joy_event(0, STDL_JOYBUTTONDOWN, 1, 1);
    CHECK(next().type == 0);
    fake_helds[0] = 0;
    fake_pads[0] = 0;
}

/* More than one: the program asked STDL for them (STDL_SetNumJoysticks) */
static void joysticks(void)
{
    SDL_Event e;
    SDL_Joystick *j0, *j2;
    SDL_GameController *c1, *c2;
    int i;

    /* STDL says each new one: its joystick, then its controller */
    fake_set_joys(3);
    fake_pads[2] = 1;
    for (i = 1; i < 3; i++) {
        CHECK(pair(i, 1));
    }
    CHECK(next().type == 0);
    CHECK(SDL_NumJoysticks() == 3);
    CHECK(SDL_IsGameController(2) && !SDL_IsGameController(3));
    CHECK(SDL_JoystickOpen(3) == NULL);
    CHECK(strcmp(SDL_JoystickNameForIndex(1), "Atari joystick") == 0);
    CHECK(strcmp(SDL_JoystickNameForIndex(2), "Xpad pad") == 0);

    j0 = SDL_JoystickOpen(0);
    j2 = SDL_JoystickOpen(2);
    CHECK(j2 != NULL && j2 != j0);
    CHECK(SDL_JoystickInstanceID(j2) == 2 && SDL_JoystickInstanceID(j0) == 0);
    CHECK(SDL_JoystickNumAxes(j2) == 6 && SDL_JoystickNumButtons(j2) == 13 &&
          SDL_JoystickNumHats(j2) == 1);

    /* each one's state its own, and its events its `which` */
    joy_event(2, STDL_JOYAXISMOTION, 4, 777);
    joy_event(0, STDL_JOYAXISMOTION, 4, -5);
    held(1, STDL_JOYSTICK_EAST, 1);             /* 1 is not open */
    joy_event(2, STDL_JOYHATMOTION, 0, SDL_HAT_RIGHT);
    held(2, STDL_JOYSTICK_FIRE + 7, 1);
    e = next();
    CHECK(e.type == SDL_JOYAXISMOTION && e.jaxis.which == 2 &&
          e.jaxis.axis == 4 && e.jaxis.value == 777);
    e = next();
    CHECK(e.type == SDL_JOYAXISMOTION && e.jaxis.which == 0 &&
          e.jaxis.value == -5);
    e = next();
    CHECK(e.type == SDL_JOYHATMOTION && e.jhat.which == 2 &&
          e.jhat.value == SDL_HAT_RIGHT);
    e = next();
    CHECK(e.type == SDL_JOYBUTTONDOWN && e.jbutton.which == 2 &&
          e.jbutton.button == 7);
    CHECK(next().type == 0);
    CHECK(SDL_JoystickGetAxis(j2, 4) == 777 && SDL_JoystickGetAxis(j0, 4) == -5);
    CHECK(SDL_JoystickGetHat(j2, 0) == SDL_HAT_RIGHT &&
          SDL_JoystickGetHat(j0, 0) != SDL_HAT_RIGHT);
    CHECK(SDL_JoystickGetButton(j2, 7) == 1 && SDL_JoystickGetButton(j0, 7) == 0);
    /* a closed joystick's buttons are STDL's too */
    c1 = SDL_GameControllerOpen(1);
    CHECK(SDL_JoystickGetButton(SDL_GameControllerGetJoystick(c1), 1) == 1);

    /* the controllers: each one's buttons from its own held inputs */
    fake_helds[2] = 1u << STDL_JOYSTICK_SOUTH;
    c2 = SDL_GameControllerOpen(2);
    CHECK(c2 != NULL && c2 != c1);
    CHECK(SDL_GameControllerGetJoystick(c2) == j2);
    CHECK(SDL_GameControllerGetAttached(c2));
    CHECK(next().type == 0);
    held(1, STDL_JOYSTICK_LEFT, 1);
    e = next();
    CHECK(e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.which == 1 &&
          e.cbutton.button == SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    CHECK(next().type == 0);
    CHECK(SDL_GameControllerGetButton(c2, SDL_CONTROLLER_BUTTON_A) == 1);
    CHECK(SDL_GameControllerGetButton(c1, SDL_CONTROLLER_BUTTON_A) == 0);
    joy_event(2, STDL_JOYAXISMOTION, SDL_CONTROLLER_AXIS_RIGHTY, 99);
    e = next();
    CHECK(e.type == SDL_JOYAXISMOTION && e.jaxis.which == 2);
    e = next();
    CHECK(e.type == SDL_CONTROLLERAXISMOTION && e.caxis.which == 2 &&
          e.caxis.axis == SDL_CONTROLLER_AXIS_RIGHTY && e.caxis.value == 99);
    CHECK(SDL_GameControllerGetAxis(c2, SDL_CONTROLLER_AXIS_RIGHTY) == 99);

    /* rumble, each pad its own */
    CHECK(SDL_GameControllerRumble(c2, 0x4000, 0x8000, 50) == 0);
    CHECK(fake_rumble_which == 2 && fake_rumble_low == 0x40 &&
          fake_rumble_high == 0x80 && fake_rumble_ms == 50);
    CHECK(SDL_GameControllerRumble(c1, 0x4000, 0x8000, 50) < 0);

    /* fewer: STDL's releases, then each one gone, each controller's
     * releases before its REMOVED (the joysticks closed, so that only
     * the controllers say anything of the releases) */
    SDL_JoystickClose(j2);
    SDL_JoystickClose(j0);
    held(1, STDL_JOYSTICK_LEFT, 0);
    held(1, STDL_JOYSTICK_EAST, 0);
    held(2, STDL_JOYSTICK_SOUTH, 0);
    fake_set_joys(1);
    {
        int ups = 0, n = 0;
        while ((e = next()).type == SDL_CONTROLLERBUTTONUP && n++ < 8) {
            CHECK(e.cbutton.which == 1);
            ups++;
        }
        CHECK(ups == 2);
        CHECK(e.type == SDL_JOYDEVICEREMOVED && e.jdevice.which == 1);
        e = next();
        CHECK(e.type == SDL_CONTROLLERDEVICEREMOVED && e.cdevice.which == 1);
        e = next();
        CHECK(e.type == SDL_CONTROLLERBUTTONUP && e.cbutton.which == 2 &&
              e.cbutton.button == SDL_CONTROLLER_BUTTON_A);
        CHECK(pair(2, 0));
        CHECK(next().type == 0);
    }
    CHECK(SDL_NumJoysticks() == 1 && SDL_JoystickOpen(2) == NULL);
    CHECK(!SDL_GameControllerGetAttached(c2));
    SDL_GameControllerClose(c1);
    SDL_GameControllerClose(c2);
    fake_helds[2] = 0;
    fake_pads[2] = 0;
}

/* Six, apart (STDL_UseJoystickMerge(0)): when the pads are merged or
 * parted every joystick stands for other sources, so each goes and
 * comes back. */
static void six(void)
{
    SDL_Joystick *j;
    SDL_GameController *c;
    SDL_Event e;
    int i;

    fake_set_joys(6);
    for (i = 1; i < 6; i++) {
        CHECK(pair(i, 1));
    }
    CHECK(next().type == 0);
    CHECK(SDL_NumJoysticks() == 6 && SDL_IsGameController(5));
    j = SDL_JoystickOpen(5);
    CHECK(j != NULL && SDL_JoystickOpen(6) == NULL);
    held(5, STDL_JOYSTICK_FIRE + 3, 1);
    e = next();
    CHECK(e.type == SDL_JOYBUTTONDOWN && e.jbutton.which == 5);
    CHECK(SDL_JoystickGetButton(j, 3) == 1);
    SDL_JoystickClose(j);
    fake_helds[5] = 0;
    /* a controller opened on its own, its joystick never handed out,
     * rumbles its own pad, not joystick 0's */
    fake_pads[4] = 1;
    c = SDL_GameControllerOpen(4);
    fake_rumble_which = -1;
    CHECK(SDL_GameControllerRumble(c, 0x4000, 0x8000, 50) == 0 &&
          fake_rumble_which == 4);
    /* a pad taken over at a re-map starts at rest in STDL and says
     * nothing of what it holds: its controller reads it all the same */
    fake_helds[4] = 1u << STDL_JOYSTICK_SOUTH;
    fake_set_merge(6);
    for (i = 0; i < 6; i++) {
        CHECK(pair(i, 0) && pair(i, 1));
    }
    e = next();
    CHECK(e.type == SDL_CONTROLLERBUTTONDOWN && e.cbutton.which == 4 &&
          e.cbutton.button == SDL_CONTROLLER_BUTTON_A);
    CHECK(next().type == 0);
    SDL_GameControllerClose(c);
    fake_helds[4] = 0;
    fake_pads[4] = 0;
    /* merged again (four at most) and one joystick, in one pump: the
     * merge's pairs, two gone, then three more */
    fake_set_merge(4);
    fake_set_joys(1);
    for (i = 0; i < 4; i++) {
        CHECK(pair(i, 0) && pair(i, 1));
    }
    CHECK(pair(4, 0) && pair(5, 0));
    for (i = 1; i < 4; i++) {
        CHECK(pair(i, 0));
    }
    CHECK(next().type == 0);
    /* and a change undone before a pump is said, where comparing the
     * count at the pump saw nothing: 1 to 2 to 1, the merge twice */
    fake_set_joys(2);
    fake_set_joys(1);
    CHECK(pair(1, 1) && pair(1, 0));
    CHECK(next().type == 0);
    fake_set_merge(1);
    fake_set_merge(1);
    CHECK(pair(0, 0) && pair(0, 1) && pair(0, 0) && pair(0, 1));
    CHECK(next().type == 0);
}

static void queue(void)
{
    SDL_Event e;
    STDL_Event se;
    Uint32 user;
    int i, n;

    /* ignored types are not queued, and flushed when ignored */
    memset(&se, 0, sizeof(se));
    se.type = STDL_MOUSEMOTION;
    fake_event(&se);
    SDL_PumpEvents();
    CHECK(SDL_EventState(SDL_MOUSEMOTION, SDL_IGNORE) == SDL_ENABLE);
    CHECK(next().type == 0);
    fake_event(&se);
    CHECK(next().type == 0);
    CHECK(SDL_GetEventState(SDL_MOUSEMOTION) == SDL_IGNORE);
    SDL_EventState(SDL_MOUSEMOTION, SDL_ENABLE);
    fake_event(&se);
    CHECK(next().type == SDL_MOUSEMOTION);

    /* user events, pushed */
    user = SDL_RegisterEvents(2);
    CHECK(user == SDL_USEREVENT);
    CHECK(SDL_RegisterEvents(1) == SDL_USEREVENT + 2);
    CHECK(SDL_RegisterEvents(0) == (Uint32)-1);
    memset(&e, 0, sizeof(e));
    e.type = user + 1;
    e.user.code = 7;
    CHECK(SDL_PushEvent(&e) == 1);
    e = next();
    CHECK(e.type == user + 1 && e.user.code == 7);

    /* a full queue, and a flush that keeps what it does not name */
    for (i = 0, n = 0; i < 70; i++) {
        memset(&e, 0, sizeof(e));
        e.type = i % 3 ? user : SDL_KEYDOWN;
        e.key.keysym.sym = i;
        n += SDL_PushEvent(&e) == 1;
    }
    CHECK(n == 64);
    SDL_FlushEvents(SDL_USEREVENT, SDL_LASTEVENT);
    for (i = 0, n = 0; i < 64; i++) {
        if (i % 3 == 0) {
            e = next();
            n += e.type == SDL_KEYDOWN && e.key.keysym.sym == i;
        }
    }
    CHECK(n == 22);
    CHECK(next().type == 0);

    /* a peek leaves the event there */
    memset(&e, 0, sizeof(e));
    e.type = user;
    SDL_PushEvent(&e);
    CHECK(SDL_PollEvent(NULL) == 1);
    CHECK(next().type == user);

    /* quit, and a wait that times out */
    memset(&se, 0, sizeof(se));
    se.type = STDL_QUIT;
    fake_event(&se);
    e = next();
    CHECK(e.type == SDL_QUIT);
    {
        const Uint32 t0 = SDL_GetTicks();
        CHECK(SDL_WaitEventTimeout(&e, 10) == 0);
        CHECK(SDL_GetTicks() != t0);
    }
    fake_key(1, STDLK_x, 0, 0);
    CHECK(SDL_WaitEvent(&e) == 1 && e.type == SDL_KEYDOWN);
    drain();
}

/*
 * The pump's beat: SDL_PollEvent pumps, STDL's and the add-on's, once
 * a VBL (STDL_PumpEventsIfDue), and its drain of STDL's queue pumps
 * no more; SDL_PumpEvents and the waits pump whenever called.
 */
static void beat(void)
{
    SDL_Event e;
    int i, n, p;

    drain();
    for (i = 0; i < 8; i++) {
        fake_key(i & 1 ? 0 : 1, STDLK_UP, 0, 0);
    }
    fake_vbl++;
    p = fake_pumps;
    n = 0;
    while (SDL_PollEvent(&e)) {
        n++;
    }
    CHECK(n == 8 && fake_pumps == p + 1);
    /* a key in the same VBL waits for the next */
    fake_key(1, STDLK_UP, 0, 0);
    CHECK(SDL_PollEvent(&e) == 0 && fake_pumps == p + 1);
    fake_vbl++;
    CHECK(SDL_PollEvent(&e) == 1 && e.type == SDL_KEYDOWN &&
          fake_pumps == p + 2);
    /* asked for, always */
    SDL_PumpEvents();
    SDL_PumpEvents();
    CHECK(fake_pumps == p + 4);
    /* a wait pumps at its first look */
    fake_key(0, STDLK_UP, 0, 0);
    CHECK(SDL_WaitEventTimeout(&e, 0) == 1 && e.type == SDL_KEYUP &&
          fake_pumps == p + 5);
}

int main(void)
{
    fake_layout = FAKE_VGA;
    fake_mach.mch_cookie = 0x00030000UL;
    numbers();
    CHECK(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) == 0);
    CHECK(SDL_WasInit(SDL_INIT_JOYSTICK | SDL_INIT_EVENTS) ==
          (SDL_INIT_JOYSTICK | SDL_INIT_EVENTS));

    /* the first pump: STDL's repeat and text on, and its device events,
     * which say the joysticks there are then (none before: a merge
     * change is not said) */
    fake_set_merge(1);
    CHECK(fake_repeat_delay == -1 && fake_unicode == -1 && !fake_devices);
    CHECK(pair(0, 1));
    CHECK(fake_repeat_delay == 500 && fake_repeat_interval == 30 &&
          fake_unicode == 1);
    CHECK(next().type == 0);
    /* merged after: joystick 0 goes and comes back */
    fake_set_merge(1);
    CHECK(pair(0, 0) && pair(0, 1));
    CHECK(next().type == 0);

    keymap();
    keys();
    mouse();
    joystick();
    joysticks();
    six();
    queue();
    beat();
    SDL_Quit();
    if (failures) {
        printf("test_events: %d failures\n", failures);
        return 1;
    }
    printf("test_events: ok\n");
    return 0;
}
