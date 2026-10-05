/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The host STDL_BlitSprites tests, run on the machine.
 *
 * tests/host/test_spritebatch.c compares STDL_BlitSprites with
 * STDL_BlitSprite byte for byte, but natively, where the row loop is
 * the C twin. The 68000 loop, with the batch's own arguments,
 * is only covered here. Rerun it after touching src/spritebatch.c.
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Iinclude -Isrc -Ilib/xpad/src \
 *       -o dist/SBCHK.TOS tests/hatari/sbchk.c libstdl.a
 *   TOS=$PWD/tmp/tos/etos256uk.img MACHINE=st EXTRA="--memsize 4" \
 *       tests/hatari/run.sh sb dist/SBCHK.TOS 8 "waitfor SB:done"
 */
#define main sbchk_suite
#include "../host/test_spritebatch.c"
#undef main

int main(void)
{
    int r;

    if (STDL_Init(0) < 0) {
        return 1;
    }
    r = sbchk_suite();
    STDL_Quit();
    printf("SB:done %s\n", r == 0 ? "PASS" : "FAIL");
    fflush(stdout);
    return r;
}
