/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_SDL2_RenderCopyPairs (SDL_stdl.h): a copy whose pixels come in
 * pairs, as a program drawing at half the width has them, converted
 * by loops that read a group's 8 distinct pixels (c2p_asm.h's
 * C2P_PGROUP): 56 instructions a group against 74. The loops are in
 * this object and reached only through the pointer the call gives the
 * renderer, so a program that never asks for pairs links none of them.
 *
 * Two variants, as for the whole loops: none for X = 0 and one that
 * reads X's two longs, for any other X (a fixed X's loops each would
 * be 15 more objects for a call few programs make).
 */

#include "SDL_stdl.h"
#include "internal.h"
#include "c2p.h"

#ifdef __m68k__
#include "c2p_asm.h"

/* X = 0: no tail */
__asm__(C2P_PPAIR("stdl_sdl2_c2pp_plain", ""));

/* any X: its two longs from the arguments, onto planes 4-5 and 6-7 */
__asm__(C2P_PPAIR("stdl_sdl2_c2pp_any",
                  "	move.l	104(%sp),%d7\n"
                  "	eor.l	%d7,%d3\n"
                  "	move.l	108(%sp),%d7\n"
                  "	eor.l	%d7,%d2\n"));

extern stdl_sdl2_c2p_fn stdl_sdl2_c2pp_plain1, stdl_sdl2_c2pp_plain2,
                        stdl_sdl2_c2pp_any1, stdl_sdl2_c2pp_any2;

static void pairs(const uint8_t *src, long sp, uint8_t *dst, long dp,
                  long groups, long h, int dbl, uint32_t x2, uint32_t x0)
{
    stdl_sdl2_c2p_fn *f;
    if (x2 | x0) {
        f = dbl ? stdl_sdl2_c2pp_any2 : stdl_sdl2_c2pp_any1;
    } else {
        f = dbl ? stdl_sdl2_c2pp_plain2 : stdl_sdl2_c2pp_plain1;
    }
    f(src, sp, dst, dp, groups, h, x2, x0);
}
#else
/* the host: the loops' C twin, a group at a time */
static void pairs(const uint8_t *src, long sp, uint8_t *dst, long dp,
                  long groups, long h, int dbl, uint32_t x2, uint32_t x0)
{
    for (; h > 0; h--, src += sp, dst += dbl ? dp << 1 : dp) {
        long g;
        for (g = 0; g < groups; g++) {
            uint8_t in[16];
            int i;
            for (i = 0; i < 16; i++) {
                in[i] = src[g * 16 + (i < 8 ? (i & ~1) : (i | 1))];
            }
            stdl_sdl2_c2p_c(in, 16, dst + g * 16, dp, 1, 1, dbl, x2, x0);
        }
    }
}
#endif

/* A group is converted as pairs only if every copy of the batch that
 * reached it was a pairs copy covering it whole (internal.h) */
static void mark(Uint32 *pair, const Uint32 *whole, const Uint32 *part,
                 int h, int sx, int w, int pairing)
{
    int g0, g1;
    Uint32 m;
    if (pairing) {
        g0 = (sx + 15) >> 4;
        g1 = ((sx + w) >> 4) - 1;
    } else {
        g0 = sx >> 4;
        g1 = (sx + w - 1) >> 4;
    }
    if (g0 > g1) {
        return;
    }
    m = (Uint32)(((1UL << (g1 + 1)) - 1) & ~((1UL << g0) - 1));
    for (; h > 0; h--, pair++, whole++, part++) {
        const Uint32 p = *pair;
        *pair = pairing ? p | (m & ~((*whole & ~p) | *part)) : p & ~m;
    }
}

static const struct stdl_sdl2_pairs ops = { pairs, mark };

int STDL_SDL2_RenderCopyPairs(SDL_Renderer *renderer, SDL_Texture *texture,
                              const SDL_Rect *srcrect,
                              const SDL_Rect *dstrect)
{
    return stdl_sdl2_copy_pairs(renderer, texture, srcrect, dstrect, &ops);
}
