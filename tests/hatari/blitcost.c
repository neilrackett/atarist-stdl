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
static uint32_t one(STDL_Surface *dst, STDL_Surface *src, int w, int h)
{
    STDL_Rect s = { 0, 0, (int16_t)w, (int16_t)h };
    STDL_Rect d = { 0, 0, 0, 0 };
    uint32_t t0 = STDL_GetTicks();
    int i;
    for (i = 0; i < ITER; i++) { d.x = 0; d.y = 0;
        STDL_BlitSurface(src, &s, dst, &d); }
    return STDL_GetTicks() - t0;
}
int main(int argc, char *argv[])
{
    static const int ws[] = { 16, 32, 64, 128, 192, 320 };
    static const int hs[] = { 8, 16, 32 };
    STDL_Surface *dst, *src; int wi, hi;
    (void)argc; (void)argv;
    STDL_Init(STDL_INIT_VIDEO);
    STDL_SetVideoMode(320, 200, 4, 0);
    dst = STDL_CreateSurface(336, 200); src = STDL_CreateSurface(336, 200);
    printf("BC: w h allowed cpu1 cpu2 verdict\n"); fflush(stdout);
    for (hi = 0; hi < 3; hi++) for (wi = 0; wi < 6; wi++) {
        uint32_t def, c; unsigned long b;
        STDL_UseBlitter(0); c = one(dst, src, ws[wi], hs[hi]);
        STDL_UseBlitter(1); def = one(dst, src, ws[wi], hs[hi]);
        b = 0;
        printf("BC: %3d %2d allowed=%6lu cpu=%6lu %s\n", ws[wi], hs[hi],
               (unsigned long)def, (unsigned long)c,
               def <= c + c / 50 ? "ok" : "SLOWER");
        fflush(stdout);
    }
    printf("BC: done\n"); fflush(stdout);
    STDL_Quit(); return 0;
}
