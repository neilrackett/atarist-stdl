/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL's joysticks as SDL2's joysticks and game controllers, by the
 * same numbers: one, unless the program asked STDL for more
 * (STDL_SetNumJoysticks), and their instance IDs their indices, since
 * a joystick keeps its number for as long as it is there. Each one's
 * axes, hat and buttons are STDL's own (STDL_GetJoystickAxis,
 * STDL_GetJoystickHat, STDL_JoystickInputHeld), whoever has it open,
 * as SDL2 keeps a joystick's; STDL's joystick events go on to an open
 * joystick and, as axis events, to an open controller, and its device
 * events become SDL2's for both. The controller's buttons are read
 * from STDL_JoystickInputHeld at a pump that brought an event for its
 * joystick - a device event too, as a joystick re-mapped stands for
 * other sources - and their changes sent, as
 * the d-pad is a pad's hat or a joystick's directions, which no one
 * STDL event carries.
 */

#include "internal.h"

#define JOYS    STDL_MAX_JOYSTICKS
#define AXES    6
#define BUTTONS 13

struct _SDL_Joystick {
    int open;
    int index;
};

struct _SDL_GameController {
    int open;
    int index;
    Uint8 held[SDL_CONTROLLER_BUTTON_MAX];
};

/* BSS: each one's index is set when it is first asked after (there),
 * which every handout is */
static SDL_Joystick joy[JOYS];
static SDL_GameController pad[JOYS];
/* the joysticks an STDL event came for this pump (bit i), whose
 * controllers' buttons may have moved, a device event's included. A
 * controller's opening reads every button, so needs no poll of its
 * own. */
static unsigned touched;
static int joy_events = SDL_ENABLE;
static int pad_events = SDL_ENABLE;

/* SDL2's controller buttons as STDL's inputs */
static const Uint8 pad_input[SDL_CONTROLLER_BUTTON_MAX] = {
    STDL_JOYSTICK_SOUTH, STDL_JOYSTICK_EAST, STDL_JOYSTICK_WEST,
    STDL_JOYSTICK_NORTH, STDL_JOYSTICK_SELECT, STDL_JOYSTICK_MODE,
    STDL_JOYSTICK_START, STDL_JOYSTICK_THUMBL, STDL_JOYSTICK_THUMBR,
    STDL_JOYSTICK_TL, STDL_JOYSTICK_TR, STDL_JOYSTICK_UP, STDL_JOYSTICK_DOWN,
    STDL_JOYSTICK_LEFT, STDL_JOYSTICK_RIGHT
};

/* a handle of ours, to be written through */
static int ours_joy(const SDL_Joystick *j)
{
    return j >= joy && j < joy + JOYS;
}

static int ours_pad(const SDL_GameController *c)
{
    return c >= pad && c < pad + JOYS;
}

/* Out of line, both: inlined at each of their callers they cost the
 * add-on 120 bytes more. Every handle handed out has been asked
 * after here first, so here its index is set: once, rather than at
 * each handout. */
static __attribute__((noinline)) int there(int index)
{
    if (index < 0 || index >= STDL_NumJoysticks()) {
        return 0;
    }
    joy[index].index = pad[index].index = index;
    return 1;
}

static __attribute__((noinline)) const char *joy_name(int index)
{
    return STDL_HasPad(index) ? "Xpad pad" : "Atari joystick";
}

/* Joystick i's controller's buttons, read and their changes sent; all
 * let go for one that goes, which comes back at rest */
static void buttons(int i, int gone)
{
    int b;
    if (!pad[i].open || pad_events != SDL_ENABLE) {
        return;
    }
    for (b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) {
        const Uint8 now =
            (!gone && STDL_JoystickInputHeld(i, pad_input[b])) ? 1 : 0;
        if (now != pad[i].held[b]) {
            SDL_Event e;
            memset(&e, 0, sizeof(e));
            e.type = now ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
            e.cbutton.which = i;
            e.cbutton.button = (Uint8)b;
            e.cbutton.state = now;
            pad[i].held[b] = now;
            stdl_sdl2_push(&e);
        }
    }
}

