/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The SDL2 add-on's error string: one, set by the call that failed
 * and kept until the next failure or SDL_ClearError, as in SDL2.
 */

#ifndef STDL_SDL2_ERROR_H
#define STDL_SDL2_ERROR_H

#include "SDL_stdinc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDL_SetError   stdl_sdl2_SetError
#define SDL_GetError   stdl_sdl2_GetError
#define SDL_ClearError stdl_sdl2_ClearError

/* always -1, so a failing call can `return SDL_SetError(...)` */
extern int SDL_SetError(const char *fmt, ...) SDL_PRINTF_VARARG_FUNC(1);
extern const char *SDL_GetError(void);
extern void SDL_ClearError(void);

#define SDL_OutOfMemory()  SDL_SetError("Out of memory")
#define SDL_Unsupported()  SDL_SetError("That operation is not supported")
#define SDL_InvalidParamError(param) \
    SDL_SetError("Parameter '%s' is invalid", (param))

#ifdef __cplusplus
}
#endif

#endif /* STDL_SDL2_ERROR_H */
