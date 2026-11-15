/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The SDL2 add-on's byte order: the 68000 family is big-endian, so the
 * little-endian swaps are the ones that do anything (files from a PC:
 * WADs, WAVs, saved games), each one rotate - or rotate, swap, rotate -
 * inline, and the big-endian ones are the value itself. No call, so no
 * link symbol.
 */

#ifndef STDL_SDL2_ENDIAN_H
#define STDL_SDL2_ENDIAN_H

#include "SDL_stdinc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDL_LIL_ENDIAN 1234
#define SDL_BIG_ENDIAN 4321
#if defined(__m68k__) || defined(__BIG_ENDIAN__) \
    || (defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define SDL_BYTEORDER  SDL_BIG_ENDIAN
#else
#define SDL_BYTEORDER  SDL_LIL_ENDIAN   /* the host's tests */
#endif

#define SDL_Swap16 stdl_sdl2_Swap16
#define SDL_Swap32 stdl_sdl2_Swap32
#define SDL_Swap64 stdl_sdl2_Swap64

#ifdef __m68k__
static __inline__ Uint16 SDL_Swap16(Uint16 x)
{
    __asm__ ("rol.w #8,%0" : "+d" (x) : : "cc");
    return x;
}

static __inline__ Uint32 SDL_Swap32(Uint32 x)
{
    __asm__ ("rol.w #8,%0\n\t"
             "swap %0\n\t"
             "rol.w #8,%0" : "+d" (x) : : "cc");
    return x;
}
#else
static __inline__ Uint16 SDL_Swap16(Uint16 x)
{
    return (Uint16)(x << 8 | x >> 8);
}

static __inline__ Uint32 SDL_Swap32(Uint32 x)
{
    return x << 24 | (x & 0xFF00u) << 8 | (x >> 8 & 0xFF00u) | x >> 24;
}
#endif

static __inline__ Uint64 SDL_Swap64(Uint64 x)
{
    const Uint32 hi = (Uint32)(x >> 32);
    const Uint32 lo = (Uint32)x;
    return ((Uint64)SDL_Swap32(lo) << 32) | SDL_Swap32(hi);
}

#if SDL_BYTEORDER == SDL_BIG_ENDIAN
#define SDL_SwapLE16(X) SDL_Swap16(X)
#define SDL_SwapLE32(X) SDL_Swap32(X)
#define SDL_SwapLE64(X) SDL_Swap64(X)
#define SDL_SwapBE16(X) ((Uint16)(X))
#define SDL_SwapBE32(X) ((Uint32)(X))
#define SDL_SwapBE64(X) ((Uint64)(X))
#else
#define SDL_SwapLE16(X) ((Uint16)(X))
#define SDL_SwapLE32(X) ((Uint32)(X))
#define SDL_SwapLE64(X) ((Uint64)(X))
#define SDL_SwapBE16(X) SDL_Swap16(X)
#define SDL_SwapBE32(X) SDL_Swap32(X)
#define SDL_SwapBE64(X) SDL_Swap64(X)
#endif

#ifdef __cplusplus
}
#endif

#endif /* STDL_SDL2_ENDIAN_H */
