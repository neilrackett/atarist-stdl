/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Joystick 0 as SDL2's joystick and game controller. Its state - six
 * axes, thirteen buttons, a hat - is kept from STDL's joystick events
 * as they are pumped, whoever has it open, as SDL2 keeps a joystick's;
 * the events go on to an open joystick and, as axis events, to an
 * open controller. The controller's buttons are read from
 * STDL_JoyInputHeld once a pump and their changes sent, as the d-pad is
 * a pad's hat or a joystick's directions, which no one STDL event
 * carries.
 */

#include "internal.h"

#define AXES    6
#define BUTTONS 13

struct _SDL_Joystick {
    int open;
};

struct _SDL_GameController {
    int open;
    Uint8 held[SDL_CONTROLLER_BUTTON_MAX];
};

static SDL_Joystick joy;
static SDL_GameController pad;
static Sint16 axes[AXES];
static Uint8 buttons[BUTTONS];
static Uint8 hat;
static int joy_events = SDL_ENABLE;
static int pad_events = SDL_ENABLE;

/* SDL2's controller buttons as STDL's inputs */
static const Uint8 pad_input[SDL_CONTROLLER_BUTTON_MAX] = {
    STDL_JOYKEY_SOUTH, STDL_JOYKEY_EAST, STDL_JOYKEY_WEST,
    STDL_JOYKEY_NORTH, STDL_JOYKEY_SELECT, STDL_JOYKEY_MODE,
    STDL_JOYKEY_START, STDL_JOYKEY_THUMBL, STDL_JOYKEY_THUMBR,
    STDL_JOYKEY_TL, STDL_JOYKEY_TR, STDL_JOYKEY_UP, STDL_JOYKEY_DOWN,
    STDL_JOYKEY_LEFT, STDL_JOYKEY_RIGHT
};

static const char *joy_name(void)
{
    return STDL_HavePad() ? "Xpad pad" : "Atari joystick";
}

void stdl_sdl2_joy_event(const STDL_Event *se)
{
    SDL_Event e;
    memset(&e, 0, sizeof(e));
    switch (se->type) {
    case STDL_JOYAXISMOTION:
        if (se->jaxis.axis >= AXES) {
            return;
        }
        axes[se->jaxis.axis] = se->jaxis.value;
        if (joy.open && joy_events == SDL_ENABLE) {
            e.type = SDL_JOYAXISMOTION;
            e.jaxis.axis = se->jaxis.axis;
            e.jaxis.value = se->jaxis.value;
            stdl_sdl2_push(&e);
        }
        if (pad.open && pad_events == SDL_ENABLE) {
            memset(&e, 0, sizeof(e));
            e.type = SDL_CONTROLLERAXISMOTION;
            e.caxis.axis = se->jaxis.axis;
            e.caxis.value = se->jaxis.value;
            stdl_sdl2_push(&e);
        }
        break;
    case STDL_JOYHATMOTION:
        hat = se->jhat.value;
        if (joy.open && joy_events == SDL_ENABLE) {
            e.type = SDL_JOYHATMOTION;
            e.jhat.hat = 0;
            e.jhat.value = hat;
            stdl_sdl2_push(&e);
        }
        break;
    case STDL_JOYBUTTONDOWN:
    case STDL_JOYBUTTONUP:
        if (se->jbutton.button >= BUTTONS) {
            return;
        }
        buttons[se->jbutton.button] = se->type == STDL_JOYBUTTONDOWN;
        if (joy.open && joy_events == SDL_ENABLE) {
            e.type = se->type == STDL_JOYBUTTONDOWN ? SDL_JOYBUTTONDOWN
                                                    : SDL_JOYBUTTONUP;
            e.jbutton.button = se->jbutton.button;
            e.jbutton.state = buttons[se->jbutton.button];
            stdl_sdl2_push(&e);
        }
        break;
    default:
        break;
    }
}

void stdl_sdl2_controller_poll(void)
{
    int b;
    if (!pad.open || pad_events != SDL_ENABLE) {
        return;
    }
    for (b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) {
        const Uint8 now = STDL_JoyInputHeld(pad_input[b]) ? 1 : 0;
        if (now != pad.held[b]) {
            SDL_Event e;
            memset(&e, 0, sizeof(e));
            e.type = now ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
            e.cbutton.button = (Uint8)b;
            e.cbutton.state = now;
            pad.held[b] = now;
            stdl_sdl2_push(&e);
        }
    }
}

/* ------------------------------------------------------------------ */
/* the joystick                                                        */

int SDL_NumJoysticks(void)
{
    return 1;
}

const char *SDL_JoystickNameForIndex(int device_index)
{
    return device_index == 0 ? joy_name() : NULL;
}

