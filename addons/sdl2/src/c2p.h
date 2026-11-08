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

/* one of the assembler's loops: as stdl_sdl2_c2p, a line a row or two */
typedef void stdl_sdl2_c2p_fn(const uint8_t *src, long sp, uint8_t *dst,
                              long dp, long groups, long h, uint32_t x2,
                              uint32_t x0);

/*
 * A variant: the loops for one X, a line a row and two. `plain` is X =
 * 0; stdl_sdl2_c2p_x10 to _xf0 (c2px.c, an object each) are a fixed X's,
 * which SDL_stdl.h's STDL_SDL2_SetBorderIndex names for a constant X so
 * that a program links that one; `any` (c2pany.c) reads X's two longs
 * at run time, a little slower.
 */
struct stdl_sdl2_c2pv {
    stdl_sdl2_c2p_fn *one;
    stdl_sdl2_c2p_fn *two;
};
extern const struct stdl_sdl2_c2pv stdl_sdl2_c2p_plain, stdl_sdl2_c2p_any;
extern const struct stdl_sdl2_c2pv
    stdl_sdl2_c2p_x10, stdl_sdl2_c2p_x20, stdl_sdl2_c2p_x30,
    stdl_sdl2_c2p_x40, stdl_sdl2_c2p_x50, stdl_sdl2_c2p_x60,
    stdl_sdl2_c2p_x70, stdl_sdl2_c2p_x80, stdl_sdl2_c2p_x90,
    stdl_sdl2_c2p_xa0, stdl_sdl2_c2p_xb0, stdl_sdl2_c2p_xc0,
    stdl_sdl2_c2p_xd0, stdl_sdl2_c2p_xe0, stdl_sdl2_c2p_xf0;

/*
 * h rows (at least one) of `groups` groups (at least one) from src,
 * sp bytes a row, to dst, dp bytes a line; with dbl each row goes to
 * two lines, dp apart (a TT's doubled rows), and dst steps 2*dp a
 * row. x2 and x0 are the XOR's two longs (stdl_sdl2_c2p_xor), and v the
 * variant for that X, whose assembler every box goes through on the
 * machine (the host has the C alone). groups is 32,768 at most. src
 * needs no alignment on the 68020; dst is a group's first byte.
 */
void stdl_sdl2_c2p(const uint8_t *src, long sp, uint8_t *dst, long dp,
                   long groups, long h, int dbl,
                   const struct stdl_sdl2_c2pv *v, uint32_t x2, uint32_t x0);

/* The C, whatever the width: the assembler's twin, which the host
 * runs and the machine checks the assembler against. */
void stdl_sdl2_c2p_c(const uint8_t *src, long sp, uint8_t *dst, long dp,
                     long groups, long h, int dbl, uint32_t x2,
                     uint32_t x0);

/*
 * The XOR by X as two longs. After the merges planes 4 and 5 are one
 * long's high and low words, as the screen has them, and planes 6 and
 * 7 another's, so X's bits 4-7 are each a word of ones; X's low nibble
 * must be 0.
 */
static inline void stdl_sdl2_c2p_xor(unsigned x, uint32_t *x2, uint32_t *x0)
{
    *x2 = ((x & 0x10) ? 0xFFFF0000UL : 0) | ((x & 0x20) ? 0x0000FFFFUL : 0);
    *x0 = ((x & 0x40) ? 0xFFFF0000UL : 0) | ((x & 0x80) ? 0x0000FFFFUL : 0);
}

#endif /* STDL_SDL2_C2P_H */
