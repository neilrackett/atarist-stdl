/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The ST build of the add-on, experimental (docs/sdl2.md, "An
 * experimental build for every machine"): the whole add-on for the
 * 68000, under STDL_SDL2_ST, which `make sdl2st` defines for every
 * object of libstdl-sdl2-st.a and nothing else does. A TT or a Falcon
 * gets the 256-colour screen from it; an ST gets its own screen in 16
 * colours, for testing, which is what is here. It is code for the
 * 68000 alone; internal.h refuses the macro on anything above one, so
 * none of it can reach the add-on built for a TT or a Falcon.
 */

#ifndef STDL_SDL2_ST_H
#define STDL_SDL2_ST_H

#include "internal.h"

/* the maps in use: a colour's first slot and its second (the same
 * without the checkerboard), and the error they were made with */
struct stdl_sdl2_st_maps {
    Uint8 a[256], b[256];
    Uint32 made;
    int made_valid;
};

/*
 * The palette's first ncol colours (256 at most) as 16, for a machine
 * showing `bits` a channel (3 an ST, 4 an STE; anything else: as they
 * are), made from the colours seen (seen[i] not 0; NULL: all): a[i]
 * and b[i] are colour i's two slots, which a checkerboard mixes (b is a
 * without dither), and
 * out[] the 16 slots' colours. Slot 0 is black and takes every black,
 * every index past ncol and the border index x (when not 0); the other
 * 15 are made from the colours seen, and a colour not seen goes to the
 * nearest. Returns the error, as stdl_sdl2_st_fit does.
 */
Uint32 stdl_sdl2_st_reduce(const SDL_Color *col, int ncol, unsigned x,
                           int bits, int dither, const Uint8 *seen,
                           Uint8 *a, Uint8 *b, SDL_Color *out);

/* maps' 16 colours over a palette, each slot the mean of the colours
 * seen whose first slot it is, as shown, and slot 0 black; returns how
 * far the colours seen are from what they are shown as, as a share of
 * those colours' own size */
Uint32 stdl_sdl2_st_fit(const SDL_Color *col, int ncol, unsigned x, int bits,
                        const Uint8 *seen, const Uint8 *a, const Uint8 *b,
                        SDL_Color *out);

/* 1 if the maps in use still fit the palette and the colours seen
 * (st.c says when), out their colours; 0 if new maps are wanted */
int stdl_sdl2_st_keep(const SDL_Color *col, int ncol, unsigned x, int bits,
                      const Uint8 *seen, const struct stdl_sdl2_st_maps *m,
                      SDL_Color *out);

/* new maps from the palette and the colours seen; 1 when a map moved */
int stdl_sdl2_st_remake(const SDL_Color *col, int ncol, unsigned x, int bits,
                        int dither, const Uint8 *seen,
                        struct stdl_sdl2_st_maps *m, SDL_Color *out);

/* the maps as the conversion's two tables of 4 by 256 longs each, for
 * even rows and for odd: a slot's four bits, a byte each, shifted 3,
 * 2, 1 and 0, the first slot where x + y is even */
void stdl_sdl2_st_spread(const Uint8 *a, const Uint8 *b, Uint32 *tab);

/* h rows of `groups` groups of 16 pixels from src, sp bytes a row,
 * to four planes at dst, dp bytes a row, 8 bytes a group; at least one
 * of each, 32,768 groups at most. The first row goes through tab, the
 * next through `next`, and so on in turn. */
void stdl_sdl2_st_c2p(const Uint8 *src, long sp, Uint8 *dst, long dp,
                      long groups, long h, const Uint32 *tab,
                      const Uint32 *next);

/* video.c: the renderer's maps as its pages have them, what it reduces
 * with and the colours it has seen, for the tests */
const struct stdl_sdl2_st_maps *stdl_sdl2_st_view(SDL_Renderer *renderer,
                                                  int *bits, int *dither,
                                                  const Uint8 **seen);

#endif /* STDL_SDL2_ST_H */
