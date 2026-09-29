/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The host BlitIndexed8 tests, run on the machine.
 *
 * tests/host/test_blit8.c checks STDL_BlitIndexed8 against a
 * per-pixel reference model - every source walk, every clip edge, the
 * mask flags - but natively, where the pixel gather is the C twin.
 * The 68000 gather and group merge that the target runs are only
 * covered by running the same checks here, as PIXCHK does for
 * test_pixel.c. Rerun it after touching src/blit8.c.
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Iinclude -Ilib/xpad/src \
 *       -o dist/I8CHK.TOS tests/hatari/i8chk.c libstdl.a
 *   TOS=$PWD/tmp/tos/etos256uk.img MACHINE=st EXTRA="--memsize 4" \
 *       tests/hatari/run.sh i8 dist/I8CHK.TOS 8 "waitfor I8:done"
 */
#define main i8chk_suite
#include "../host/test_blit8.c"
#undef main

int main(void)
{
    int r = i8chk_suite();

    printf("I8:done %s\n", r == 0 ? "PASS" : "FAIL");
    fflush(stdout);
    STDL_Quit();
    return r;
}
