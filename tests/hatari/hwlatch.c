/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * When does a hardware-scroll request reach the screen? Asked of the
 * hardware, not of STDL_ScrollWindowPending: after each request the
 * probe waits for the next VBL, lets the beam get into the picture,
 * and reads the Shifter's video counter. The window alternates
 * between rows 0 and 200 of a 400-row world, so the old and new
 * windows cannot overlap and the counter says unambiguously which
 * base the frame is being fetched from.
 *
 * Two passes: requests made just after a VBL ("early", which has
 * always landed on the next frame) and 15ms or more into a frame
 * ("late", which cost a frame more until the VBL learned to write the
 * counter itself). Each prints how many of its frames showed the new
 * window at the next VBL.
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Iinclude -Ilib/xpad/src -o dist/HWLATCH.TOS \
 *       tests/hatari/hwlatch.c libstdl.a
 *   TOS=$PWD/tmp/tos/etos256uk.img MACHINE=ste \
 *       tests/hatari/run.sh hl dist/HWLATCH.TOS 8 "waitfor HL:done"
 */
#include <stdio.h>
#include <stdl/stdl.h>

#define FRAMES 100
#define VC_HI  (*(volatile uint8_t *)0xFFFF8205UL)
#define VC_MID (*(volatile uint8_t *)0xFFFF8207UL)
#define VC_LO  (*(volatile uint8_t *)0xFFFF8209UL)

static uint32_t counter(void)
{
    return ((uint32_t)VC_HI << 16) | ((uint32_t)VC_MID << 8) | VC_LO;
}

static void wait_ticks(uint32_t n)
{
    uint32_t t0 = STDL_GetHz200();
    while (STDL_GetHz200() - t0 < n)
        ;
}

int main(void)
{
    STDL_Surface *world;
    int pass;

    STDL_Init(STDL_INIT_VIDEO);
    STDL_SetVideoMode(320, 200, 4, 0);
    if (!STDL_HasHwScroll()) {
        printf("HL: no STE Shifter\n");
        printf("HL:done\n");
        return 0;
    }
    world = STDL_CreateSurface(336, 400);
    if (world == NULL || STDL_SetScrollOrigin(world, 0, 0) < 0) {
        printf("HL: setup failed: %s\n", STDL_GetError());
        return 1;
    }
    for (pass = 0; pass < 2; pass++) {
        int f, hits = 0;

        for (f = 0; f < FRAMES; f++) {
            const int y = (f & 1) ? 200 : 0;
            const int x = 16 + (f & 15);
            const uint32_t base = (uint32_t)(uintptr_t)(world->pixels
                + (uint32_t)y * world->stride + ((x >> 1) & ~7));
            uint32_t c;

            STDL_WaitVBL();
            if (pass == 1) {
                wait_ticks(3);          /* 10-15ms: past the early path */
            }
            STDL_SetScrollOrigin(world, x, y);
            STDL_WaitVBL();
            wait_ticks(1);              /* into the picture */
            c = counter();
            if (c >= base && c < base + 200UL * world->stride) {
                hits++;
            }
        }
        printf("HL: %s requests on screen at the next VBL: %d/%d\n",
               pass ? "late " : "early", hits, FRAMES);
    }
    printf("HL:done\n");
    STDL_ResetScrollWindow();
    STDL_Quit();
    return 0;
}
