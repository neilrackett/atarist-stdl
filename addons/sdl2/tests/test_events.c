/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The add-on's events on the host: STDL's events queued in
 * fake_stdl.c, pumped and read back as SDL2's. Keys with their
 * scancodes, key codes, modifiers, repeat flag and text; the mouse from
 * the logical size's top left; joystick 0's state and events, and the
 * controller's buttons from STDL's held inputs; the queue's ignore
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

/* the next event, or type 0 */
static SDL_Event next(void)
{
    SDL_Event e;
    if (!SDL_PollEvent(&e)) {
        memset(&e, 0, sizeof(e));
    }
    return e;
}

static void drain(void)
{
    SDL_Event e;
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

static void joy_axis(int axis, int value)
{
    STDL_Event se;
    memset(&se, 0, sizeof(se));
    se.type = STDL_JOYAXISMOTION;
    se.jaxis.axis = (Uint8)axis;
    se.jaxis.value = (Sint16)value;
    fake_event(&se);
}

static void joy_button(int b, int down)
{
    STDL_Event se;
    memset(&se, 0, sizeof(se));
    se.type = down ? STDL_JOYBUTTONDOWN : STDL_JOYBUTTONUP;
    se.jbutton.button = (Uint8)b;
    se.jbutton.state = (Uint8)down;
    fake_event(&se);
}

static void joystick(void)
{
    SDL_Event e;
    STDL_Event se;
    SDL_Joystick *j;
    SDL_GameController *c;

    CHECK(SDL_NumJoysticks() == 1);
    CHECK(SDL_IsGameController(0) && !SDL_IsGameController(1));
    CHECK(SDL_JoystickOpen(1) == NULL);

    /* state is kept while nothing is open, and no events are sent */
    joy_button(3, 1);
    joy_axis(1, 32767);
    memset(&se, 0, sizeof(se));
    se.type = STDL_JOYHATMOTION;
    se.jhat.value = SDL_HAT_DOWN;
    fake_event(&se);
    CHECK(next().type == 0);
    j = SDL_JoystickOpen(0);
    CHECK(j != NULL);
    CHECK(SDL_JoystickGetButton(j, 3) == 1);
    CHECK(SDL_JoystickGetAxis(j, 1) == 32767);
    CHECK(SDL_JoystickGetHat(j, 0) == SDL_HAT_DOWN);
    CHECK(SDL_JoystickNumAxes(j) == 2 && SDL_JoystickNumButtons(j) == 1 &&
          SDL_JoystickNumHats(j) == 0);
    fake_pad = 1;
    CHECK(SDL_JoystickNumAxes(j) == 6 && SDL_JoystickNumButtons(j) == 13 &&
          SDL_JoystickNumHats(j) == 1);
    CHECK(SDL_JoystickInstanceID(j) == 0);

    joy_axis(0, -32768);
    e = next();
    CHECK(e.type == SDL_JOYAXISMOTION && e.jaxis.which == 0 &&
          e.jaxis.axis == 0 && e.jaxis.value == -32768);
    CHECK(SDL_JoystickGetAxis(j, 0) == -32768);
    memset(&se, 0, sizeof(se));
    se.type = STDL_JOYHATMOTION;
    se.jhat.value = SDL_HAT_LEFTUP;
    fake_event(&se);
    e = next();
    CHECK(e.type == SDL_JOYHATMOTION && e.jhat.value == SDL_HAT_LEFTUP);
    CHECK(SDL_JoystickGetHat(j, 0) == SDL_HAT_LEFTUP);
    joy_button(3, 0);
    e = next();
    CHECK(e.type == SDL_JOYBUTTONUP && e.jbutton.button == 3 &&
          e.jbutton.state == SDL_RELEASED);
    CHECK(SDL_JoystickGetButton(j, 3) == 0);
    joy_button(13, 1);                          /* past the last */
    CHECK(next().type == 0);

    /* ignored: kept, not sent */
    CHECK(SDL_JoystickEventState(SDL_IGNORE) == SDL_IGNORE);
    joy_button(2, 1);
    CHECK(next().type == 0);
    CHECK(SDL_JoystickGetButton(j, 2) == 1);
    CHECK(SDL_JoystickEventState(SDL_QUERY) == SDL_IGNORE);
    SDL_JoystickEventState(SDL_ENABLE);

    /* the controller: what is held at its opening is no news */
    fake_held = 1u << STDL_JOYKEY_SOUTH;
    c = SDL_GameControllerOpen(0);
    CHECK(c != NULL);
    CHECK(SDL_GameControllerGetJoystick(c) == j);
    CHECK(next().type == 0);
    CHECK(SDL_GameControllerGetButton(c, SDL_CONTROLLER_BUTTON_A) == 1);
    fake_held = 1u << STDL_JOYKEY_EAST | 1u << STDL_JOYKEY_UP;
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
    fake_held = 1u << STDL_JOYKEY_START | 1u << STDL_JOYKEY_THUMBR |
                1u << STDL_JOYKEY_TR | 1u << STDL_JOYKEY_MODE;
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
    CHECK(e.type == SDL_JOYAXISMOTION);
    e = next();
    CHECK(e.type == SDL_CONTROLLERAXISMOTION &&
          e.caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERLEFT &&
          e.caxis.value == 1234);
    CHECK(SDL_GameControllerGetAxis(c, SDL_CONTROLLER_AXIS_TRIGGERLEFT)
          == 1234);

    /* rumble: SDL2's 16 bits to STDL's 8, the time held to 16 bits */
    fake_pad = 0;
    CHECK(SDL_JoystickRumble(j, 0x1234, 0xABCD, 200) < 0);
    fake_pad = 1;
    CHECK(SDL_GameControllerRumble(c, 0x1234, 0xABCD, 100000) == 0);
    CHECK(fake_rumble_low == 0x12 && fake_rumble_high == 0xAB &&
          fake_rumble_ms == 0xFFFF && fake_rumbles == 1);

    SDL_GameControllerClose(c);
    fake_held = 0;
    CHECK(next().type == 0);                    /* closed: no events */
    SDL_JoystickClose(j);
    joy_button(1, 1);
    CHECK(next().type == 0);
    fake_pad = 0;
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

int main(void)
{
    SDL_Event e;
    fake_layout = FAKE_VGA;
    fake_mach.mch_cookie = 0x00030000UL;
    numbers();
    CHECK(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) == 0);
    CHECK(SDL_WasInit(SDL_INIT_JOYSTICK | SDL_INIT_EVENTS) ==
          (SDL_INIT_JOYSTICK | SDL_INIT_EVENTS));

    /* the first pump: STDL's repeat and text on, the devices said */
    CHECK(fake_repeat_delay == -1 && fake_unicode == -1);
    e = next();
    CHECK(fake_repeat_delay == 500 && fake_repeat_interval == 30 &&
          fake_unicode == 1);
    CHECK(e.type == SDL_JOYDEVICEADDED && e.jdevice.which == 0);
    e = next();
    CHECK(e.type == SDL_CONTROLLERDEVICEADDED && e.cdevice.which == 0);
    CHECK(next().type == 0);

    keymap();
    keys();
    mouse();
    joystick();
    queue();
    SDL_Quit();
    if (failures) {
        printf("test_events: %d failures\n", failures);
        return 1;
    }
    printf("test_events: ok\n");
    return 0;
}
