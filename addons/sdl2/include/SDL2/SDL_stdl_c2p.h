/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The add-on's conversion alone, for a program that draws its own
 * chunky screen and converts it itself, with no SDL_Renderer: a box of
 * 8-bit pixels into the eight interleaved planes of a TT's or a
 * Falcon's 256-colour screen (stdl_responsive.h), whole pixels or
 * pixel pairs. Nothing else of SDL2 is declared here, so a program
 * that never includes SDL.h can include this one:
 *
 *     -I$STDL/addons/sdl2/include       #include <SDL2/SDL_stdl_c2p.h>
 *
 * and links the add-on's archive for the conversion's objects alone
 * (docs/sdl2.md, "Converting a program's own screen"): no renderer, and
 * no libstdl.a unless the program uses STDL itself.
 */

#ifndef STDL_SDL2_STDL_C2P_H
#define STDL_SDL2_STDL_C2P_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * A box of `groups` 16-pixel groups by `rows` rows, from src (pixel
 * 0 of its first row, src_pitch bytes a row) to dst (its first group
 * on its first line, dst_pitch bytes a line), the screen's eight
 * planes a word each, 16 bytes a group. With two_lines each row goes
 * to two lines, dst_pitch apart (a TT's doubled rows in TT Low), and
 * dst moves on 2 * dst_pitch a row. Nothing is done for groups or rows
 * under 1; groups is 32,768 at most.
 *
 * x is the border index X (SDL_stdl.h's STDL_SDL2_SetBorderIndex): 0,
 * or a multiple of 16 up to 0xF0 that every pixel is XORed with. A
 * constant links only that X's loops: X = 0's are c2p.o's, which every
 * program that converts links (1,452 bytes with the C twin), and a
 * fixed X's are about 700 bytes more. An X from a variable links the
 * loops that read it at run time (704 bytes), about 4.5% slower.
 *
 * For a TT or a Falcon (a 68030): src may be at any address there.
 * The loops have no instruction a 68000 lacks, so a 68000 program may
 * call this once it knows it is on one of those machines.
 */
static __inline__ __attribute__((always_inline))
void STDL_SDL2_ConvertBox(const uint8_t *src, long src_pitch, uint8_t *dst,
                          long dst_pitch, long groups, long rows,
                          int two_lines, unsigned x);

/*
 * The same box for pixels in pairs: the program's word that in every
 * group the pixels 2i and 2i + 1 are the same, as a program drawing at
 * half the width (a game's low detail) has them. Each group is
 * converted from its 8 distinct pixels, 56 instructions against 74.
 * Pixels not in pairs come out as one of each pair: the first in a
 * group's first half, the second in its second. It pays in whole
 * boxes: one call for a whole view, not a call a row, nor a box for
 * each run of a group or two (docs/sdl2.md's numbers). x as above, but
 * any X but 0 takes the loops that read it at run time; both are in
 * one object, c2pp.o, 1,404 bytes.
 */
static __inline__ __attribute__((always_inline))
void STDL_SDL2_ConvertBoxPairs(const uint8_t *src, long src_pitch,
                               uint8_t *dst, long dst_pitch, long groups,
                               long rows, int two_lines, unsigned x);

/* What the calls above name; a program calls those, not these. */

/* one X's loops, a line a row and two (c2p.h) */
struct stdl_sdl2_c2pv;
extern const struct stdl_sdl2_c2pv
    stdl_sdl2_c2p_plain, stdl_sdl2_c2p_any,
    stdl_sdl2_c2p_x10, stdl_sdl2_c2p_x20, stdl_sdl2_c2p_x30,
    stdl_sdl2_c2p_x40, stdl_sdl2_c2p_x50, stdl_sdl2_c2p_x60,
    stdl_sdl2_c2p_x70, stdl_sdl2_c2p_x80, stdl_sdl2_c2p_x90,
    stdl_sdl2_c2p_xa0, stdl_sdl2_c2p_xb0, stdl_sdl2_c2p_xc0,
    stdl_sdl2_c2p_xd0, stdl_sdl2_c2p_xe0, stdl_sdl2_c2p_xf0;

/* c2p.c: the box through variant v's loops, at least a group and a
 * row; x2 and x0 are X as stdl_sdl2_c2p_xor makes them */
extern void stdl_sdl2_c2p(const uint8_t *src, long sp, uint8_t *dst,
                          long dp, long groups, long h, int dbl,
                          const struct stdl_sdl2_c2pv *v, uint32_t x2,
                          uint32_t x0);

/* c2pp.c: the same for pixel pairs, X = 0's loops or the run-time
 * XOR's as x2 and x0 say */
extern void stdl_sdl2_c2pp(const uint8_t *src, long sp, uint8_t *dst,
                           long dp, long groups, long h, int dbl,
                           uint32_t x2, uint32_t x0);

/*
 * The XOR by X as two longs. After the merges planes 4 and 5 are one
 * long's high and low words, as the screen has them, and planes 6 and
 * 7 another's, so X's bits 4-7 are each a word of ones; X's low nibble
 * must be 0.
 */
static __inline__ __attribute__((always_inline))
void stdl_sdl2_c2p_xor(unsigned x, uint32_t *x2, uint32_t *x0)
{
    *x2 = ((x & 0x10) ? 0xFFFF0000UL : 0) | ((x & 0x20) ? 0x0000FFFFUL : 0);
    *x0 = ((x & 0x40) ? 0xFFFF0000UL : 0) | ((x & 0x80) ? 0x0000FFFFUL : 0);
}

/* X's variant: a constant names its own, so a program links that one;
 * an X from a variable takes the run-time XOR's, or X = 0's when 0 */
static __inline__ __attribute__((always_inline))
const struct stdl_sdl2_c2pv *stdl_sdl2_c2p_variant(unsigned x)
{
    if (!__builtin_constant_p(x)) {
        return x ? &stdl_sdl2_c2p_any : &stdl_sdl2_c2p_plain;
    }
    return x == 0x00 ? &stdl_sdl2_c2p_plain :
        x == 0x10 ? &stdl_sdl2_c2p_x10 : x == 0x20 ? &stdl_sdl2_c2p_x20 :
        x == 0x30 ? &stdl_sdl2_c2p_x30 : x == 0x40 ? &stdl_sdl2_c2p_x40 :
        x == 0x50 ? &stdl_sdl2_c2p_x50 : x == 0x60 ? &stdl_sdl2_c2p_x60 :
        x == 0x70 ? &stdl_sdl2_c2p_x70 : x == 0x80 ? &stdl_sdl2_c2p_x80 :
        x == 0x90 ? &stdl_sdl2_c2p_x90 : x == 0xA0 ? &stdl_sdl2_c2p_xa0 :
        x == 0xB0 ? &stdl_sdl2_c2p_xb0 : x == 0xC0 ? &stdl_sdl2_c2p_xc0 :
        x == 0xD0 ? &stdl_sdl2_c2p_xd0 : x == 0xE0 ? &stdl_sdl2_c2p_xe0 :
        x == 0xF0 ? &stdl_sdl2_c2p_xf0 : &stdl_sdl2_c2p_any;
}

static __inline__ __attribute__((always_inline))
void STDL_SDL2_ConvertBox(const uint8_t *src, long src_pitch, uint8_t *dst,
                          long dst_pitch, long groups, long rows,
                          int two_lines, unsigned x)
{
    uint32_t x2, x0;
    if (groups > 0 && rows > 0) {
        stdl_sdl2_c2p_xor(x, &x2, &x0);
        stdl_sdl2_c2p(src, src_pitch, dst, dst_pitch, groups, rows,
                      two_lines, stdl_sdl2_c2p_variant(x), x2, x0);
    }
}

static __inline__ __attribute__((always_inline))
void STDL_SDL2_ConvertBoxPairs(const uint8_t *src, long src_pitch,
                               uint8_t *dst, long dst_pitch, long groups,
                               long rows, int two_lines, unsigned x)
{
    uint32_t x2, x0;
    if (groups > 0 && rows > 0) {
        stdl_sdl2_c2p_xor(x, &x2, &x0);
        stdl_sdl2_c2pp(src, src_pitch, dst, dst_pitch, groups, rows,
                       two_lines, x2, x0);
    }
}

#ifdef __cplusplus
}
#endif

#endif /* STDL_SDL2_STDL_C2P_H */
