/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The sync rate a program leaves behind. STDL_Init zeroed the
 * library's state, and the exit path took a zero "rate to put back"
 * as 60Hz: every program that inited without a video mode - test
 * probes, tools - left a 50Hz machine at 60. A Mega STE's monitor
 * showed it; nothing in the emulator was looking. Three exits, each
 * against the rate before it: init and quit alone, a video mode, and
 * STDL_SetRefresh to the other rate.
 *
 * The exit path is video.c's, which tests/host does not link, so
 * this is its check.
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Iinclude -Ilib/xpad/src \
 *       -o dist/SYNCCHK.TOS tests/hatari/syncchk.c libstdl.a
 *   MACHINE=st tests/hatari/run.sh sy dist/SYNCCHK.TOS 8 "waitfor SY:done"
 */
#include <stdio.h>
#include <mint/osbind.h>
#include <stdl/stdl.h>

static unsigned char sync_now;

static long read_sync(void)
{
    sync_now = *(volatile unsigned char *)0xFFFF820AUL & 3;
    return 0;
}

static int rate(void)
{
    Supexec(read_sync);
    return sync_now;
}

int main(void)
{
    int before, after[3], fails = 0, k;

    before = rate();
    STDL_Init(0);
    STDL_Quit();
    after[0] = rate();

    STDL_SetVideoMode(320, 200, 4, 0);
    STDL_Quit();
    after[1] = rate();

    STDL_SetVideoMode(320, 200, 4, 0);
    STDL_SetRefresh(before & 2 ? 60 : 50);
    STDL_Quit();
    after[2] = rate();

    for (k = 0; k < 3; k++) {
        fails += after[k] != before;
    }
    printf("SY: sync %d before; after init and quit %d, a video mode %d, "
           "a rate change %d\n", before, after[0], after[1], after[2]);
    printf("SY:done %s\n", fails ? "FAIL" : "PASS");
    fflush(stdout);
    return fails;
}
