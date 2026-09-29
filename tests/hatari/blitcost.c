/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * On-target blit cost sweep: for each size it times the library's
 * own choice against the CPU path forced, so a regression in the
 * BLiTTER decision shows up as "SLOWER" rather than as a frame rate
 * nobody can explain. The constants in STDL_BLIT_CPU_ROW,
 * STDL_BLIT_CPU_CELL and STDL_BLIT_SETUP were fitted to runs of
 * this on an emulated STE - re-run it if either path changes.
 *
 * Build by hand; keep the stem to eight characters, because
 * Hatari's GEMDOS drive maps names to 8.3 and BLITCOST2.TOS would
 * silently run BLITCOST.TOS:
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Iinclude -Iinclude/compat -Ilib/xpad/src \
 *       -o dist/BLITCOST.TOS tests/hatari/blitcost.c libstdl.a
 *   MACHINE=ste tests/hatari/run.sh bc dist/BLITCOST.TOS 12 "sleep 220"
 *
 * Run it on ste and on megaste: the CPU path is about 1.8x faster
 * at 16MHz with the cache, so the crossover moves and the library
 * carries one set of constants for both.
 */
#include <stdio.h>
#include "stdl/stdl.h"
#define ITER 1500
/* dx, sx: destination and source x; unequal phases make the blit
 * unaligned (the BLiTTER's skewed passes) */
static uint32_t one(STDL_Surface *dst, STDL_Surface *src, int w, int h,
                    int dx, int sx)
{
    STDL_Rect s = { (int16_t)sx, 0, (int16_t)w, (int16_t)h };
    STDL_Rect d = { 0, 0, 0, 0 };
    uint32_t t0 = STDL_GetTicks();
    int i;
    for (i = 0; i < ITER; i++) { d.x = (int16_t)dx; d.y = 0;
        STDL_BlitSurface(src, &s, dst, &d); }
    return STDL_GetTicks() - t0;
}
int main(int argc, char *argv[])
{
    static const int ws[] = { 16, 32, 64, 128, 192, 320 };
    static const int hs[] = { 8, 16, 32 };
    /* kinds: same-phase plain, same-phase keyed, unaligned plain,
     * unaligned keyed, and same phase with both edges partial - each
     * has its own threshold */
    static const char *const kind[5] = { "aligned", "aligned keyed",
                                         "shift", "shift keyed",
                                         "edges" };
    STDL_Surface *dst, *src, *srck; int wi, hi, k;
    (void)argc; (void)argv;
    STDL_Init(STDL_INIT_VIDEO);
    STDL_SetVideoMode(320, 200, 4, 0);
    dst = STDL_CreateSurface(336, 200); src = STDL_CreateSurface(336, 200);
    srck = STDL_CreateSurface(336, 200);
    {
        int x, y;
        for (y = 0; y < 200; y++)
            for (x = 0; x < 336; x++)
                STDL_PutPixel(srck, x, y, (uint8_t)((x ^ y) % 5));
        STDL_SetColourKey(srck, 1, 3);
    }
    printf("BC: src %p dst %p stride %u, long aligned: %s\n",
           (void *)src->pixels, (void *)dst->pixels,
           (unsigned)src->stride,
           ((((uintptr_t)src->pixels | (uintptr_t)dst->pixels) & 3) == 0)
           ? "yes - short rows copy inline"
           : "NO - short rows fall back to memcpy");
    printf("BC: kind w h allowed cpu verdict\n"); fflush(stdout);
    for (k = 0; k < 5; k++)
    for (hi = 0; hi < 3; hi++) for (wi = 0; wi < 6; wi++) {
        STDL_Surface *s = (k & 1) ? srck : src;
        int w = ws[wi] - ((k & 2) ? 16 : 0), dx = (k & 2) ? 5 : 0;
        int sx = 0;
        uint32_t def, c;
        if (k == 4) {
            /* same phase, both edges partial: the CPU merges them */
            w = ws[wi] - 6;
            sx = dx = 5;
        }
        if (w <= 0) {
            w = 11;
        }
        STDL_UseBlitter(0); c = one(dst, s, w, hs[hi], dx, sx);
        STDL_UseBlitter(1); def = one(dst, s, w, hs[hi], dx, sx);
        printf("BC: %-13s %3d %2d allowed=%6lu cpu=%6lu %s\n", kind[k],
               w, hs[hi], (unsigned long)def, (unsigned long)c,
               def <= c + c / 50 ? "ok" : "SLOWER");
        fflush(stdout);
    }
    printf("BC: done\n"); fflush(stdout);
    STDL_Quit(); return 0;
}
