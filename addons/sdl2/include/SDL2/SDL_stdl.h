/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The SDL2 add-on's Atari-only calls, which no SDL2 has: what a TT's
 * or a Falcon's screen needs said that SDL2 has no words for, and
 * what a program that manages its own screen (pushing the boxes it
 * changed, fading by the palette) needs to stay as it was. A program
 * includes this itself; SDL.h does not.
 */

#ifndef STDL_SDL2_STDL_H
#define STDL_SDL2_STDL_H

#include "SDL_render.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Hints, set before SDL_CreateWindow / SDL_CreateRenderer */

/* "2" or "3" (the default): the screen's pages. Three let a frame be
 * drawn while the display still waits to take the last; without the
 * ST-RAM for a third (153.6K on a TT, 77-92K on a Falcon) STDL gives
 * two. Read by the first SDL_CreateWindow. */
#define STDL_SDL2_HINT_PAGES "STDL_SDL2_PAGES"

/* "1": the screen check. The renderer keeps a chunky model of the
 * screen, drawn as each copy and clear is made, and before every flip
 * compares the page about to be shown with the model converted -
 * margins and all. 230K of memory and a whole page's conversion a
 * flip, for testing; STDL_SDL2_GetCheck reads the result. */
#define STDL_SDL2_HINT_CHECK "STDL_SDL2_CHECK"

/* a file name: where SDL_Log writes, a line a message, from the first
 * message on. Unset, the log goes nowhere; never to the console (see
 * SDL_log.h). */
#define STDL_SDL2_HINT_LOGFILE "STDL_SDL2_LOGFILE"

/* The renderer's palette: the screen has one, of 256 registers. The
 * renderer reads it at each present and stages it when it has changed
 * (its version), so SDL_SetPaletteColors on it is how a program
 * changes colours. The palette stays the program's to free, after the
 * renderer. */
extern int STDL_SDL2_SetRenderPalette(SDL_Renderer *renderer,
                                      SDL_Palette *palette);

/* the draw colour as an index, for SDL_RenderClear, until the next
 * SDL_SetRenderDrawColor */
extern int STDL_SDL2_SetRenderDrawIndex(SDL_Renderer *renderer, Uint8 index);

/*
 * A texture over the program's own 8-bit pixels, w by h, pitch bytes a
 * row (any pitch, any alignment), with no copy: the renderer reads
 * them when it converts a copy's batch (SDL_render.h says when), so a
 * program drawing into its buffer and copying the boxes it changed
 * pays for no copy of its own. Locked and updated as a streaming
 * texture is; destroying it leaves the pixels alone.
 */
extern SDL_Texture *STDL_SDL2_CreateTextureFrom(SDL_Renderer *renderer,
                                                void *pixels, int w, int h,
                                                int pitch);

/*
 * The border index X: a palette index the program never shows, a
 * multiple of 16 from 0x10 to 0xF0, which goes to register 0 and is
 * held black, so the border round the screen's memory (a Falcon's on
 * RGB or a TV, which nothing drawn reaches) is black whatever colour 0
 * is. Every pixel is XORed with X as it is converted, as an inversion
 * of some of planes 4-7, and the palette goes to the registers
 * permuted to match: register i takes colour i ^ X. 0, the default,
 * swaps nothing. Set before anything is drawn.
 *
 * Inline, so that a constant X links that X's conversion alone (an
 * object each, about 900 bytes, costing what X = 0 does); an X known
 * only at run time links the loop that reads it, 4.5% slower.
 */
struct stdl_sdl2_c2pv;
extern const struct stdl_sdl2_c2pv
    stdl_sdl2_c2p_x10, stdl_sdl2_c2p_x20, stdl_sdl2_c2p_x30,
    stdl_sdl2_c2p_x40, stdl_sdl2_c2p_x50, stdl_sdl2_c2p_x60,
    stdl_sdl2_c2p_x70, stdl_sdl2_c2p_x80, stdl_sdl2_c2p_x90,
    stdl_sdl2_c2p_xa0, stdl_sdl2_c2p_xb0, stdl_sdl2_c2p_xc0,
    stdl_sdl2_c2p_xd0, stdl_sdl2_c2p_xe0, stdl_sdl2_c2p_xf0,
    stdl_sdl2_c2p_any;
extern int stdl_sdl2_set_border(SDL_Renderer *renderer, unsigned x,
                                const struct stdl_sdl2_c2pv *v);

static __inline__ __attribute__((always_inline))
int STDL_SDL2_SetBorderIndex(SDL_Renderer *renderer, unsigned x)
{
    return stdl_sdl2_set_border(renderer, x,
        x == 0x00 ? 0 :
        x == 0x10 ? &stdl_sdl2_c2p_x10 : x == 0x20 ? &stdl_sdl2_c2p_x20 :
        x == 0x30 ? &stdl_sdl2_c2p_x30 : x == 0x40 ? &stdl_sdl2_c2p_x40 :
        x == 0x50 ? &stdl_sdl2_c2p_x50 : x == 0x60 ? &stdl_sdl2_c2p_x60 :
        x == 0x70 ? &stdl_sdl2_c2p_x70 : x == 0x80 ? &stdl_sdl2_c2p_x80 :
        x == 0x90 ? &stdl_sdl2_c2p_x90 : x == 0xA0 ? &stdl_sdl2_c2p_xa0 :
        x == 0xB0 ? &stdl_sdl2_c2p_xb0 : x == 0xC0 ? &stdl_sdl2_c2p_xc0 :
        x == 0xD0 ? &stdl_sdl2_c2p_xd0 : x == 0xE0 ? &stdl_sdl2_c2p_xe0 :
        x == 0xF0 ? &stdl_sdl2_c2p_xf0 : &stdl_sdl2_c2p_any);
}

/*
 * The page on show cleared now, margins and all, with no flip; the
 * other pages are cleared when next drawn on, and what this frame had
 * copied is dropped. For a program that clears its screen without
 * presenting a frame, as the one it came from did when it changed
 * picture size.
 */
extern int STDL_SDL2_ClearScreenNow(SDL_Renderer *renderer);

/* the palette staged now, without a present: it lands in the next
 * blank. A fade's steps, each a palette change and a wait. */
extern int STDL_SDL2_StagePalette(SDL_Renderer *renderer);

/* the 256 colours as last staged, in register order: register i's is
 * colour i ^ X, and register 0's black when X is set */
extern const SDL_Color *STDL_SDL2_GetRegisterColours(SDL_Renderer *renderer);

/* the page last shown, from the start of its memory (a Falcon's RGB
 * margins included), and its size; 0 and a NULL base before the first
 * flip. For a program that hashes what it showed. */
extern int STDL_SDL2_GetShownPage(SDL_Renderer *renderer,
                                  const Uint8 **base, long *bytes);

/* STDL_SDL2_HINT_CHECK's result: flips checked, flips unlike the model,
 * and the first of those and its first row unlike (-1: none) */
typedef struct STDL_SDL2_Check {
    Uint32 flips;
    Uint32 bad;
    Uint32 first_flip;
    int first_row;
} STDL_SDL2_Check;

/* -1 with the error set when the check is off */
extern int STDL_SDL2_GetCheck(SDL_Renderer *renderer, STDL_SDL2_Check *check);

#ifdef __cplusplus
}
#endif

#endif /* STDL_SDL2_STDL_H */
