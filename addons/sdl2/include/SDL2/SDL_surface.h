/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The SDL2 add-on's surfaces: SDL2's layout, of SDL_PIXELFORMAT_INDEX8
 * only - a byte a pixel and a palette, which a program draws into and
 * makes a texture of. Blits between them copy bytes, keyed or not; a
 * surface of any other depth is refused, as a 32-bit picture would need
 * a colour lookup per pixel on its way to the screen.
 */

#ifndef STDL_SDL2_SURFACE_H
#define STDL_SDL2_SURFACE_H

#include "SDL_stdinc.h"
#include "SDL_pixels.h"
#include "SDL_rect.h"
#include "SDL_rwops.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDL_SWSURFACE 0
#define SDL_PREALLOC  0x00000001u
#define SDL_RLEACCEL  0x00000002u
#define SDL_DONTFREE  0x00000004u

#define SDL_MUSTLOCK(S) 0

typedef struct SDL_PixelFormat {
    Uint32 format;
    SDL_Palette *palette;
    Uint8 BitsPerPixel;
    Uint8 BytesPerPixel;
    Uint8 padding[2];
    Uint32 Rmask, Gmask, Bmask, Amask;
    Uint8 Rloss, Gloss, Bloss, Aloss;
    Uint8 Rshift, Gshift, Bshift, Ashift;
    int refcount;
    struct SDL_PixelFormat *next;
} SDL_PixelFormat;

typedef struct SDL_Surface {
    Uint32 flags;
    SDL_PixelFormat *format;
    int w, h;
    int pitch;
    void *pixels;
    void *userdata;
    int locked;
    void *lock_data;
    SDL_Rect clip_rect;
    void *map;                  /* the colour key, here */
    int refcount;
} SDL_Surface;

#define SDL_CreateRGBSurface           stdl_sdl2_CreateRGBSurface
#define SDL_CreateRGBSurfaceWithFormat stdl_sdl2_CreateRGBSurfaceWithFormat
#define SDL_CreateRGBSurfaceFrom       stdl_sdl2_CreateRGBSurfaceFrom
#define SDL_FreeSurface                stdl_sdl2_FreeSurface
#define SDL_SetSurfacePalette          stdl_sdl2_SetSurfacePalette
#define SDL_LockSurface                stdl_sdl2_LockSurface
#define SDL_UnlockSurface              stdl_sdl2_UnlockSurface
#define SDL_SetColorKey                stdl_sdl2_SetColorKey
#define SDL_GetColorKey                stdl_sdl2_GetColorKey
#define SDL_SetClipRect                stdl_sdl2_SetClipRect
#define SDL_GetClipRect                stdl_sdl2_GetClipRect
#define SDL_FillRect                   stdl_sdl2_FillRect
#define SDL_UpperBlit                  stdl_sdl2_UpperBlit
#define SDL_BlitSurface                SDL_UpperBlit

/* depth 8 and no masks: SDL_PIXELFORMAT_INDEX8, with a palette of 256
 * (SDL2 gives an 8-bit surface 256 white entries) */
extern SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height,
                                         int depth, Uint32 Rmask, Uint32 Gmask,
                                         Uint32 Bmask, Uint32 Amask);
extern SDL_Surface *SDL_CreateRGBSurfaceWithFormat(Uint32 flags, int width,
                                                   int height, int depth,
                                                   Uint32 format);
extern SDL_Surface *SDL_CreateRGBSurfaceFrom(void *pixels, int width,
                                             int height, int depth, int pitch,
                                             Uint32 Rmask, Uint32 Gmask,
                                             Uint32 Bmask, Uint32 Amask);
extern void SDL_FreeSurface(SDL_Surface *surface);
extern int SDL_SetSurfacePalette(SDL_Surface *surface, SDL_Palette *palette);
extern int SDL_LockSurface(SDL_Surface *surface);
extern void SDL_UnlockSurface(SDL_Surface *surface);
extern int SDL_SetColorKey(SDL_Surface *surface, int flag, Uint32 key);
extern int SDL_GetColorKey(SDL_Surface *surface, Uint32 *key);
extern SDL_bool SDL_SetClipRect(SDL_Surface *surface, const SDL_Rect *rect);
extern void SDL_GetClipRect(SDL_Surface *surface, SDL_Rect *rect);
/* color is the index */
extern int SDL_FillRect(SDL_Surface *dst, const SDL_Rect *rect, Uint32 color);
/* clipped to the source and to the destination's clip rectangle, which
 * dstrect then holds; pixels in the colour key are skipped */
extern int SDL_UpperBlit(SDL_Surface *src, const SDL_Rect *srcrect,
                         SDL_Surface *dst, SDL_Rect *dstrect);

#ifdef __cplusplus
}
#endif

#endif /* STDL_SDL2_SURFACE_H */
