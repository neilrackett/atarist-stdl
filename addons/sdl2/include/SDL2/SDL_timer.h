/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The SDL2 add-on's clock: STDL's, milliseconds since STDL_Init in the
 * 200Hz timer's 5ms steps.
 */

#ifndef STDL_SDL2_TIMER_H
#define STDL_SDL2_TIMER_H

#include "SDL_stdinc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDL_GetTicks stdl_sdl2_GetTicks
#define SDL_Delay    stdl_sdl2_Delay

#define SDL_TICKS_PASSED(A, B) ((Sint32)((B) - (A)) <= 0)

extern Uint32 SDL_GetTicks(void);
/* rounded up to the clock's 5ms; STDL's audio and timer services run
 * while it waits */
extern void SDL_Delay(Uint32 ms);

#ifdef __cplusplus
}
#endif

#endif /* STDL_SDL2_TIMER_H */
