/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The add-on's public headers, compiled by `make sdl2` as gnu99 and as
 * C++98 under g++ 4.6 with warnings as errors (a C++ port includes
 * them as they are), and once more with the SDL 1.2 shim's guard
 * defined, which must fail. Compiled only, never linked.
 */

#include "SDL.h"

int stdl_sdl2_hdrcheck(void)
{
    SDL_version v;
    SDL_GetVersion(&v);
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        return SDL_SetError("%s %d", SDL_GetError(), v.major);
    }
    return SDL_GetHintBoolean(SDL_HINT_RENDER_VSYNC, SDL_FALSE)
           + (int)SDL_WasInit(0);
}
