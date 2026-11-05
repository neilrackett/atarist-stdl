/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The SDL2 add-on's game controller: joystick 0 as SDL2's controller
 * layout, which a pad has as it is and a joystick has in part - its
 * directions are the d-pad and its fire is A. The buttons are read
 * from STDL_JoyInputHeld; the axes are the joystick's.
 */

#ifndef STDL_SDL2_GAMECONTROLLER_H
#define STDL_SDL2_GAMECONTROLLER_H

#include "SDL_stdinc.h"
#include "SDL_joystick.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _SDL_GameController SDL_GameController;

typedef enum {
    SDL_CONTROLLER_AXIS_INVALID = -1,
    SDL_CONTROLLER_AXIS_LEFTX,
    SDL_CONTROLLER_AXIS_LEFTY,
    SDL_CONTROLLER_AXIS_RIGHTX,
    SDL_CONTROLLER_AXIS_RIGHTY,
    SDL_CONTROLLER_AXIS_TRIGGERLEFT,
    SDL_CONTROLLER_AXIS_TRIGGERRIGHT,
    SDL_CONTROLLER_AXIS_MAX
} SDL_GameControllerAxis;

typedef enum {
    SDL_CONTROLLER_BUTTON_INVALID = -1,
    SDL_CONTROLLER_BUTTON_A,
    SDL_CONTROLLER_BUTTON_B,
    SDL_CONTROLLER_BUTTON_X,
    SDL_CONTROLLER_BUTTON_Y,
    SDL_CONTROLLER_BUTTON_BACK,
    SDL_CONTROLLER_BUTTON_GUIDE,
    SDL_CONTROLLER_BUTTON_START,
    SDL_CONTROLLER_BUTTON_LEFTSTICK,
    SDL_CONTROLLER_BUTTON_RIGHTSTICK,
    SDL_CONTROLLER_BUTTON_LEFTSHOULDER,
    SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,
    SDL_CONTROLLER_BUTTON_DPAD_UP,
    SDL_CONTROLLER_BUTTON_DPAD_DOWN,
    SDL_CONTROLLER_BUTTON_DPAD_LEFT,
    SDL_CONTROLLER_BUTTON_DPAD_RIGHT,
    SDL_CONTROLLER_BUTTON_MAX
} SDL_GameControllerButton;

#define SDL_IsGameController           stdl_sdl2_IsGameController
#define SDL_GameControllerOpen         stdl_sdl2_GameControllerOpen
#define SDL_GameControllerClose        stdl_sdl2_GameControllerClose
#define SDL_GameControllerName         stdl_sdl2_GameControllerName
#define SDL_GameControllerNameForIndex stdl_sdl2_GameControllerNameForIndex
#define SDL_GameControllerGetAttached  stdl_sdl2_GameControllerGetAttached
#define SDL_GameControllerGetJoystick  stdl_sdl2_GameControllerGetJoystick
#define SDL_GameControllerGetButton    stdl_sdl2_GameControllerGetButton
#define SDL_GameControllerGetAxis      stdl_sdl2_GameControllerGetAxis
#define SDL_GameControllerRumble       stdl_sdl2_GameControllerRumble
#define SDL_GameControllerEventState   stdl_sdl2_GameControllerEventState

extern SDL_bool SDL_IsGameController(int joystick_index);
extern SDL_GameController *SDL_GameControllerOpen(int joystick_index);
extern void SDL_GameControllerClose(SDL_GameController *gamecontroller);
extern const char *SDL_GameControllerName(SDL_GameController *gamecontroller);
extern const char *SDL_GameControllerNameForIndex(int joystick_index);
extern SDL_bool SDL_GameControllerGetAttached(SDL_GameController *gamecontroller);
extern SDL_Joystick *SDL_GameControllerGetJoystick(SDL_GameController *gamecontroller);
extern Uint8 SDL_GameControllerGetButton(SDL_GameController *gamecontroller,
                                         SDL_GameControllerButton button);
extern Sint16 SDL_GameControllerGetAxis(SDL_GameController *gamecontroller,
                                        SDL_GameControllerAxis axis);
extern int SDL_GameControllerRumble(SDL_GameController *gamecontroller,
                                    Uint16 low, Uint16 high,
                                    Uint32 duration_ms);
extern int SDL_GameControllerEventState(int state);

#ifdef __cplusplus
}
#endif

#endif /* STDL_SDL2_GAMECONTROLLER_H */
