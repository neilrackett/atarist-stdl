/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The SDL2 add-on's paths: TOS has no per-user folder to give for
 * SDL_GetPrefPath, nor a sure way for a program to find its own folder
 * for SDL_GetBasePath (a program started from the desktop runs in its
 * folder, so a relative path is the way to it). Both say so as SDL2
 * lets them, with NULL and an error, and a program falls back to its
 * own way (a file beside it, as on Windows).
 */

#ifndef STDL_SDL2_FILESYSTEM_H
#define STDL_SDL2_FILESYSTEM_H

#include "SDL_stdinc.h"
#include "SDL_error.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDL_GetBasePath stdl_sdl2_GetBasePath
#define SDL_GetPrefPath stdl_sdl2_GetPrefPath

static __inline__ char *SDL_GetBasePath(void)
{
    SDL_Unsupported();
    return NULL;
}

static __inline__ char *SDL_GetPrefPath(const char *org, const char *app)
{
    (void)org;
    (void)app;
    SDL_Unsupported();
    return NULL;
}

#ifdef __cplusplus
}
#endif

#endif /* STDL_SDL2_FILESYSTEM_H */
