/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Events: STDL's, translated into SDL2's as they are pumped, into a
 * queue of the add-on's own (64, as STDL's is 32 and a pump can add a
 * text event to each key). The keyboard's state is kept by scancode
 * from the key events, as SDL2 keeps it.
 *
 * The first pump turns STDL's key repeat on (SDL2's keys repeat, with
 * a flag saying so) and its character translation (for SDL_TEXTINPUT),
 * and says the joystick is there (SDL_JOYDEVICEADDED, and
 * SDL_CONTROLLERDEVICEADDED with SDL_INIT_GAMECONTROLLER), as SDL2 does
 * for a device present at start-up: a program waiting for that event
 * to open its controller would otherwise never open it. Not SDL_Init:
 * a program that reads STDL's events itself and never pumps here keeps
 * STDL as it set it.
 */

#include "internal.h"

#define QUEUE 64                /* a power of two */
#define IGNORED 16

static SDL_Event queue[QUEUE];
static int qhead, qcount;
static Uint8 keystate[SDL_NUM_SCANCODES];
static int started;
static int textinput = 1;
static Uint32 ignored[IGNORED];
static int nignored;
static Uint32 next_user = SDL_USEREVENT;

static int is_ignored(Uint32 type)
{
    int i;
    for (i = 0; i < nignored; i++) {
        if (ignored[i] == type) {
            return 1;
        }
    }
    return 0;
}

int stdl_sdl2_push(SDL_Event *e)
{
    if (is_ignored(e->type)) {
        return 0;
    }
    if (qcount == QUEUE) {
        return -1;
    }
    e->common.timestamp = SDL_GetTicks();
    queue[(qhead + qcount) & (QUEUE - 1)] = *e;
    qcount++;
    return 1;
}

static void key_event(const STDL_Event *se)
{
    SDL_Event e;
    const int down = se->type == STDL_KEYDOWN;
    const SDL_Scancode sc = stdl_sdl2_scancode(se->key.keysym.sym);
    memset(&e, 0, sizeof(e));
    e.type = down ? SDL_KEYDOWN : SDL_KEYUP;
    e.key.windowID = 1;
    e.key.state = down ? SDL_PRESSED : SDL_RELEASED;
    e.key.repeat = (Uint8)(down && sc && keystate[sc]);
    e.key.keysym.scancode = sc;
    e.key.keysym.sym = stdl_sdl2_keycode(se->key.keysym.sym);
    /* SDL2's modifier bits are STDL's, but for STDL's joystick tag */
    e.key.keysym.mod = (Uint16)(se->key.keysym.mod & 0x7FFF);
    if (sc) {
        keystate[sc] = (Uint8)down;
    }
    stdl_sdl2_push(&e);
    if (down && textinput && se->key.keysym.unicode >= 0x20 &&
        se->key.keysym.unicode < 0x7F) {
        memset(&e, 0, sizeof(e));
        e.type = SDL_TEXTINPUT;
        e.text.windowID = 1;
        e.text.text[0] = (char)se->key.keysym.unicode;
        stdl_sdl2_push(&e);
    }
}

static void mouse_event(const STDL_Event *se)
{
    SDL_Event e;
    int ox, oy;
    stdl_sdl2_screen_origin(&ox, &oy);
    memset(&e, 0, sizeof(e));
    if (se->type == STDL_MOUSEMOTION) {
        e.type = SDL_MOUSEMOTION;
        e.motion.windowID = 1;
        e.motion.state = se->motion.state;
        e.motion.x = se->motion.x - ox;
        e.motion.y = se->motion.y - oy;
        e.motion.xrel = se->motion.xrel;
        e.motion.yrel = se->motion.yrel;
    } else {
        e.type = se->type == STDL_MOUSEBUTTONDOWN ? SDL_MOUSEBUTTONDOWN
                                                  : SDL_MOUSEBUTTONUP;
        e.button.windowID = 1;
        e.button.button = se->button.button;
        e.button.state = se->type == STDL_MOUSEBUTTONDOWN ? SDL_PRESSED
                                                          : SDL_RELEASED;
        e.button.clicks = 1;
        e.button.x = se->button.x - ox;
        e.button.y = se->button.y - oy;
    }
    stdl_sdl2_push(&e);
}

static void first_pump(void)
{
    SDL_Event e;
    started = 1;
    STDL_EnableKeyRepeat(STDL_DEFAULT_REPEAT_DELAY,
                         STDL_DEFAULT_REPEAT_INTERVAL);
    STDL_EnableUNICODE(1);
    if (stdl_sdl2_inited & SDL_INIT_JOYSTICK) {
        memset(&e, 0, sizeof(e));
        e.type = SDL_JOYDEVICEADDED;
        stdl_sdl2_push(&e);
    }
    if (stdl_sdl2_inited & SDL_INIT_GAMECONTROLLER) {
        memset(&e, 0, sizeof(e));
        e.type = SDL_CONTROLLERDEVICEADDED;
        stdl_sdl2_push(&e);
    }
}

