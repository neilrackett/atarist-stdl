/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The SDL2 add-on's basic types and C library names. The add-on is
 * an SDL2-shaped library for a TT or a Falcon that lives beside STDL
 * rather than in it: nothing in the core includes or links it, and a
 * program gets it only by naming its include path and its archive
 * (libstdl-sdl2.a), never by accident.
 *
 * Every public name in these headers is a macro onto a lowercase
 * link name (SDL_Init is stdl_sdl2_Init), so the archive defines no
 * SDL_ symbol: the core's SDL 1.2 shim (src/compat.c, inside
 * libstdl.a) defines SDL_Delay, SDL_FillRect and the joystick calls
 * with SDL 1.2's signatures, and a program linking both would
 * otherwise meet "multiple definition" the first time anything pulled
 * compat.o in. A call these headers do not declare is a compile
 * error by design, as in the 1.2 shim: build with
 * -Werror=implicit-function-declaration, since gcc 4.6 accepts an
 * undeclared call and leaves it to the link.
 */

#ifndef STDL_SDL2_STDINC_H
#define STDL_SDL2_STDINC_H

/* the 1.2 shim's SDL.h (include/compat) and these cannot share a
 * file: their types and values differ under the same names */
#ifdef STDL_COMPAT_SDL_H
#error "STDL's SDL2 add-on and its SDL 1.2 shim cannot meet in one file: drop -Iinclude/compat"
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t  Uint8;
typedef int8_t   Sint8;
typedef uint16_t Uint16;
typedef int16_t  Sint16;
typedef uint32_t Uint32;
typedef int32_t  Sint32;
typedef uint64_t Uint64;
typedef int64_t  Sint64;

typedef enum {
    SDL_FALSE = 0,
    SDL_TRUE  = 1
} SDL_bool;

#define DECLSPEC
#define SDLCALL
#define SDL_FORCE_INLINE static __inline__ __attribute__((always_inline))
#define SDL_PRINTF_FORMAT_STRING
#define SDL_PRINTF_VARARG_FUNC(n) \
    __attribute__((__format__(__printf__, n, (n) + 1)))

#define SDL_arraysize(a)  (sizeof(a) / sizeof((a)[0]))
#define SDL_zero(x)       SDL_memset(&(x), 0, sizeof((x)))
#define SDL_zerop(x)      SDL_memset((x), 0, sizeof(*(x)))
#define SDL_min(x, y)     (((x) < (y)) ? (x) : (y))
#define SDL_max(x, y)     (((x) > (y)) ? (x) : (y))
#define SDL_clamp(x, a, b) \
    (((x) < (a)) ? (a) : (((x) > (b)) ? (b) : (x)))
#define SDL_FOURCC(A, B, C, D) \
    ((Uint32)(Uint8)(A) | ((Uint32)(Uint8)(B) << 8) | \
     ((Uint32)(Uint8)(C) << 16) | ((Uint32)(Uint8)(D) << 24))

/* the C library's own: SDL2 programs reach for these by these names */
#define SDL_malloc   malloc
#define SDL_calloc   calloc
#define SDL_realloc  realloc
#define SDL_free     free
#define SDL_memset   memset
#define SDL_memcpy   memcpy
#define SDL_memmove  memmove
#define SDL_memcmp   memcmp
#define SDL_strlen   strlen
#define SDL_strcmp   strcmp
#define SDL_strncmp  strncmp
#define SDL_strchr   strchr
#define SDL_strrchr  strrchr
#define SDL_strstr   strstr
#define SDL_atoi     atoi
#define SDL_strtol   strtol
#define SDL_snprintf  snprintf
#define SDL_vsnprintf vsnprintf
#define SDL_getenv   getenv
#define SDL_qsort    qsort
#define SDL_abs      abs

#ifdef __cplusplus
}
#endif

#endif /* STDL_SDL2_STDINC_H */
