/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The SDL2 add-on's platform: "Atari", whose machines are a TT and a
 * Falcon here.
 */

#ifndef STDL_SDL2_PLATFORM_H
#define STDL_SDL2_PLATFORM_H

#include "SDL_stdinc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define __ATARI__ 1

#define SDL_GetPlatform stdl_sdl2_GetPlatform

extern const char *SDL_GetPlatform(void);

#ifdef __cplusplus
}
#endif

#endif /* STDL_SDL2_PLATFORM_H */
