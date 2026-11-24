/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * SDL_GetDesktopDisplayMode (SDL_video.h): the largest window this
 * build gives on this machine. An object of its own, so that a program
 * that never asks carries none of it.
 */

#include "internal.h"

int SDL_GetDesktopDisplayMode(int displayIndex, SDL_DisplayMode *mode)
{
    int wide = 0;
    if (displayIndex != 0) {
        return SDL_SetError("There is one display, 0, not %d", displayIndex);
    }
    if (!mode) {
        return SDL_InvalidParamError("mode");
    }
    if (!(stdl_sdl2_inited & SDL_INIT_VIDEO)) {
        return SDL_SetError("SDL_Init(SDL_INIT_VIDEO) comes first");
    }
#ifdef STDL_SDL2_ST
    /* the experimental build's wide screen, a TT's or a Falcon's */
    wide = !STDL_SDL2_NO_256();
#endif
    mode->format = SDL_PIXELFORMAT_INDEX8;
    mode->w = wide ? 640 : 320;
    mode->h = wide ? 480 : 240;
    mode->refresh_rate = 0;
    mode->driverdata = NULL;
    return 0;
}
