/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The host STDL_FillPolygon tests, run on the machine.
 *
 * tests/host/test_polygon.c checks the fill against an exact
 * reference, but natively, where SET and XOR rows are the C twins.
 * The 68000 rows are only covered here. Rerun it after touching
 * src/polygon.c - the 68000 fast path (stdl_poly_fast and its row
 * loops) runs nowhere else.
 *
 * The default build checks a hundredth of the host's random polygons,
 * with every fixed case (the extremes, the edges past 256 rows, a
 * deeply clipped top), for real hardware. With -DPGCHK_FULL it checks
 * a tenth, a job for the emulator: about 85 seconds on a plain ST
 * under fast-forward, against run.sh's two minute wait, but 28
 * minutes on a real Mega STE at 8MHz - the reference's exact
 * arithmetic, not the fill. Run the full build in Hatari:
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Iinclude -Isrc -Ilib/xpad/src -DPGCHK_FULL \
 *       -o dist/PGFULL.TOS tests/hatari/pgchk.c libstdl.a
 *   TOS=$PWD/tmp/tos/etos256uk.img MACHINE=st EXTRA="--memsize 4" \
 *       tests/hatari/run.sh pg dist/PGFULL.TOS 8 "waitfor PG:done"
 *
 * and the default one, dist/PGCHK.TOS, on the machine.
 */
#include <stdio.h>
#ifdef PGCHK_FULL
#define POLY_ITERS 300
#else
#define POLY_ITERS 30
#endif
#define main pgchk_suite
#include "../host/test_polygon.c"
#undef main

int main(void)
{
    int r;

    if (STDL_Init(0) < 0) {
        return 1;
    }
    r = pgchk_suite();
    STDL_Quit();
    printf("PG:done %s\n", r == 0 ? "PASS" : "FAIL");
    fflush(stdout);
    return r;
}
