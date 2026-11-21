/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Chunky to planar: 8-bit pixels into the eight interleaved planes of
 * a responsive 256-colour page (stdl_responsive.h), 16 pixels a group
 * of 16 bytes, plane 0 first. Each pixel is XORed on the way with the
 * border index X (SDL_stdl.h), a multiple of 16, which inverts some of
 * planes 4-7 and so costs two XORs a group (stdl_sdl2_c2p_xor makes
 * them).
 */

#ifndef STDL_SDL2_C2P_H
#define STDL_SDL2_C2P_H

#include <stdint.h>
/* the public declarations (the variants, stdl_sdl2_c2p, the pairs'
 * stdl_sdl2_c2pp and the XOR's longs), by path, so that this header
 * needs no include path of its own */
#include "../include/SDL2/SDL_stdl_c2p.h"

/* one of the assembler's loops: as stdl_sdl2_c2p, a line a row or two */
typedef void stdl_sdl2_c2p_fn(const uint8_t *src, long sp, uint8_t *dst,
                              long dp, long groups, long h, uint32_t x2,
                              uint32_t x0);

/*
 * A variant: the loops for one X, a line a row and two. `plain` is X =
 * 0; stdl_sdl2_c2p_x10 to _xf0 (c2px.c, an object each) are a fixed X's,
 * which SDL_stdl_c2p.h's stdl_sdl2_c2p_variant names for a constant X
 * so that a program links that one; `any` (c2pany.c) reads X's two
 * longs at run time, a little slower.
 */
struct stdl_sdl2_c2pv {
    stdl_sdl2_c2p_fn *one;
    stdl_sdl2_c2p_fn *two;
};

/* stdl_sdl2_c2p, a box through a variant, is SDL_stdl_c2p.h's */

/* The C, whatever the width: the assembler's twin, which the host
 * runs and the machine checks the assembler against. */
void stdl_sdl2_c2p_c(const uint8_t *src, long sp, uint8_t *dst, long dp,
                     long groups, long h, int dbl, uint32_t x2,
                     uint32_t x0);

/* the pair loops' entry, stdl_sdl2_c2pp (c2pp.c, SDL_stdl_c2p.h) */
typedef void stdl_sdl2_c2pp_fn(const uint8_t *src, long sp, uint8_t *dst,
                               long dp, long groups, long h, int dbl,
                               uint32_t x2, uint32_t x0);

#endif /* STDL_SDL2_C2P_H */
