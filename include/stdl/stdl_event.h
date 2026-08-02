/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * IKBD-driven input. The event union deliberately mirrors SDL 1.2's
 * shape (types, field names, numbering) to ease porting.
 */

#ifndef STDL_EVENT_H
#define STDL_EVENT_H

#include <stdl/stdl_types.h>
#include <stdl/stdl_keys.h>

enum {
    STDL_NOEVENT = 0,
    STDL_ACTIVEEVENT,
    STDL_KEYDOWN,
    STDL_KEYUP,
    STDL_MOUSEMOTION,
    STDL_MOUSEBUTTONDOWN,
    STDL_MOUSEBUTTONUP,
    STDL_JOYAXISMOTION,
    STDL_JOYBALLMOTION,
    STDL_JOYHATMOTION,
    STDL_JOYBUTTONDOWN,
    STDL_JOYBUTTONUP,
    STDL_QUIT,
    STDL_NUMEVENTS = 32
};

#define STDL_RELEASED 0
#define STDL_PRESSED  1

#define STDL_BUTTON_LEFT   1
#define STDL_BUTTON_MIDDLE 2
#define STDL_BUTTON_RIGHT  3

typedef struct {
    uint8_t  scancode;   /* raw IKBD scancode */
    uint16_t sym;        /* STDL_Key */
    uint16_t mod;        /* STDL_Mod bitmask */
    uint16_t unicode;    /* translated character (ASCII subset) */
} STDL_Keysym;

typedef struct {
    uint8_t     type;    /* STDL_KEYDOWN or STDL_KEYUP */
    uint8_t     state;
    STDL_Keysym keysym;
} STDL_KeyboardEvent;

typedef struct {
    uint8_t  type;
    uint8_t  state;      /* current button bitmask */
    uint16_t x, y;
    int16_t  xrel, yrel;
} STDL_MouseMotionEvent;

typedef struct {
    uint8_t  type;
    uint8_t  button;
    uint8_t  state;
    uint16_t x, y;
} STDL_MouseButtonEvent;

typedef struct {
    uint8_t type;
    uint8_t which;
    uint8_t axis;        /* 0 = x, 1 = y */
    int16_t value;       /* -32768 / 0 / 32767 */
} STDL_JoyAxisEvent;

typedef struct {
    uint8_t type;
    uint8_t which;
    uint8_t button;
    uint8_t state;
} STDL_JoyButtonEvent;

typedef struct {
    uint8_t type;
} STDL_QuitEvent;

/* hats and balls don't exist on ST joysticks; present so ported
 * event switches compile */
typedef struct {
    uint8_t type;
    uint8_t which;
    uint8_t hat;
    uint8_t value;
} STDL_JoyHatEvent;

typedef struct {
    uint8_t type;
    uint8_t which;
    uint8_t ball;
    int16_t xrel, yrel;
} STDL_JoyBallEvent;

/* never generated on the ST; present so ported switch statements
 * over SDL 1.2 event types still compile */
typedef struct {
    uint8_t type;
    int     w, h;
} STDL_ResizeEvent;

typedef union {
    uint8_t               type;
    STDL_KeyboardEvent    key;
    STDL_MouseMotionEvent motion;
    STDL_MouseButtonEvent button;
    STDL_JoyAxisEvent     jaxis;
    STDL_JoyButtonEvent   jbutton;
    STDL_JoyHatEvent      jhat;
    STDL_JoyBallEvent     jball;
    STDL_QuitEvent        quit;
    STDL_ResizeEvent      resize;
} STDL_Event;

int  STDL_PollEvent(STDL_Event *e);
int  STDL_WaitEvent(STDL_Event *e);
int  STDL_PushEvent(const STDL_Event *e);
void STDL_PumpEvents(void);

/* Key state array indexed by STDL_Key, valid after each pump. */
const uint8_t *STDL_GetKeyState(int *numkeys);
uint16_t       STDL_GetModState(void);
const char    *STDL_GetKeyName(uint16_t sym);

int  STDL_EnableKeyRepeat(int delay, int interval);
int  STDL_EnableUNICODE(int enable);
#define STDL_DEFAULT_REPEAT_DELAY    500
#define STDL_DEFAULT_REPEAT_INTERVAL 30

uint8_t STDL_GetMouseState(int *x, int *y);
void    STDL_WarpMouse(uint16_t x, uint16_t y);

/* Joystick 1 (the physical joystick port), reported as events too.
 * State byte: bit 0 up, 1 down, 2 left, 3 right, 7 fire. */
uint8_t STDL_GetJoyState(void);

/* When enabled, joystick changes also synthesise keyboard events
 * (tagged STDL_KMOD_JOYSTICK, with STDL_GetKeyState updated) so
 * keyboard-driven games work with the stick unmodified. Joystick
 * events are still delivered. The default mapping is the arrows
 * with left Alt as fire. */
void STDL_JoyKeyEmulation(int enable);

/*
 * Change which keys the joystick emulates. Each argument is an
 * STDL_Key (e.g. STDLK_SPACE); 0 leaves that input unmapped. Only
 * keys that exist on the ST keyboard can be emulated - the return
 * value is the number of arguments successfully resolved to a key,
 * so a game can verify its bindings. Safe to call while enabled
 * (held directions are released on the old keys first).
 */
int STDL_JoyKeyMapping(uint16_t up, uint16_t down, uint16_t left,
                       uint16_t right, uint16_t fire);

#endif /* STDL_EVENT_H */
