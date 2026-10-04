/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The host STDL_SurfaceFromIndexed8 tests, run on the machine, then
 * the load-time conversions timed: the chunky conversion, and the
 * sixteen variants STDL_PRESHIFT builds for a 64x64 sprite.
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

/* milliseconds to build a pre-shifted 64x64 sprite */
static unsigned long preshift_ms(void)
{
    STDL_Surface *art = STDL_CreateSurface(64, 64);
    STDL_Rect r = { 8, 8, 40, 40 };
    uint32_t t0;
    int n;

    if (art == NULL) {
        return 0;
    }
    STDL_FillRect(art, NULL, 0);
    STDL_FillRect(art, &r, 9);
    STDL_SetColourKey(art, 1, 0);
    t0 = STDL_GetHz200();
    for (n = 0; n < 4; n++) {
        STDL_FreeSprite(STDL_SpriteFromSurface(art, 64, STDL_PRESHIFT));
    }
    STDL_FreeSurface(art);
    return (STDL_GetHz200() - t0) * 5UL / 4;
}

int main(void)
{
    unsigned long plain, keyed, pre;
    int r;

    if (STDL_Init(0) < 0) {
        return 1;
    }
    r = ixchk_suite();
    plain = cost(-1);
    keyed = cost(3);
    pre = preshift_ms();
    STDL_Quit();
    printf("IX: SurfaceFromIndexed8 %lu cycles a pixel, keyed %lu\n",
           plain, keyed);
    printf("IX: STDL_PRESHIFT 64x64 %lums\n", pre);
    printf("IX:done %s\n", r == 0 ? "PASS" : "FAIL");
    fflush(stdout);
    return r;
}
