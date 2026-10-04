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
 * src/polygon.c. A tenth of the host's polygons, a minute or so on a
 * plain ST under fast-forward.
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Iinclude -Isrc -Ilib/xpad/src \
 *       -o dist/PGCHK.TOS tests/hatari/pgchk.c libstdl.a
 *   TOS=$PWD/tmp/tos/etos256uk.img MACHINE=st EXTRA="--memsize 4" \
 *       tests/hatari/run.sh pg dist/PGCHK.TOS 8 "waitfor PG:done"
 */
#include <stdio.h>
#define POLY_ITERS 300
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
