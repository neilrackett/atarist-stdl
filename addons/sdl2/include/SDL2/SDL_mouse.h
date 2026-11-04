/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The SDL2 add-on's cursor: there is none on a responsive screen
 * (STDL refuses its cursor there), so SDL_ShowCursor reports it hidden
 * whatever it is asked.
 */

#ifndef STDL_SDL2_MOUSE_H
#define STDL_SDL2_MOUSE_H

#include "SDL_stdinc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDL_QUERY   (-1)
#define SDL_IGNORE  0
#define SDL_DISABLE 0
#define SDL_ENABLE  1

#define SDL_ShowCursor stdl_sdl2_ShowCursor

extern int SDL_ShowCursor(int toggle);

#ifdef __cplusplus
}
#endif

#endif /* STDL_SDL2_MOUSE_H */
