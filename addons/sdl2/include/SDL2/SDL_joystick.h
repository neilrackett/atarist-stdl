/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The SDL2 add-on's joysticks: STDL's, by the same numbers. One unless
 * the program asks STDL for more (STDL_SetNumJoysticks, up to six):
 * 0 is the joystick port and 1 the mouse port, each with the pad of its
 * number merged in, and 2 and 3 pads 2 and 3; or, with the pads apart
 * (STDL_UseJoystickMerge(0)), 2 to 5 pads 0 to 3. With an Xpad pad
 * behind it a joystick has 6 axes (the left stick, the right, the
 * triggers), 13 buttons (0 is fire, the pad's lower face button) and a
 * hat; a joystick alone has 2 axes and a button.
 * Each one's instance ID is its index. Events come for one once it is
 * opened, as in SDL2.
 */

#ifndef STDL_SDL2_JOYSTICK_H
#define STDL_SDL2_JOYSTICK_H

#include "SDL_stdinc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _SDL_Joystick SDL_Joystick;
typedef Sint32 SDL_JoystickID;

#define SDL_HAT_CENTERED  0x00
#define SDL_HAT_UP        0x01
#define SDL_HAT_RIGHT     0x02
#define SDL_HAT_DOWN      0x04
#define SDL_HAT_LEFT      0x08
#define SDL_HAT_RIGHTUP   (SDL_HAT_RIGHT | SDL_HAT_UP)
#define SDL_HAT_RIGHTDOWN (SDL_HAT_RIGHT | SDL_HAT_DOWN)
#define SDL_HAT_LEFTUP    (SDL_HAT_LEFT | SDL_HAT_UP)
#define SDL_HAT_LEFTDOWN  (SDL_HAT_LEFT | SDL_HAT_DOWN)

#define SDL_JOYSTICK_AXIS_MAX 32767
#define SDL_JOYSTICK_AXIS_MIN -32768

#define SDL_NumJoysticks          stdl_sdl2_NumJoysticks
#define SDL_JoystickNameForIndex  stdl_sdl2_JoystickNameForIndex
#define SDL_JoystickOpen          stdl_sdl2_JoystickOpen
#define SDL_JoystickClose         stdl_sdl2_JoystickClose
#define SDL_JoystickName          stdl_sdl2_JoystickName
#define SDL_JoystickInstanceID    stdl_sdl2_JoystickInstanceID
#define SDL_JoystickNumAxes       stdl_sdl2_JoystickNumAxes
#define SDL_JoystickNumButtons    stdl_sdl2_JoystickNumButtons
#define SDL_JoystickNumHats       stdl_sdl2_JoystickNumHats
#define SDL_JoystickGetAxis       stdl_sdl2_JoystickGetAxis
#define SDL_JoystickGetButton     stdl_sdl2_JoystickGetButton
#define SDL_JoystickGetHat        stdl_sdl2_JoystickGetHat
#define SDL_JoystickRumble        stdl_sdl2_JoystickRumble
#define SDL_JoystickEventState    stdl_sdl2_JoystickEventState

extern int SDL_NumJoysticks(void);
extern const char *SDL_JoystickNameForIndex(int device_index);
extern SDL_Joystick *SDL_JoystickOpen(int device_index);
extern void SDL_JoystickClose(SDL_Joystick *joystick);
extern const char *SDL_JoystickName(SDL_Joystick *joystick);
extern SDL_JoystickID SDL_JoystickInstanceID(SDL_Joystick *joystick);
extern int SDL_JoystickNumAxes(SDL_Joystick *joystick);
extern int SDL_JoystickNumButtons(SDL_Joystick *joystick);
extern int SDL_JoystickNumHats(SDL_Joystick *joystick);
extern Sint16 SDL_JoystickGetAxis(SDL_Joystick *joystick, int axis);
extern Uint8 SDL_JoystickGetButton(SDL_Joystick *joystick, int button);
extern Uint8 SDL_JoystickGetHat(SDL_Joystick *joystick, int hat);
/* STDL_PadRumble's motors, the top byte of each strength */
extern int SDL_JoystickRumble(SDL_Joystick *joystick, Uint16 low,
                              Uint16 high, Uint32 duration_ms);
extern int SDL_JoystickEventState(int state);

#ifdef __cplusplus
}
#endif

#endif /* STDL_SDL2_JOYSTICK_H */
