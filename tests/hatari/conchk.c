/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Text printed to the console while a responsive screen is up
 * (stdl_responsive.h): the display's own height in 16 colours, or with
 * -DCONCOL the colour at 200 rows. It plays 300 frames of a block
 * moving over a filled screen and prints a line in the middle of them,
 * as a program might. On a TT the console writes the display page
 * itself, and the Flip after puts that page back whole, so a screenshot
 * a second after "CC:shot" must show the picture and no text. Then the
 * present's time a frame, typical and the most - the page put back:
 *
 *   CC:screen 320x240 bpp=4
 *   CC:text printed in play          (frame 100)
 *   CC:shot                          (frame 150)
 *   CC:present p50=0.00 max=0.00 ms (frame N)
 *   CC:done
 *
 * Build by hand (stems of eight characters, for Hatari's 8.3 drive):
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Iinclude -Ilib/xpad/src -o dist/CONCHK.TOS \
 *       tests/hatari/conchk.c libstdl.a
 *   ... -DCONCOL -o dist/CONCOL.TOS ...
 *   MACHINE=tt FF=off tests/hatari/run.sh cc dist/CONCHK.TOS 8 \
 *       "waitfor CC:shot;sleep 1;shot;waitfor CC:done"
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdl/stdl.h>
#include <stdl/stdl_bench.h>
#include "../../examples/hold.h"

#ifdef CONCOL
#define WHAT STDL_RESPONSIVE_COLOUR
#define BPP  8
#define ROWS 200
#else
#define WHAT STDL_RESPONSIVE_HEIGHT
#define BPP  4
#define ROWS 240
#endif
#define FRAMES 300

static int cmpu(const void *a, const void *b)
{
    const uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;

    return x < y ? -1 : x > y;
}

/* hundredths of a millisecond from STDL_GetHz38400 counts */
static unsigned long cms(uint32_t n)
{
    return (unsigned long)((unsigned long long)n * 100000ULL / 38400ULL);
}

int main(void)
{
    static uint32_t pt[FRAMES], sorted[FRAMES];
    const STDL_PresentStats *st;
    STDL_Surface *scr;
    uint32_t last;
    int f, worst = 0;

    hold_on_exit("CONCHK");

    if (STDL_Init(STDL_INIT_VIDEO) < 0) {
        return 1;
    }
    STDL_UseResponsive(WHAT);
    scr = STDL_SetVideoMode(320, ROWS, BPP, STDL_DOUBLEBUF);
    if (scr == NULL) {
        fprintf(stderr, "CC:no screen: %s\nCC:done\n", STDL_GetError());
        STDL_Quit();
        return 1;
    }
    fprintf(stderr, "CC:screen %dx%d bpp=%d\n", scr->w, scr->h,
            scr->format->BitsPerPixel);
    st = STDL_GetPresentStats();
    last = st->present_time;
    for (f = 0; f < FRAMES; f++) {
        STDL_Rect r;

        r.x = (int16_t)((f * 3) % 280);
        r.y = 60;
        r.w = 40;
        r.h = 40;
        STDL_FillRect(scr, NULL, 1);
        STDL_FillRect(scr, &r, 14);
        if (f == 100) {
            fprintf(stderr, "CC:text printed in play\n");
        }
        if (f == 150) {
            fprintf(stderr, "CC:shot\n");
        }
        STDL_Flip();
        pt[f] = st->present_time - last;
        last = st->present_time;
        if (pt[f] > pt[worst]) {
            worst = f;
        }
    }
    for (f = 0; f < FRAMES; f++) {
        sorted[f] = pt[f];
    }
    qsort(sorted, FRAMES, sizeof(sorted[0]), cmpu);
    STDL_Quit();
    fprintf(stderr, "CC:present p50=%lu.%02lu max=%lu.%02lu ms (frame %d)\n",
            cms(sorted[FRAMES / 2]) / 100, cms(sorted[FRAMES / 2]) % 100,
            cms(pt[worst]) / 100, cms(pt[worst]) % 100, worst);
    fprintf(stderr, "CC:done\n");
    return 0;
}
