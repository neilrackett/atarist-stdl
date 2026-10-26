/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * On-target check of STDL_TRIPLEBUF: the surface says what it got,
 * the pages go round three distinct addresses, the page STDL_Flip
 * hands back is never the one the video counter is fetching from, and
 * a frame longer than a VBL is no longer rounded up to whole VBLs.
 * Frames of 0, 6 and 9 ticks of the 200Hz clock (0, 1.5 and 2.25
 * VBLs at 50Hz) are flipped triple- and then double-buffered, the
 * counter sampled through each; the VBLs they took are printed beside
 * each other. Takes about 15s.
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Iinclude -Ilib/xpad/src -o dist/TRIPCHK.TOS \
 *       tests/hatari/tripchk.c libstdl.a
 *   TOS=$PWD/tmp/tos/etos256uk.img MACHINE=st EXTRA="--memsize 4" \
 *       tests/hatari/run.sh tc dist/TRIPCHK.TOS 8 "waitfor TC:done"
 */
#include <stdio.h>
#include <stdl/stdl.h>

#define FRCLOCK (*(volatile uint32_t *)0x466UL)
#define HZ200   (*(volatile uint32_t *)0x4BAUL)
#define VC_HI   (*(volatile uint8_t *)0xFFFF8205UL)
#define VC_MID  (*(volatile uint8_t *)0xFFFF8207UL)
#define VC_LO   (*(volatile uint8_t *)0xFFFF8209UL)
#define FRAMES  40

/* the address the Shifter is fetching, read until two reads agree */
static uint32_t vcounter(void)
{
    uint32_t a, b;

    do {
        a = (uint32_t)VC_HI << 16 | (uint32_t)VC_MID << 8 | VC_LO;
        b = (uint32_t)VC_HI << 16 | (uint32_t)VC_MID << 8 | VC_LO;
    } while (a != b);
    return a;
}

/* FRAMES frames of `ticks` each, the counter sampled every tick and
 * after each flip; the samples inside the page being drawn */
static int run(STDL_Surface *s, int ticks, uint32_t *vbls)
{
    uint32_t v0;
    int f, t, bad = 0;

    STDL_Flip();
    v0 = FRCLOCK;
    for (f = 0; f < FRAMES; f++) {
        for (t = 0; t <= ticks; t++) {
            const uint32_t c = vcounter(), p = (uint32_t)s->pixels;
            uint32_t h;

            if (c >= p && c < p + 32000UL) {
                bad++;
            }
            if (t < ticks) {
                h = HZ200;
                while (HZ200 == h) {
                }
            }
        }
        STDL_Flip();
    }
    *vbls = FRCLOCK - v0;
    return bad;
}

int main(int argc, char *argv[])
{
    static const int ticks[3] = { 0, 6, 9 };
    STDL_Surface *s;
    uint8_t *seen[6];
    uint32_t tv[3], dv[3];
    int i, k, bad = 0, fail = 0, distinct;

    (void)argc;
    (void)argv;
    s = STDL_SetVideoMode(320, 200, 4, STDL_TRIPLEBUF);
    if (s == NULL) {
        printf("TC: video: %s\nTC:done FAIL\n", STDL_GetError());
        return 1;
    }
    printf("TC: flags triple %d double %d\n",
           (s->flags & STDL_TRIPLEBUF) != 0, (s->flags & STDL_DOUBLEBUF) != 0);
    if (!(s->flags & STDL_TRIPLEBUF) || !(s->flags & STDL_DOUBLEBUF)) {
        fail = 1;
    }
    for (i = 0; i < 6; i++) {
        seen[i] = s->pixels;
        STDL_Flip();
    }
    distinct = seen[0] != seen[1] && seen[1] != seen[2]
               && seen[0] != seen[2];
    for (i = 3; i < 6; i++) {
        distinct = distinct && seen[i] == seen[i - 3];
    }
    printf("TC: pages %p %p %p, in turn: %s\n", (void *)seen[0],
           (void *)seen[1], (void *)seen[2], distinct ? "yes" : "NO");
    fail |= !distinct;

    for (k = 0; k < 3; k++) {
        bad += run(s, ticks[k], &tv[k]);
    }
    s = STDL_SetVideoMode(320, 200, 4, STDL_DOUBLEBUF);
    printf("TC: double again: flags triple %d\n",
           (s->flags & STDL_TRIPLEBUF) != 0);
    fail |= (s->flags & STDL_TRIPLEBUF) != 0;
    for (k = 0; k < 3; k++) {
        bad += run(s, ticks[k], &dv[k]);
    }
    for (k = 0; k < 3; k++) {
        printf("TC: %d frames of %d ticks: %lu VBLs triple, %lu double\n",
               FRAMES, ticks[k], (unsigned long)tv[k],
               (unsigned long)dv[k]);
    }
    /* at 50Hz: 0 ticks a VBL a frame either way, every frame shown;
     * 6 ticks 1.5 against 2; 9 ticks 2.25 against 3 */
    fail |= tv[0] + 2 < FRAMES || tv[0] > dv[0] + 2
            || tv[1] * 5 > dv[1] * 4 || tv[2] * 5 > dv[2] * 4;
    printf("TC: counter in the page drawn: %d samples\n", bad);
    fail |= bad != 0;
    printf("TC:done %s\n", fail ? "FAIL" : "PASS");
    fflush(stdout);
    STDL_Quit();
    return fail;
}
