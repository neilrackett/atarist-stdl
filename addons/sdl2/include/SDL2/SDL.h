/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The SDL2 add-on: an SDL2-shaped library for a TT or a Falcon, on
 * STDL. A program asks for it by name - this directory on its
 * include path (or its parent, for <SDL2/SDL.h>) and libstdl-sdl2.a
 * ahead of libstdl.a on its link - and nothing else ever pulls it in.
 * SDL_stdinc.h says how the names are kept apart from the core's SDL
 * 1.2 shim.
 *
 * SDL_Init refuses anything but a TT or a Falcon: the add-on draws on
 * the 256-colour responsive screen (stdl_responsive.h), which an ST
 * has not got.
 */

#ifndef STDL_SDL2_SDL_H
#define STDL_SDL2_SDL_H

#include "SDL_stdinc.h"
#include "SDL_error.h"
#include "SDL_hints.h"
#include "SDL_version.h"

#ifdef __cplusplus
extern "C" {
#endif

/* SDL2's values. STDL_Init takes the whole machine whatever it is
 * asked for, so these only say what SDL_WasInit reports. */
#define SDL_INIT_TIMER          0x00000001u
#define SDL_INIT_AUDIO          0x00000010u
#define SDL_INIT_VIDEO          0x00000020u  /* implies EVENTS */
#define SDL_INIT_JOYSTICK       0x00000200u  /* implies EVENTS */
#define SDL_INIT_HAPTIC         0x00001000u
#define SDL_INIT_GAMECONTROLLER 0x00002000u  /* implies JOYSTICK */
#define SDL_INIT_EVENTS         0x00004000u
#define SDL_INIT_SENSOR         0x00008000u
#define SDL_INIT_NOPARACHUTE    0x00100000u  /* ignored, as in SDL2 */
#define SDL_INIT_EVERYTHING \
    (SDL_INIT_TIMER | SDL_INIT_AUDIO | SDL_INIT_VIDEO | SDL_INIT_EVENTS | \
     SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC | SDL_INIT_GAMECONTROLLER | \
     SDL_INIT_SENSOR)

#define SDL_Init          stdl_sdl2_Init
#define SDL_InitSubSystem stdl_sdl2_InitSubSystem
#define SDL_QuitSubSystem stdl_sdl2_QuitSubSystem
#define SDL_WasInit       stdl_sdl2_WasInit
#define SDL_Quit          stdl_sdl2_Quit

/*
 * SDL_Init calls STDL_Init, which does nothing when the program has
 * already called it, so a program may use STDL directly beside the
 * add-on. SDL_Quit is STDL_Quit: it puts the desktop back and ends
 * the library for both, so a program that goes on using STDL after
 * the add-on does not call it.
 */
extern int SDL_Init(Uint32 flags);
extern int SDL_InitSubSystem(Uint32 flags);
extern void SDL_QuitSubSystem(Uint32 flags);
extern Uint32 SDL_WasInit(Uint32 flags);
extern void SDL_Quit(void);

#ifdef __cplusplus
}
#endif

#endif /* STDL_SDL2_SDL_H */
