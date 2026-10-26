/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The host STDL_FillPolygonPattern tests, run on the machine.
 *
 * tests/host/test_polypat.c checks the fill against an exact
 * reference natively, where the edge set-up and the rows are the C
 * twins. The 68000 fast path - stdl_ppoly_fast and its row loops in
 * src/polypat.c - runs nowhere else, so rerun this after touching
 * that file. Its own runner rather than a third suite in PGCHK, which
 * already sits near run.sh's two-minute wait.
 *
 * The default build checks a hundredth of the host's random
 * polygons, with every fixed case (the extremes, the edges either
 * side of the fast path's 32767 sub-pixel rows and of its old 4095,
 * clipped tops), for real hardware. With -DPPCHK_FULL it checks a
 * tenth, for the emulator: 36s and 196s on a plain ST (19s and 108s
 * on a Mega STE), the reference's checking rather than the fill.
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Iinclude -Isrc -Ilib/xpad/src -DPPCHK_FULL \
 *       -o dist/PPFULL.TOS tests/hatari/ppchk.c libstdl.a
 *   TOS=$PWD/tmp/tos/etos256uk.img MACHINE=st EXTRA="--memsize 4" \
 *       tests/hatari/run.sh pp dist/PPFULL.TOS 8 "waitfor PP:done"
 *
 * and the default one (-o dist/PPCHK.TOS, no -DPPCHK_FULL) on the
 * machine.
 */
#include <stdio.h>
#include <stdl/stdl.h>
#ifdef PPCHK_FULL
#define PP_ITERS 300
#else
#define PP_ITERS 12
#endif
#define PP_CLOCK() ((unsigned long)STDL_GetTicks())
#define main ppchk_suite
#include "../host/test_polypat.c"
#undef main

int main(void)
{
    uint32_t t0;
    int r;

    if (STDL_Init(0) < 0) {
        return 1;
    }
    t0 = STDL_GetTicks();
    r = ppchk_suite();
    printf("PP: %lu ms\n", (unsigned long)(STDL_GetTicks() - t0));
    STDL_Quit();
    printf("PP:done %s\n", r == 0 ? "PASS" : "FAIL");
    fflush(stdout);
    return r;
}