void SDL_PumpEvents(void)
{
    STDL_Event se;
    if (!started) {
        first_pump();
    }
    while (STDL_PollEvent(&se)) {
        switch (se.type) {
        case STDL_KEYDOWN:
        case STDL_KEYUP:
            key_event(&se);
            break;
        case STDL_MOUSEMOTION:
        case STDL_MOUSEBUTTONDOWN:
        case STDL_MOUSEBUTTONUP:
            mouse_event(&se);
            break;
        case STDL_JOYAXISMOTION:
        case STDL_JOYHATMOTION:
        case STDL_JOYBUTTONDOWN:
        case STDL_JOYBUTTONUP:
            stdl_sdl2_joy_event(&se);
            break;
        case STDL_QUIT: {
            SDL_Event e;
            memset(&e, 0, sizeof(e));
            e.type = SDL_QUIT;
            stdl_sdl2_push(&e);
            break;
        }
        default:
            break;
        }
    }
    stdl_sdl2_controller_poll();
}

int SDL_PollEvent(SDL_Event *event)
{
    SDL_PumpEvents();
    if (!qcount) {
        return 0;
    }
    if (event) {
        *event = queue[qhead];
        qhead = (qhead + 1) & (QUEUE - 1);
        qcount--;
    }
    return 1;
}

int SDL_WaitEventTimeout(SDL_Event *event, int timeout)
{
    const Uint32 end = SDL_GetTicks() + (Uint32)timeout;
    for (;;) {
        if (SDL_PollEvent(event)) {
            return 1;
        }
        if (timeout >= 0 && SDL_TICKS_PASSED(SDL_GetTicks(), end)) {
            return 0;
        }
        /* a tick of the 200Hz clock, STDL's audio and timers served */
        STDL_Delay(5);
    }
}

int SDL_WaitEvent(SDL_Event *event)
{
    return SDL_WaitEventTimeout(event, -1);
}

int SDL_PushEvent(SDL_Event *event)
{
    if (!event) {
        return SDL_InvalidParamError("event");
    }
    return stdl_sdl2_push(event);
}

void SDL_FlushEvents(Uint32 minType, Uint32 maxType)
{
    SDL_Event keep[QUEUE];
    int i, n = 0;
    for (i = 0; i < qcount; i++) {
        const SDL_Event *e = &queue[(qhead + i) & (QUEUE - 1)];
        if (e->type < minType || e->type > maxType) {
            keep[n++] = *e;
        }
    }
    for (i = 0; i < n; i++) {
        queue[i] = keep[i];
    }
    qhead = 0;
    qcount = n;
}

void SDL_FlushEvent(Uint32 type)
{
    SDL_FlushEvents(type, type);
}

Uint8 SDL_EventState(Uint32 type, int state)
{
    int i;
    const Uint8 was = is_ignored(type) ? SDL_IGNORE : SDL_ENABLE;
    if (state == SDL_IGNORE && was == SDL_ENABLE && nignored < IGNORED) {
        ignored[nignored++] = type;
        SDL_FlushEvent(type);
    } else if (state == SDL_ENABLE && was == SDL_IGNORE) {
        for (i = 0; i < nignored; i++) {
            if (ignored[i] == type) {
                ignored[i] = ignored[--nignored];
                break;
            }
        }
    }
    return was;
}

Uint32 SDL_RegisterEvents(int numevents)
{
    Uint32 first = next_user;
    if (numevents <= 0 || next_user + (Uint32)numevents > SDL_LASTEVENT + 1) {
        return (Uint32)-1;
    }
    next_user += (Uint32)numevents;
    return first;
}

/* ------------------------------------------------------------------ */
/* the keyboard and the mouse                                          */

const Uint8 *SDL_GetKeyboardState(int *numkeys)
{
    if (numkeys) {
        *numkeys = SDL_NUM_SCANCODES;
    }
    return keystate;
}

SDL_Keymod SDL_GetModState(void)
{
    return (SDL_Keymod)(STDL_GetModState() & 0x7FFF);
}

void SDL_StartTextInput(void)
{
    textinput = 1;
}

void SDL_StopTextInput(void)
{
    textinput = 0;
}

SDL_bool SDL_IsTextInputActive(void)
{
    return textinput ? SDL_TRUE : SDL_FALSE;
}

Uint32 SDL_GetMouseState(int *x, int *y)
{
    int sx, sy, ox, oy;
    const Uint8 buttons = STDL_GetMouseState(&sx, &sy);
    stdl_sdl2_screen_origin(&ox, &oy);
    if (x) {
        *x = sx - ox;
    }
    if (y) {
        *y = sy - oy;
    }
    return buttons;
}
