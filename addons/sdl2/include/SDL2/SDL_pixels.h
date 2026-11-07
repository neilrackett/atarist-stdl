/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The SDL2 add-on's colours and palettes. Its one pixel format is
 * SDL_PIXELFORMAT_INDEX8, a byte a pixel through a palette of up to
 * 256 colours, which a TT's and a Falcon's 256 registers hold exactly;
 * a 32-bit pixel would want a colour lookup per pixel before the
 * conversion, more than a frame has time for.
 */

#ifndef STDL_SDL2_PIXELS_H
#define STDL_SDL2_PIXELS_H

#include "SDL_stdinc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDL_ALPHA_OPAQUE      255
#define SDL_ALPHA_TRANSPARENT 0

/* SDL2's values: unknown, and index 8 (SDL_DEFINE_PIXELFORMAT of
 * SDL_PIXELTYPE_INDEX8, 8 bits, 1 byte) */
#define SDL_PIXELFORMAT_UNKNOWN 0u
#define SDL_PIXELFORMAT_INDEX8  0x13000801u

typedef struct SDL_Color {
    Uint8 r;
    Uint8 g;
    Uint8 b;
    Uint8 a;
} SDL_Color;
#define SDL_Colour SDL_Color

/* SDL2's layout; version moves on with every change, which is how a
 * renderer knows to stage the colours again */
typedef struct SDL_Palette {
    int ncolors;
    SDL_Color *colors;
    Uint32 version;
    int refcount;
} SDL_Palette;

#define SDL_AllocPalette     stdl_sdl2_AllocPalette
#define SDL_FreePalette      stdl_sdl2_FreePalette
#define SDL_SetPaletteColors stdl_sdl2_SetPaletteColors

/* ncolors entries, all white as in SDL2 */
extern SDL_Palette *SDL_AllocPalette(int ncolors);
extern void SDL_FreePalette(SDL_Palette *palette);
extern int SDL_SetPaletteColors(SDL_Palette *palette, const SDL_Color *colors,
                                int firstcolor, int ncolors);

#ifdef __cplusplus
}
#endif

#endif /* STDL_SDL2_PIXELS_H */