SDL_Joystick *SDL_JoystickOpen(int device_index)
{
    if (device_index != 0) {
        SDL_SetError("There is joystick 0 alone");
        return NULL;
    }
    joy.open++;
    return &joy;
}

void SDL_JoystickClose(SDL_Joystick *joystick)
{
    if (joystick == &joy && joy.open > 0) {
        joy.open--;
    }
}

const char *SDL_JoystickName(SDL_Joystick *joystick)
{
    return joystick ? joy_name() : NULL;
}

SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick *joystick)
{
    return joystick ? 0 : -1;
}

int SDL_JoystickNumAxes(SDL_Joystick *joystick)
{
    return joystick ? (STDL_HavePad() ? AXES : 2) : -1;
}

int SDL_JoystickNumButtons(SDL_Joystick *joystick)
{
    return joystick ? (STDL_HavePad() ? BUTTONS : 1) : -1;
}

int SDL_JoystickNumHats(SDL_Joystick *joystick)
{
    return joystick ? (STDL_HavePad() ? 1 : 0) : -1;
}

Sint16 SDL_JoystickGetAxis(SDL_Joystick *joystick, int axis)
{
    return (joystick && axis >= 0 && axis < AXES) ? axes[axis] : 0;
}

Uint8 SDL_JoystickGetButton(SDL_Joystick *joystick, int button)
{
    return (joystick && button >= 0 && button < BUTTONS) ? buttons[button] : 0;
}

Uint8 SDL_JoystickGetHat(SDL_Joystick *joystick, int which)
{
    return (joystick && which == 0) ? hat : 0;
}

int SDL_JoystickRumble(SDL_Joystick *joystick, Uint16 low, Uint16 high,
                       Uint32 duration_ms)
{
    if (!joystick) {
        return SDL_InvalidParamError("joystick");
    }
    if (STDL_PadRumble((Uint8)(low >> 8), (Uint8)(high >> 8),
                       (Uint16)(duration_ms > 0xFFFF ? 0xFFFF
                                                     : duration_ms)) < 0) {
        return SDL_Unsupported();
    }
    return 0;
}

int SDL_JoystickEventState(int state)
{
    if (state == SDL_ENABLE || state == SDL_IGNORE) {
        joy_events = state;
    }
    return joy_events;
}

/* ------------------------------------------------------------------ */
/* the game controller                                                 */

SDL_bool SDL_IsGameController(int joystick_index)
{
    return joystick_index == 0 ? SDL_TRUE : SDL_FALSE;
}

SDL_GameController *SDL_GameControllerOpen(int joystick_index)
{
    int b;
    if (joystick_index != 0) {
        SDL_SetError("There is joystick 0 alone");
        return NULL;
    }
    /* what is held now is no news */
    if (!pad.open) {
        for (b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) {
            pad.held[b] = STDL_JoyInputHeld(pad_input[b]) ? 1 : 0;
        }
    }
    pad.open++;
    return &pad;
}

void SDL_GameControllerClose(SDL_GameController *gamecontroller)
{
    if (gamecontroller == &pad && pad.open > 0) {
        pad.open--;
    }
}

const char *SDL_GameControllerName(SDL_GameController *gamecontroller)
{
    return gamecontroller ? joy_name() : NULL;
}

const char *SDL_GameControllerNameForIndex(int joystick_index)
{
    return joystick_index == 0 ? joy_name() : NULL;
}

SDL_bool SDL_GameControllerGetAttached(SDL_GameController *gamecontroller)
{
    return gamecontroller ? SDL_TRUE : SDL_FALSE;
}

SDL_Joystick *SDL_GameControllerGetJoystick(SDL_GameController *gamecontroller)
{
    return gamecontroller ? &joy : NULL;
}

Uint8 SDL_GameControllerGetButton(SDL_GameController *gamecontroller,
                                  SDL_GameControllerButton button)
{
    if (!gamecontroller || button < 0 || button >= SDL_CONTROLLER_BUTTON_MAX) {
        return 0;
    }
    return STDL_JoyInputHeld(pad_input[button]) ? 1 : 0;
}

Sint16 SDL_GameControllerGetAxis(SDL_GameController *gamecontroller,
                                 SDL_GameControllerAxis axis)
{
    return (gamecontroller && axis >= 0 && axis < AXES) ? axes[axis] : 0;
}

int SDL_GameControllerRumble(SDL_GameController *gamecontroller, Uint16 low,
                             Uint16 high, Uint32 duration_ms)
{
    return SDL_JoystickRumble(gamecontroller ? &joy : NULL, low, high,
                              duration_ms);
}

int SDL_GameControllerEventState(int state)
{
    if (state == SDL_ENABLE || state == SDL_IGNORE) {
        pad_events = state;
    }
    return pad_events;
}
