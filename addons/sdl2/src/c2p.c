/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Chunky to planar (c2p.h). Ten bit-group exchanges (merges) between
 * the four longs holding a group's 16 pixels move every bit to its
 * plane; X's planes are inverted once the bits are in place.
 *
 * On the 68030 the loop is assembler, with the C as its twin: the C
 * is what runs on the host, where tests/test_c2p.c checks it against a
 * pixel-by-pixel reference, and SDL2CHK (tests/hatari/sdl2chk.c)
 * checks the assembler against it on the machine. gcc's build of the
 * C writes a group's four longs back to back, each a wait for ST-RAM;
 * the assembler parks them in a3-a6 and writes them among the next
 * group's merges, so the 68030 goes on working while each goes out.
 * One port's whole-frame conversions took 136% of their budget on a
 * Falcon in C against 121% for a routine with that overlap, and a
 * first C version of twelve merges, too big for the 68030's 256-byte
 * instruction cache, twice as long. The machine sends every box
 * through the assembler, a group wide or twenty: gcc's C, with every
 * mask an immediate and a whole merge for the 16-bit exchange, took a
 * Falcon 103.1ms a frame's worth of boxes two groups wide against the
 * assembler's 69.1 (Hatari). The loops are checked against the cache
 * when the add-on is built (sdl2.mk's sdl2check).
 *
 * This object has the C, the dispatch and the loops for X = 0, which
 * every program takes; a fixed X's loops are c2px.c's, one object an X,
 * and those for an X known only at run time c2pany.c's.
 */

#include "c2p.h"

/* a long of four pixels, the first in the top byte, as the 68000
 * reads it; the host is little-endian and may not read it unaligned */
#ifdef __m68k__
#define LD(p)    (*(const uint32_t *)(const void *)(p))
#define ST(p, v) (*(uint32_t *)(void *)(p) = (v))
#else
static inline uint32_t LD(const uint8_t *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
           (uint32_t)p[2] << 8 | (uint32_t)p[3];
}

static inline void ST(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}
#endif

/* exchange a's bits under m with b's bits under m << s */
#define MERGE(a, b, s, m) do {                          \
        const uint32_t t_ = (((b) >> (s)) ^ (a)) & (m); \
        (a) ^= t_;                                      \
        (b) ^= t_ << (s);                               \
    } while (0)

static inline __attribute__((always_inline))
void rows(const uint8_t *src, long sp, uint8_t *dst, long dp, long groups,
          long h, const int dbl, uint32_t x2, uint32_t x0)
{
    for (; h > 0; h--, src += sp, dst += dbl ? dp << 1 : dp) {
        const uint8_t *s = src;
        uint8_t *d = dst;
        long g;
        for (g = groups; g > 0; g--, s += 16, d += 16) {
            uint32_t a0 = LD(s);
            uint32_t a1 = LD(s + 4);
            uint32_t a2 = LD(s + 8);
            uint32_t a3 = LD(s + 12);
            /* A long holds four pixels; numbering the bits of a group
             * by long (k1 k0) and by place in the long from the top
             * (u4..u0), each merge swaps one k bit with one u bit, and
             * five leave the longs holding planes and their words the
             * pixels in order. */
            MERGE(a0, a1, 4, 0x0F0F0F0FUL);     /* k0 with u2 */
            MERGE(a2, a3, 4, 0x0F0F0F0FUL);
            MERGE(a0, a2, 8, 0x00FF00FFUL);     /* k1 with u3 */
            MERGE(a1, a3, 8, 0x00FF00FFUL);
            MERGE(a0, a2, 1, 0x55555555UL);     /* k1 with u0 */
            MERGE(a1, a3, 1, 0x55555555UL);
            MERGE(a0, a2, 16, 0x0000FFFFUL);    /* k1 with u4 */
            MERGE(a1, a3, 16, 0x0000FFFFUL);
            MERGE(a0, a2, 2, 0x33333333UL);     /* k1 with u1 */
            MERGE(a1, a3, 2, 0x33333333UL);
            /* planes 0 and 1 are a3's low word and high word, 2 and 3
             * a1's, 4 and 5 a2's, 6 and 7 a0's: a swap each puts them
             * in screen order (the assembler's 16-bit exchange does
             * both at once), then X's planes are inverted */
            a3 = (a3 << 16) | (a3 >> 16);
            a1 = (a1 << 16) | (a1 >> 16);
            a2 = ((a2 << 16) | (a2 >> 16)) ^ x2;
            a0 = ((a0 << 16) | (a0 >> 16)) ^ x0;
            ST(d, a3);
            ST(d + 4, a1);
            ST(d + 8, a2);
            ST(d + 12, a0);
            if (dbl) {
                ST(d + dp, a3);
                ST(d + dp + 4, a1);
                ST(d + dp + 8, a2);
                ST(d + dp + 12, a0);
            }
        }
    }
}

