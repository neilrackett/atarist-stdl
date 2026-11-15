/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The SDL2 add-on's mouse: STDL's, its position in the renderer's
 * logical coordinates (the window's, before there is a renderer). There
 * is no cursor on a responsive screen (STDL refuses its cursor there),
 * so SDL_ShowCursor reports it hidden whatever it is asked.
 *
 * The relative state is the IKBD's own movement, summed from the motion
 * events as they are taken, since the last SDL_GetRelativeMouseState:
 * it keeps coming at a screen's edge, where the position stops, in
 * either mode. So SDL_SetRelativeMouseMode only records the mode (there
 * is no cursor to hide or pointer to hold).
 */

#ifndef STDL_SDL2_MOUSE_H
#define STDL_SDL2_MOUSE_H

#include "SDL_stdinc.h"
#include "SDL_events.h"
#include "SDL_video.h"

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

#define SDL_ShowCursor              stdl_sdl2_ShowCursor
#define SDL_GetMouseState           stdl_sdl2_GetMouseState
#define SDL_GetRelativeMouseState   stdl_sdl2_GetRelativeMouseState
#define SDL_SetRelativeMouseMode    stdl_sdl2_SetRelativeMouseMode
#define SDL_GetRelativeMouseMode    stdl_sdl2_GetRelativeMouseMode
#define SDL_WarpMouseInWindow       stdl_sdl2_WarpMouseInWindow

extern int SDL_ShowCursor(int toggle);
extern Uint32 SDL_GetMouseState(int *x, int *y);
extern Uint32 SDL_GetRelativeMouseState(int *x, int *y);
extern int SDL_SetRelativeMouseMode(SDL_bool enabled);
extern SDL_bool SDL_GetRelativeMouseMode(void);
/* to (x, y) of the renderer's logical size; the window is the one */
extern void SDL_WarpMouseInWindow(SDL_Window *window, int x, int y);

#ifdef __cplusplus
}
#endif

#endif /* STDL_SDL2_MOUSE_H */
