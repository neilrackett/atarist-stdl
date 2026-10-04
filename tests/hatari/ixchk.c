/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The host STDL_SurfaceFromIndexed8 tests, run on the machine, then
 * the conversion timed.
 *
 * tests/host/test_indexed.c checks the conversion against a
 * per-pixel reference built from STDL_GetPixel, but natively, where
 * the whole-group conversion is the C twin. The 68000 version (addx
 * into the plane words) is only covered here. Rerun it after
 * touching src/indexed.c.
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Iinclude -Ilib/xpad/src \
 *       -o dist/IXCHK.TOS tests/hatari/ixchk.c libstdl.a
 *   TOS=$PWD/tmp/tos/etos256uk.img MACHINE=st EXTRA="--memsize 4" \
 *       tests/hatari/run.sh ix dist/IXCHK.TOS 8 "waitfor IX:done"
 */
#define main ixchk_suite
#include "../host/test_indexed.c"
#undef main

/* cycles a pixel at 8MHz for a 192x70 frame, keyed and not */
static unsigned long cost(int key)
{
    static uint8_t px[192 * 70];
    uint32_t t0;
    unsigned i;
    int n;

    for (i = 0; i < sizeof px; i++) {
        px[i] = (uint8_t)(i * 7 + (i >> 5));
    }
    t0 = STDL_GetHz200();
    for (n = 0; n < 8; n++) {
        STDL_FreeSurface(STDL_SurfaceFromIndexed8(px, 192, 70, 192, key));
    }
    /* ticks of 5ms = 40000 cycles, over 8 frames of 13440 pixels */
    return (STDL_GetHz200() - t0) * 40000UL / (8UL * 192 * 70);
}

int main(void)
{
    unsigned long plain, keyed;
    int r;

    if (STDL_Init(0) < 0) {
        return 1;
    }
    r = ixchk_suite();
    plain = cost(-1);
    keyed = cost(3);
    STDL_Quit();
    printf("IX: SurfaceFromIndexed8 %lu cycles a pixel, keyed %lu\n",
           plain, keyed);
    printf("IX:done %s\n", r == 0 ? "PASS" : "FAIL");
    fflush(stdout);
    return r;
}
