/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The Mega STE speed and cache control, as a public call. Its own
 * translation unit because STDL_Init only needs the two lines that
 * set the register (they live in video.c, which every program
 * links); a program that never asks to change speed should not pay
 * for the API that changes it.
 *
 * Bit 0 of $FFFF8E21 is the cache and bit 1 the 16MHz clock, so 3
 * is both. The other bits of that byte belong to whoever set them -
 * Atari's control panel and the MSTE_CC utilities keep the high
 * ones set - so this reads, modifies and writes rather than
 * assigning a value.
 */
#include "stdl_internal.h"

int STDL_UseMegaSteSpeedup(int mode)
{
    int old = stdl_megaste_mode;

    if (mode >= 0 && mode <= 3) {
        stdl_megaste_mode = mode;
        /* After STDL_Init the change applies now; mode 0 puts back
         * what the machine had when the library claimed it, which
         * is what the terminate path would have restored. */
        if (stdl.initialised && stdl.mach.is_megaste) {
            if (mode == 0) {
                if (stdl.old_cpuspeed >= 0) {
                    STDL_MSTE_CTL = (uint8_t)stdl.old_cpuspeed;
                }
            } else {
                if (stdl.old_cpuspeed < 0) {
                    stdl.old_cpuspeed = STDL_MSTE_CTL;
                }
                stdl_megaste_apply(mode);
            }
        }
    }
    return old;
}
