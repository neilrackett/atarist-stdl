/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Pixel pairs: rows whose pixels 2i and 2i + 1 are the same, as a
 * program drawing at half the width has them, converted by loops that
 * read a group's 8 distinct pixels (c2p_asm.h's C2P_PGROUP): 56
 * instructions a group against 74. Two variants, as for the whole
 * loops: none for X = 0 and one that reads X's two longs, for any other
 * X (a fixed X's loops each would be 15 more objects for a call few
 * programs make).
 *
 * An object of its own, which names nothing else: the renderer's
 * STDL_SDL2_RenderCopyPairs (c2ppair.c) reaches it through a pointer,
 * and a program converting its own screen calls it through
 * SDL_stdl_c2p.h's STDL_SDL2_ConvertBoxPairs, linking neither the
 * renderer nor STDL. It was in c2ppair.c until November 2026, where
 * the call's reference to the renderer brought video.o with it.
 */

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

void stdl_sdl2_c2pp(const uint8_t *src, long sp, uint8_t *dst, long dp,
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
void stdl_sdl2_c2pp(const uint8_t *src, long sp, uint8_t *dst, long dp,
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
