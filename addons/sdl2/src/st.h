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

/*
 * The palette's first ncol colours (256 at most) as 16: map[i] is
 * colour i's slot, out[] the 16 slots' colours. Slot 0 is black and
 * takes every black, every index past ncol and the border index x
 * (when not 0); the other 15 share out the rest. Returns what
 * stdl_sdl2_st_fit says of the result.
 */
Uint32 stdl_sdl2_st_reduce(const SDL_Color *col, int ncol, unsigned x,
                         Uint8 *map, SDL_Color *out);

/* a map's 16 colours over a palette, each slot the mean of the colours
 * mapped to it and slot 0 black; returns how far the colours are from
 * their slots', every channel's difference summed */
Uint32 stdl_sdl2_st_fit(const SDL_Color *col, int ncol, unsigned x,
                        const Uint8 *map, SDL_Color *out);

/* A palette change: map is the one in use, and stays if it still fits
 * (st.c says when), else becomes the palette's own reduction; out is
 * the 16 colours either way. 1 when the map moved. */
int stdl_sdl2_st_update(const SDL_Color *col, int ncol, unsigned x,
                        Uint8 *map, SDL_Color *out);

/* map as the conversion's table of 4 by 256 longs: a slot's four
 * bits, a byte each, shifted 3, 2, 1 and 0 */
void stdl_sdl2_st_spread(const Uint8 *map, Uint32 *tab);

/* h rows of `groups` groups of 16 pixels from src, sp bytes a row,
 * through tab to four planes at dst, dp bytes a row, 8 bytes a group;
 * at least one of each, 32,768 groups at most */
void stdl_sdl2_st_c2p(const Uint8 *src, long sp, Uint8 *dst, long dp,
                      long groups, long h, const Uint32 *tab);

/* video.c: the renderer's map as its pages have it, for the tests */
const Uint8 *stdl_sdl2_st_map(SDL_Renderer *renderer);

#endif /* STDL_SDL2_ST_H */
