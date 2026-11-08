/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * On-target check of the mouse's reach (event.c's clamp_mouse): on the
 * display's own height (STDL_RESPONSIVE_HEIGHT), 240 rows on a TT or a
 * Falcon and 200 on an ST, the mouse reaches the last row and no
 * further, and the last column. STDL_WarpMouse goes through the clamp
 * that the IKBD's motion does, so a warp to the bottom row and beyond
 * every edge is the test; it was 200 rows on every screen. It prints
 * after STDL_Quit, so a TT's console writes nothing into the screen.
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Iinclude -Ilib/xpad/src -o dist/MOUSERNG.TOS \
 *       tests/hatari/mouserng.c libstdl.a
 *   TOS="<TOS 3.06>" MACHINE=tt LATE="--mmu off" \
 *       EXTRA="--cpu-exact on --compatible on --memsize 4" \
 *       tests/hatari/run.sh mr dist/MOUSERNG.TOS 8 "waitfor MR:done"
 */
#include <stdio.h>
#include <stdl/stdl.h>
#include <stdl/stdl_responsive.h>
#include "../../examples/hold.h"

static void warp(int x, int y, int *gx, int *gy)
{
    STDL_Event ev;
    STDL_WarpMouse((uint16_t)x, (uint16_t)y);
    while (STDL_PollEvent(&ev)) {
    }
    STDL_GetMouseState(gx, gy);
}

int main(void)
{
    STDL_Surface *s;
    int rows, bx, by, fx, fy, mx, my, ok;

    hold_on_exit("MOUSERNG");
    if (STDL_Init(STDL_INIT_VIDEO) < 0) {
        printf("MR: STDL_Init failed\nMR:done\n");
        return 1;
    }
    STDL_UseResponsive(STDL_RESPONSIVE_COLOUR | STDL_RESPONSIVE_HEIGHT);
    s = STDL_SetVideoMode(320, 240, 8, STDL_DOUBLEBUF);
    rows = s ? s->h : 0;
    warp(160, rows - 1, &bx, &by);      /* the last row */
    warp(400, 1000, &fx, &fy);          /* past the right and the bottom */
    warp(17, 100, &mx, &my);            /* somewhere in the middle */
    STDL_Quit();
    ok = rows > 0 && bx == 160 && by == rows - 1 && fx == 319 &&
         fy == rows - 1 && mx == 17 && my == 100;
    printf("MR: %d rows; warped to the last row %d,%d; past the edges"
           " %d,%d; to 17,100 %d,%d\n", rows, bx, by, fx, fy, mx, my);
    printf("MR: %s\nMR:done\n", ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}
