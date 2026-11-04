/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The SDL2 add-on's renderer: textures of SDL_PIXELFORMAT_INDEX8
 * copied to the screen without scaling, and converted to its planes
 * at the present. What differs from SDL2, all of it from the machine:
 *
 * - Textures are SDL_PIXELFORMAT_INDEX8 only, which SDL2 itself
 *   refuses, with one palette for the screen
 *   (STDL_SDL2_SetRenderPalette, SDL_stdl.h).
 * - No scaling: a copy's source and destination are the same size,
 *   and the logical size, at most 320x240, is centred on the screen.
 * - The screen keeps what was drawn. SDL2 leaves it undefined after a
 *   present, so a program that clears and redraws every frame is
 *   right here too, and pays only for what it draws: a present
 *   converts the groups of 16 pixels this frame's copies and clears
 *   touched, and copies into the page the groups it missed from the
 *   page last shown.
 * - A copy's pixels are read when its batch is converted: at the
 *   present, or sooner when the next copy comes from another texture
 *   or another place in this one, or when the texture is updated,
 *   locked or destroyed. That is SDL2's own rule for a texture's
 *   updates; a texture over a program's own pixels
 *   (STDL_SDL2_CreateTextureFrom) is read then too.
 */

#ifndef STDL_SDL2_RENDER_H
#define STDL_SDL2_RENDER_H

#include "SDL_stdinc.h"
#include "SDL_rect.h"
#include "SDL_pixels.h"
#include "SDL_video.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;

/* SDL2's values; one renderer, so the first two say nothing, and
 * every present waits for the display */
typedef enum {
    SDL_RENDERER_SOFTWARE      = 0x00000001,
    SDL_RENDERER_ACCELERATED   = 0x00000002,
    SDL_RENDERER_PRESENTVSYNC  = 0x00000004,
    SDL_RENDERER_TARGETTEXTURE = 0x00000008
} SDL_RendererFlags;

typedef enum {
    SDL_TEXTUREACCESS_STATIC,
    SDL_TEXTUREACCESS_STREAMING,
    SDL_TEXTUREACCESS_TARGET     /* refused: there are no targets */
} SDL_TextureAccess;

#define SDL_CreateRenderer        stdl_sdl2_CreateRenderer
#define SDL_DestroyRenderer       stdl_sdl2_DestroyRenderer
#define SDL_RenderSetLogicalSize  stdl_sdl2_RenderSetLogicalSize
#define SDL_RenderGetLogicalSize  stdl_sdl2_RenderGetLogicalSize
#define SDL_SetRenderDrawColor    stdl_sdl2_SetRenderDrawColor
#define SDL_RenderClear           stdl_sdl2_RenderClear
#define SDL_RenderCopy            stdl_sdl2_RenderCopy
#define SDL_RenderPresent         stdl_sdl2_RenderPresent
#define SDL_CreateTexture         stdl_sdl2_CreateTexture
#define SDL_DestroyTexture        stdl_sdl2_DestroyTexture
#define SDL_UpdateTexture         stdl_sdl2_UpdateTexture
#define SDL_LockTexture           stdl_sdl2_LockTexture
#define SDL_UnlockTexture         stdl_sdl2_UnlockTexture
#define SDL_QueryTexture          stdl_sdl2_QueryTexture

/* one renderer a window; index and flags are taken as SDL2's */
extern SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, int index,
                                        Uint32 flags);
extern void SDL_DestroyRenderer(SDL_Renderer *renderer);

/* at most 320x240, centred (x on a 16-pixel group); 0x0 is the
 * window's own size. Nothing is cleared. */
extern int SDL_RenderSetLogicalSize(SDL_Renderer *renderer, int w, int h);
extern void SDL_RenderGetLogicalSize(SDL_Renderer *renderer, int *w, int *h);

/* the palette entry nearest the colour, the lowest of equals, as
 * RenderClear meets it; alpha is ignored */
extern int SDL_SetRenderDrawColor(SDL_Renderer *renderer, Uint8 r, Uint8 g,
                                  Uint8 b, Uint8 a);
/* the whole screen, as SDL2 clears its whole target */
extern int SDL_RenderClear(SDL_Renderer *renderer);
extern int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture,
                          const SDL_Rect *srcrect, const SDL_Rect *dstrect);
/* nothing drawn since the last: no flip, colours staged if changed,
 * and with SDL_RENDERER_PRESENTVSYNC a wait for the display */
extern void SDL_RenderPresent(SDL_Renderer *renderer);

extern SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format,
                                      int access, int w, int h);
extern void SDL_DestroyTexture(SDL_Texture *texture);
extern int SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect,
                             const void *pixels, int pitch);
extern int SDL_LockTexture(SDL_Texture *texture, const SDL_Rect *rect,
                           void **pixels, int *pitch);
extern void SDL_UnlockTexture(SDL_Texture *texture);
extern int SDL_QueryTexture(SDL_Texture *texture, Uint32 *format, int *access,
                            int *w, int *h);

#ifdef __cplusplus
}
#endif

#endif /* STDL_SDL2_RENDER_H */