#undef MERGE

void stdl_sdl2_c2p_c(const uint8_t *src, long sp, uint8_t *dst, long dp,
                     long groups, long h, int dbl, uint32_t x2,
                     uint32_t x0)
{
    if (dbl) {
        rows(src, sp, dst, dp, groups, h, 1, x2, x0);
    } else {
        rows(src, sp, dst, dp, groups, h, 0, x2, x0);
    }
}

#ifdef __m68k__
#include "c2p_asm.h"

/* X = 0: no tail at all */
__asm__(C2P_PAIR("stdl_sdl2_c2p_plain", ""));

extern stdl_sdl2_c2p_fn stdl_sdl2_c2p_plain1, stdl_sdl2_c2p_plain2;
const struct stdl_sdl2_c2pv stdl_sdl2_c2p_plain = {
    stdl_sdl2_c2p_plain1, stdl_sdl2_c2p_plain2
};

void stdl_sdl2_c2p(const uint8_t *src, long sp, uint8_t *dst, long dp,
                   long groups, long h, int dbl,
                   const struct stdl_sdl2_c2pv *v, uint32_t x2, uint32_t x0)
{
    (dbl ? v->two : v->one)(src, sp, dst, dp, groups, h, x2, x0);
}
#else
/* the host has the C alone; the variants are there to be named, the
 * fixed X's among them, as c2px.c is not built there */
const struct stdl_sdl2_c2pv stdl_sdl2_c2p_plain = { 0, 0 };
const struct stdl_sdl2_c2pv
    stdl_sdl2_c2p_x10 = { 0, 0 }, stdl_sdl2_c2p_x20 = { 0, 0 },
    stdl_sdl2_c2p_x30 = { 0, 0 }, stdl_sdl2_c2p_x40 = { 0, 0 },
    stdl_sdl2_c2p_x50 = { 0, 0 }, stdl_sdl2_c2p_x60 = { 0, 0 },
    stdl_sdl2_c2p_x70 = { 0, 0 }, stdl_sdl2_c2p_x80 = { 0, 0 },
    stdl_sdl2_c2p_x90 = { 0, 0 }, stdl_sdl2_c2p_xa0 = { 0, 0 },
    stdl_sdl2_c2p_xb0 = { 0, 0 }, stdl_sdl2_c2p_xc0 = { 0, 0 },
    stdl_sdl2_c2p_xd0 = { 0, 0 }, stdl_sdl2_c2p_xe0 = { 0, 0 },
    stdl_sdl2_c2p_xf0 = { 0, 0 };

void stdl_sdl2_c2p(const uint8_t *src, long sp, uint8_t *dst, long dp,
                   long groups, long h, int dbl,
                   const struct stdl_sdl2_c2pv *v, uint32_t x2, uint32_t x0)
{
    (void)v;
    stdl_sdl2_c2p_c(src, sp, dst, dp, groups, h, dbl, x2, x0);
}
#endif
