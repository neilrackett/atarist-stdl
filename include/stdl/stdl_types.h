/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Core types and the surface format contract.
 *
 * Surface format (see docs/format.md):
 *   - Pixels are grouped in 16s; a group is `planes` consecutive words.
 *   - Pixel x occupies bit 15 - (x & 15), MSB first, in each plane word.
 *   - Group address: pixels + y * stride + (x >> 4) * planes * 2.
 *   - v1 is low resolution only: 320x200, 4 planes, stride 160.
 */

#ifndef STDL_TYPES_H
#define STDL_TYPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct { int16_t x, y; }                STDL_Point;
typedef struct { int16_t x, y; uint16_t w, h; } STDL_Rect;

/*
 * One span for the batched span calls: `len` pixels starting at
 * (x, y), running right for the horizontal entry points and down
 * for the vertical ones. A span with len <= 0 is skipped, so a
 * caller can keep a fixed-size array and vary how much of it is
 * used. Packed to 6 bytes and walked with a pointer - a whole
 * frame's worth of spans is a few hundred bytes of the caller's
 * own memory.
 */
typedef struct { int16_t x, y, len; }           STDL_Span;

typedef struct { uint8_t r, g, b, unused; }     STDL_Colour;
#define STDL_Color STDL_Colour

/* Logical palette: RGB888 entries, quantised to ST 3-bit / STE 4-bit
 * per channel when programmed into hardware. */
typedef struct STDL_Palette {
    int          ncolors;
    STDL_Colour *colors;
} STDL_Palette;

/* Shaped after the SDL 1.2 fields ported code actually reads; the
 * mask/shift/loss/alpha fields exist only so ported diagnostics
 * compile - they are always zero on a palette device. */
typedef struct STDL_PixelFormat {
    STDL_Palette *palette;
    uint8_t       BitsPerPixel;   /* 4 */
    uint8_t       BytesPerPixel;  /* 1: nominal, for ported index tests */
    uint8_t       Rloss, Gloss, Bloss, Aloss;
    uint8_t       Rshift, Gshift, Bshift, Ashift;
    uint32_t      Rmask, Gmask, Bmask, Amask;
    uint32_t      colorkey;
    uint8_t       alpha;
} STDL_PixelFormat;

/*
 * Surface flags. Values are numerically identical to SDL 1.2's so
 * ported flag tests work through the compat header unchanged.
 */
#define STDL_SWSURFACE  0x00000000u
#define STDL_HWSURFACE  0x00000001u
#define STDL_HWACCEL    0x00000100u
#define STDL_SRCKEY     0x00001000u  /* colour-keyed: blit uses mask     */
#define STDL_MONO_AUTO  0x00400000u  /* post-v1: SM124 2x2 emulation     */
#define STDL_SCREEN     0x00800000u  /* this is the (or a) screen page   */
#define STDL_ANYFORMAT  0x10000000u
#define STDL_HWPALETTE  0x20000000u
#define STDL_DOUBLEBUF  0x40000000u
#define STDL_FULLSCREEN 0x80000000u

typedef struct STDL_Surface {
    uint8_t  *pixels;      /* base address of planar data               */
    int16_t   w, h;        /* dimensions in pixels                      */
    union {                /* bytes per scanline                        */
        uint16_t stride;
        uint16_t pitch;    /* SDL-compat alias                          */
    };
    uint8_t   planes;      /* 4 in v1                                   */
    uint8_t   colourkey;   /* palette index treated as transparent      */
    uint32_t  flags;
    STDL_Rect clip;        /* current clip rectangle                    */

    /* Transparency mask, built by STDL_SetColourKey (or
     * STDL_CreateMask): 1 bit per pixel, one word per group, bit
     * set = destination preserved. NULL = fully opaque. Blits and
     * draw primitives onto a masked surface maintain it. */
    uint8_t  *mask;
    uint16_t  maskstride;

    /* Origin offset (STDL_SetSurfaceOrigin): logical coordinate of
     * the surface's top-left pixel. Applied to blits and rect
     * fills, so a short stripe can stand in for a tall virtual
     * surface (scrolling camera) with game coordinates unchanged. */
    int16_t   org_x, org_y;

    /* cached STDL_SurfaceIsOpaque scan: 0 unknown, 1 opaque, 2 not */
    uint8_t   opaque_state;

    STDL_PixelFormat *format;  /* per-surface logical palette           */
} STDL_Surface;

/* Fill "colour" that makes masked pixels transparent again */
#define STDL_TRANSPARENT 16

typedef struct STDL_Sprite  STDL_Sprite;
typedef struct STDL_Tileset STDL_Tileset;
typedef struct STDL_Font    STDL_Font;

#ifdef __cplusplus
}
#endif

#endif /* STDL_TYPES_H */
