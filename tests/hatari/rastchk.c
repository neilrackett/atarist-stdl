/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The raster service on the machine.
 *
 *  - What it costs: an idle loop counted over 100 frames with
 *    rasters off, then every 4, 2 and 1 lines, one register and two.
 *  - Timer B has one owner: a border refuses to open over rasters,
 *    rasters refuse to open over a border, and each opens once the
 *    other has closed.
 *  - Then it holds a fixed table - stripes in colour 0 every ten
 *    lines - for a few seconds, for an AVI capture to check every
 *    frame is the same; and with `abort` as its argument
 *    (RASTCHK.TTP) it leaves through abort() with them open, and the
 *    desktop coming back in its own colours is the pass.
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Iinclude -Ilib/xpad/src \
 *       -o dist/RASTCHK.TOS tests/hatari/rastchk.c libstdl.a
 *   TOS=$PWD/tmp/tos/etos256uk.img MACHINE=st \
 *       tests/hatari/run.sh rc dist/RASTCHK.TOS 8 "waitfor RC:done"
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdl/stdl.h>

static volatile uint32_t vbls;
static void count_vbl(void) { vbls++; }

/* idle-loop iterations over 100 frames */
static uint32_t idle(void)
{
    uint32_t n = 0, end;

    STDL_WaitVBL();
    end = vbls + 100;
    while (vbls < end) {
        n++;
    }
    return n;
}

static void stripes(int lines)
{
    int n = STDL_OpenRasters(lines, 0, -1), i;
    uint16_t *t = STDL_RasterTable();

    for (i = 0; i < n; i++) {
        t[i] = (i * lines / 10) & 1 ? STDL_HW444(0xF80) : STDL_HW444(0x028);
    }
    STDL_ShowRasters();
}

int main(int argc, char *argv[])
{
    static const int steps[3] = { 4, 2, 1 };
    uint32_t base, one[3], two[3];
    int k, fails = 0, ok1, ok2, ok3, ok4;

    if (STDL_SetVideoMode(320, 200, 4, 0) == NULL
        || STDL_AddVBL(count_vbl) < 0) {
        return 1;
    }
    STDL_FillRect(STDL_GetVideoSurface(), NULL, 0);
    if (argc > 1 && strcmp(argv[1], "abort") == 0) {
        stripes(10);
        STDL_Delay(1000);
        abort();
    }

    base = idle();
    for (k = 0; k < 3; k++) {
        STDL_OpenRasters(steps[k], 0, -1);
        one[k] = idle();
        STDL_OpenRasters(steps[k], 0, 15);
        two[k] = idle();
    }
    STDL_CloseRasters();

    /* one owner for Timer B */
    STDL_OpenRasters(2, 0, -1);
    ok1 = STDL_OpenBottomBorder() == 0;     /* refused */
    STDL_CloseRasters();
    ok2 = STDL_OpenBottomBorder() != 0;     /* now opens */
    ok3 = STDL_OpenRasters(2, 0, -1) < 0;   /* refused */
    STDL_CloseBottomBorder();
    ok4 = STDL_OpenRasters(2, 0, -1) > 0;   /* now opens */
    STDL_CloseRasters();
    fails = !ok1 + !ok2 + !ok3 + !ok4;

    /* a fixed table, held for the capture */
    stripes(10);
    printf("RC:hold\n");
    fflush(stdout);
    STDL_Delay(3000);
    STDL_CloseRasters();
    STDL_Quit();

    printf("RC: idle %lu; cost a frame with one register / two:\n",
           (unsigned long)base);
    for (k = 0; k < 3; k++) {
        printf("RC:  every %d lines: %lu%% / %lu%%\n", steps[k],
               (unsigned long)((base - one[k]) * 100 / base),
               (unsigned long)((base - two[k]) * 100 / base));
    }
    printf("RC: border over rasters refused %s, opens after %s\n",
           ok1 ? "yes" : "NO", ok2 ? "yes" : "NO");
    printf("RC: rasters over a border refused %s, open after %s\n",
           ok3 ? "yes" : "NO", ok4 ? "yes" : "NO");
    printf("RC:done %s\n", fails ? "FAIL" : "PASS");
    fflush(stdout);
    return fails;
}