void stdl_sdl2_joy_event(const STDL_Event *se)
{
    SDL_Event e;
    /* `which` is in the same place in each of STDL's joystick events */
    const int i = se->jaxis.which;
    if (i >= JOYS) {
        return;
    }
    touched |= 1u << i;
    memset(&e, 0, sizeof(e));
    switch (se->type) {
    case STDL_JOYAXISMOTION:
        if (se->jaxis.axis >= AXES) {
            return;
        }
        if (joy[i].open && joy_events == SDL_ENABLE) {
            e.type = SDL_JOYAXISMOTION;
            e.jaxis.which = i;
            e.jaxis.axis = se->jaxis.axis;
            e.jaxis.value = se->jaxis.value;
            stdl_sdl2_push(&e);
        }
        if (pad[i].open && pad_events == SDL_ENABLE) {
            memset(&e, 0, sizeof(e));
            e.type = SDL_CONTROLLERAXISMOTION;
            e.caxis.which = i;
            e.caxis.axis = se->jaxis.axis;
            e.caxis.value = se->jaxis.value;
            stdl_sdl2_push(&e);
        }
        break;
    case STDL_JOYHATMOTION:
        if (joy[i].open && joy_events == SDL_ENABLE) {
            e.type = SDL_JOYHATMOTION;
            e.jhat.which = i;
            e.jhat.hat = 0;
            e.jhat.value = se->jhat.value;
            stdl_sdl2_push(&e);
        }
        break;
    case STDL_JOYBUTTONDOWN:
    case STDL_JOYBUTTONUP:
        if (se->jbutton.button >= BUTTONS) {
            return;
        }
        if (joy[i].open && joy_events == SDL_ENABLE) {
            const int down = se->type == STDL_JOYBUTTONDOWN;
            e.type = down ? SDL_JOYBUTTONDOWN : SDL_JOYBUTTONUP;
            e.jbutton.which = i;
            e.jbutton.button = se->jbutton.button;
            e.jbutton.state = down ? SDL_PRESSED : SDL_RELEASED;
            stdl_sdl2_push(&e);
        }
        break;
    case STDL_JOYDEVICEREMOVED:
        /* the controller's releases before it goes, as STDL sent the
         * joystick's; what is held after it comes back is said at the
         * pump's end (touched) */
        buttons(i, 1);
        /* fall through */
    case STDL_JOYDEVICEADDED: {
        /* each REMOVED is its ADDED's number plus one, and the
         * controller's `which` is where the joystick's is */
        const int gone = se->type == STDL_JOYDEVICEREMOVED;
        e.jdevice.which = i;
        if (stdl_sdl2_inited & SDL_INIT_JOYSTICK) {
            e.type = SDL_JOYDEVICEADDED + gone;
            stdl_sdl2_push(&e);
        }
        if (stdl_sdl2_inited & SDL_INIT_GAMECONTROLLER) {
            e.type = SDL_CONTROLLERDEVICEADDED + gone;
            stdl_sdl2_push(&e);
        }
        break;
    }
    default:
        break;
    }
}

/* A controller's buttons move only with an STDL event for its
 * joystick (an axis, a button, a hat or a device; none is lost), so
 * only those are read, fifteen calls each, rather than every open
 * one's every pump. */
void stdl_sdl2_controller_poll(void)
{
    const unsigned these = touched;
    int i;
    touched = 0;
    for (i = 0; i < JOYS; i++) {
        if (these & (1u << i)) {
            buttons(i, 0);
        }
    }
}

/* ------------------------------------------------------------------ */
/* the joystick                                                        */

int SDL_NumJoysticks(void)
{
    return STDL_NumJoysticks();
}

const char *SDL_JoystickNameForIndex(int device_index)
{
    return there(device_index) ? joy_name(device_index) : NULL;
}

SDL_Joystick *SDL_JoystickOpen(int device_index)
{
    if (!there(device_index)) {
        SDL_SetError("There is no joystick %d", device_index);
        return NULL;
    }
    joy[device_index].open++;
    return &joy[device_index];
}

void SDL_JoystickClose(SDL_Joystick *joystick)
{
    if (ours_joy(joystick) && joystick->open > 0) {
        joystick->open--;
    }
}

const char *SDL_JoystickName(SDL_Joystick *joystick)
{
    return joystick ? joy_name(joystick->index) : NULL;
}

SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick *joystick)
{
    return joystick ? joystick->index : -1;
}

int SDL_JoystickNumAxes(SDL_Joystick *joystick)
{
    return joystick ? (STDL_HasPad(joystick->index) ? AXES : 2) : -1;
}

