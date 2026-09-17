/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The display's sync rate, as a public call. Its own translation
 * unit because nothing else needs it: there is no section garbage
 * collection on m68k-atari-mint, so a program that never asks to
 * change rate should not carry the code that changes it. The one
 * byte of state it moves (stdl_vbl_hz) lives in video.c, which
 * every program links, because the music tick divides by it.
 *
 * Bit 1 of $FFFF820A is the rate: set is 50Hz, clear is 60Hz. The
 * machine's own region decides what TOS set at boot, so a PAL
 * machine starts at 50 and an NTSC one at 60, and this is how a
 * port asks for the other.
 */
#include "stdl_internal.h"

int STDL_SetRefresh(int hz)
{
    if ((hz == 50 || hz == 60) && stdl.initialised) {
        int want = (hz == 50) ? 2 : 0;

        if (stdl.old_sync < 0) {
            /* first change: remember what to put back, which the
             * terminate path restores whether or not we get to
             * run any shutdown code of our own */
            stdl.old_sync = STDL_SYNC_REG & 3;
        }
        stdl_sync_want = want;
        if (stdl_shutdown_overscan == NULL) {
            STDL_SYNC_REG = (uint8_t)want;
            stdl_vbl_hz = (uint8_t)hz;
        }
        /* else a border is open and overscan owns the register -
         * its tricks are PAL-timed and it asserts 50Hz every frame
         * regardless. The request is recorded above and the final
         * close applies it. */
    }
    return stdl_vbl_hz;
}
