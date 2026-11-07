/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The SDL2 add-on's mouse: STDL's, its position in the renderer's
 * logical coordinates (the window's, before there is a renderer). There
 * is no cursor on a responsive screen (STDL refuses its cursor there),
 * so SDL_ShowCursor reports it hidden whatever it is asked.
 */

#ifndef STDL_SDL2_MOUSE_H
#define STDL_SDL2_MOUSE_H

#include "SDL_stdinc.h"
#include "SDL_events.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDL_BUTTON(X)       (1u << ((X) - 1))
#define SDL_BUTTON_LEFT     1
#define SDL_BUTTON_MIDDLE   2
#define SDL_BUTTON_RIGHT    3
#define SDL_BUTTON_LMASK    SDL_BUTTON(SDL_BUTTON_LEFT)
#define SDL_BUTTON_MMASK    SDL_BUTTON(SDL_BUTTON_MIDDLE)
#define SDL_BUTTON_RMASK    SDL_BUTTON(SDL_BUTTON_RIGHT)

#define SDL_ShowCursor    stdl_sdl2_ShowCursor
#define SDL_GetMouseState stdl_sdl2_GetMouseState

extern int SDL_ShowCursor(int toggle);
extern Uint32 SDL_GetMouseState(int *x, int *y);

#ifdef __cplusplus
}
#endif

#endif /* STDL_SDL2_MOUSE_H */
