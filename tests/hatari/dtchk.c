/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The host STDL_Dirty tests, run on the machine.
 *
 * tests/host/test_dirty.c checks the restore lists and compares the
 * direct restore with STDL_BlitSurfaceEx byte for byte, but natively,
 * where the direct restore's row loop is the C twin. The 68000 loop
 * is only covered here. Rerun it after touching src/dirty.c.
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Iinclude -Isrc -Ilib/xpad/src \
 *       -o dist/DTCHK.TOS tests/hatari/dtchk.c libstdl.a
 *   TOS=$PWD/tmp/tos/etos256uk.img MACHINE=st EXTRA="--memsize 4" \
 *       tests/hatari/run.sh dt dist/DTCHK.TOS 8 "waitfor DT:done"
 */
#define main dtchk_suite
#include "../host/test_dirty.c"
#undef main

int main(void)
{
    int r;

    if (STDL_Init(0) < 0) {
        return 1;
    }
    r = dtchk_suite();
    STDL_Quit();
    printf("DT:done %s\n", r == 0 ? "PASS" : "FAIL");
    fflush(stdout);
    return r;
}
