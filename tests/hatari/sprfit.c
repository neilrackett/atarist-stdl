/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * On-target sweep for refitting the sprite thresholds (STDL_SPR_* in
 * stdl_internal.h): each sprite timed on the CPU and on the BLiTTER
 * forced, 16-96 wide by 8-64 rows, with solid, disc and ring art (the
 * CPU skips transparent groups and stores opaque ones, so its cost
 * depends on the art), aligned at phase 0, pre-shifted at phase 5,
 * and unshifted at phase 5. Lines read "SF: art w h kind cpu blt" in
 * microseconds. It links the counted build of the pixel paths, the
 * only one where stdl_blit_force exists; score a candidate rule by
 * the geometric mean over every case of its choice against the
 * better of the two. Each case runs at least 0.2s: about two minutes
 * under the emulator's fast-forward, longer on a machine.
 *
 *   STCMD_NO_TTY=1 stcmd make obj/stats/src/blit.o \
 *       obj/stats/src/sprite.o obj/stats/src/draw.o obj/stats/src/blitter.o
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Iinclude -Ilib/xpad/src -DSTDL_BLIT_STATS -o dist/SPRFIT.TOS \
 *       tests/hatari/sprfit.c obj/stats/src/blit.o obj/stats/src/sprite.o \
 *       obj/stats/src/draw.o obj/stats/src/blitter.o libstdl.a
 *   TOS=$PWD/tmp/tos/etos256uk.img MACHINE=ste EXTRA="--memsize 4" \
 *       tests/hatari/run.sh sf dist/SPRFIT.TOS 8 "waitfor SF:done"
 */
#include <stdio.h>
#include <stdl/stdl.h>
#include "../../examples/hold.h"

extern int stdl_blit_force;

static STDL_Surface *dst;

static uint32_t timeit(STDL_Sprite *spr, int x)
{
    uint32_t n = 4, t;
    for (;;) {
        uint32_t i, t0 = STDL_GetTicks();
        for (i = 0; i < n; i++) {
            STDL_BlitSprite(spr, 0, dst, x, 40);
        }
        t = STDL_GetTicks() - t0;
        if (t >= 200) {                 /* STDL_GetTicks: ms */
            break;
        }
        n <<= 1;
    }
    return (uint32_t)((t * 1000UL) / n);   /* us per op */
}

static STDL_Surface *art(int w, int h, int kind)
{
    STDL_Surface *s = STDL_CreateSurface(w, h);
    int x, y;
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            /* an ellipse in units of the half-axes, squared, x 64 */
            long dx = (2L * x + 1 - w) * 64 / w, dy = (2L * y + 1 - h) * 64 / h;
            long r = dx * dx + dy * dy;     /* 0 .. 8192 */
            int in = kind == 0 ? 1 : kind == 1 ? r <= 4096
                   : (r <= 4096 && r >= 1600);
            STDL_PutPixel(s, x, y, (uint8_t)(in ? 1 + (x + y) % 15 : 0));
        }
    }
    STDL_SetColourKey(s, 1, 0);
    return s;
}

int main(void)
{
    static const int ws[] = { 16, 32, 48, 64, 96 };
    static const int hs[] = { 8, 16, 32, 64 };
    static const char *const kn[] = { "solid", "disc", "ring" };
    int wi, hi, k, v;

    hold_on_exit("SPRFIT");

    STDL_Init(STDL_INIT_VIDEO);
    STDL_SetVideoMode(320, 200, 4, 0);
    dst = STDL_CreateSurface(320, 200);
    STDL_FillRect(dst, NULL, 6);
    printf("SF: kind w h variant cpu blt\n");
    fflush(stdout);
    for (k = 0; k < 3; k++)
    for (hi = 0; hi < 4; hi++) for (wi = 0; wi < 5; wi++) {
        STDL_Surface *s = art(ws[wi], hs[hi], k);
        STDL_Sprite *pl = STDL_SpriteFromSurface(s, ws[wi], 0);
        STDL_Sprite *pr = STDL_SpriteFromSurface(s, ws[wi], STDL_PRESHIFT);
        for (v = 0; v < 3; v++) {
            STDL_Sprite *spr = v == 1 ? pr : pl;
            int x = v == 0 ? 64 : 69;
            uint32_t c, b;
            if (spr == NULL) {
                continue;
            }
            STDL_UseBlitter(0); stdl_blit_force = 0;
            c = timeit(spr, x);
            STDL_UseBlitter(1); stdl_blit_force = 1;
            b = timeit(spr, x);
            stdl_blit_force = 0;
            printf("SF: %s %d %d %s %lu %lu\n", kn[k], ws[wi], hs[hi],
                   v == 0 ? "ph0" : v == 1 ? "pre" : "shift",
                   (unsigned long)c, (unsigned long)b);
            fflush(stdout);
        }
        STDL_FreeSprite(pl);
        STDL_FreeSprite(pr);
        STDL_FreeSurface(s);
    }
    printf("SF:done\n");
    fflush(stdout);
    STDL_Quit();
    return 0;
}