int SDL_JoystickNumButtons(SDL_Joystick *joystick)
{
    return joystick ? (STDL_HasPad(joystick->index) ? BUTTONS : 1) : -1;
}

int SDL_JoystickNumHats(SDL_Joystick *joystick)
{
    return joystick ? (STDL_HasPad(joystick->index) ? 1 : 0) : -1;
}

Sint16 SDL_JoystickGetAxis(SDL_Joystick *joystick, int axis)
{
    /* STDL's, which says 0 for an axis out of range */
    return joystick ? STDL_GetJoystickAxis(joystick->index, axis) : 0;
}

/* STDL's own, as the controller's are, so the two agree */
Uint8 SDL_JoystickGetButton(SDL_Joystick *joystick, int button)
{
    return (joystick && (unsigned)button < BUTTONS
            && STDL_JoystickInputHeld(joystick->index,
                                      STDL_JOYSTICK_FIRE + button)) ? 1 : 0;
}

Uint8 SDL_JoystickGetHat(SDL_Joystick *joystick, int which)
{
    return (joystick && which == 0) ? STDL_GetJoystickHat(joystick->index)
                                     : 0;
}

int SDL_JoystickRumble(SDL_Joystick *joystick, Uint16 low, Uint16 high,
                       Uint32 duration_ms)
{
    if (!joystick) {
        return SDL_InvalidParamError("joystick");
    }
    if (STDL_PadRumble(joystick->index, (Uint8)(low >> 8),
                        (Uint8)(high >> 8),
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
    return there(joystick_index) ? SDL_TRUE : SDL_FALSE;
}

SDL_GameController *SDL_GameControllerOpen(int joystick_index)
{
    int b;
    SDL_GameController *c;
    if (!there(joystick_index)) {
        SDL_SetError("There is no joystick %d", joystick_index);
        return NULL;
    }
    c = &pad[joystick_index];
    /* what is held now is no news */
    if (!c->open) {
        for (b = 0; b < SDL_CONTROLLER_BUTTON_MAX; b++) {
            c->held[b] = STDL_JoystickInputHeld(joystick_index, pad_input[b])
                         ? 1 : 0;
        }
    }
    c->open++;
    return c;
}

void SDL_GameControllerClose(SDL_GameController *gamecontroller)
{
    if (ours_pad(gamecontroller) && gamecontroller->open > 0) {
        gamecontroller->open--;
    }
}

const char *SDL_GameControllerName(SDL_GameController *gamecontroller)
{
    return gamecontroller ? joy_name(gamecontroller->index) : NULL;
}

const char *SDL_GameControllerNameForIndex(int joystick_index)
{
    return there(joystick_index) ? joy_name(joystick_index) : NULL;
}

/* until the program asks STDL for fewer joysticks than this one's */
SDL_bool SDL_GameControllerGetAttached(SDL_GameController *gamecontroller)
{
    return (gamecontroller && there(gamecontroller->index)) ? SDL_TRUE
                                                            : SDL_FALSE;
}

SDL_Joystick *SDL_GameControllerGetJoystick(SDL_GameController *gamecontroller)
{
    return gamecontroller ? &joy[gamecontroller->index] : NULL;
}

Uint8 SDL_GameControllerGetButton(SDL_GameController *gamecontroller,
                                  SDL_GameControllerButton button)
{
    if (!gamecontroller || (unsigned)button >= SDL_CONTROLLER_BUTTON_MAX) {
        return 0;
    }
    return STDL_JoystickInputHeld(gamecontroller->index, pad_input[button])
           ? 1 : 0;
}

Sint16 SDL_GameControllerGetAxis(SDL_GameController *gamecontroller,
                                 SDL_GameControllerAxis axis)
{
    return gamecontroller ? STDL_GetJoystickAxis(gamecontroller->index, axis)
                          : 0;
}

int SDL_GameControllerRumble(SDL_GameController *gamecontroller, Uint16 low,
                             Uint16 high, Uint32 duration_ms)
{
    return SDL_JoystickRumble(SDL_GameControllerGetJoystick(gamecontroller),
                              low, high, duration_ms);
}

int SDL_GameControllerEventState(int state)
{
    if (state == SDL_ENABLE || state == SDL_IGNORE) {
        pad_events = state;
    }
    return pad_events;
}
