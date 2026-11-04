/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The SDL2 add-on claims SDL 2.0.10: old enough that a program
 * testing SDL_VERSION_ATLEAST takes the paths the add-on has, rather
 * than newer calls it would only meet as compile errors.
 */

#ifndef STDL_SDL2_VERSION_H
#define STDL_SDL2_VERSION_H

#include "SDL_stdinc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SDL_version {
    Uint8 major;
    Uint8 minor;
    Uint8 patch;
} SDL_version;

#define SDL_MAJOR_VERSION 2
#define SDL_MINOR_VERSION 0
#define SDL_PATCHLEVEL    10

#define SDL_VERSION(x) do {             \
        (x)->major = SDL_MAJOR_VERSION; \
        (x)->minor = SDL_MINOR_VERSION; \
        (x)->patch = SDL_PATCHLEVEL;    \
    } while (0)
#define SDL_VERSIONNUM(X, Y, Z) ((X) * 1000 + (Y) * 100 + (Z))
#define SDL_COMPILEDVERSION \
    SDL_VERSIONNUM(SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL)
#define SDL_VERSION_ATLEAST(X, Y, Z) \
    (SDL_COMPILEDVERSION >= SDL_VERSIONNUM(X, Y, Z))

#define SDL_GetVersion stdl_sdl2_GetVersion

extern void SDL_GetVersion(SDL_version *ver);

#ifdef __cplusplus
}
#endif

#endif /* STDL_SDL2_VERSION_H */
