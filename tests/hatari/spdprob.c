/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Does the emulator actually change CPU speed when the Mega STE's
 * $FFFF8E21 is written? The library's bottom border caches a
 * timing calibration keyed on the requested mode, so the answer
 * decides whether Hatari can ever exercise that path - and the
 * assumption that it cannot was worth one wrong claim already.
 *
 * Times a fixed loop against the 200Hz counter at each of the four
 * STDL_UseMegaSteSpeedup settings and prints the ticks. On a plain
 * ST or STE all four read the same by construction.
 */
#include <stdio.h>
#include <stdl/stdl.h>

#define HZ200 (*(volatile uint32_t *)0x4BAL)

static uint32_t spin(void)
{
    volatile uint32_t sink = 0;
    uint32_t t0, i;

    t0 = HZ200;
    while (HZ200 == t0) {
        ;                       /* start on a tick boundary */
    }
    t0 = HZ200;
    for (i = 0; i < 300000UL; i++) {
        sink += i;
    }
    return HZ200 - t0;
}

int main(int argc, char *argv[])
{
    static const int mode[4] = { 0, 1, 2, 3 };
    uint32_t t[4];
    int i;

    (void)argc; (void)argv;
    if (STDL_Init(STDL_INIT_VIDEO) < 0) {
        return 1;
    }
    for (i = 0; i < 4; i++) {
        STDL_UseMegaSteSpeedup(mode[i]);
        t[i] = spin();
    }
    STDL_UseMegaSteSpeedup(1);
    STDL_Quit();
    for (i = 0; i < 4; i++) {
        printf("mode %d: %lu ticks\n", mode[i], (unsigned long)t[i]);
    }
    printf("done\n");
    fflush(stdout);
    return 0;
}
