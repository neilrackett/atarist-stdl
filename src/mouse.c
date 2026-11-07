/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Whether the IKBD reports the mouse, as a public call. Its own
 * translation unit because event.c is linked by every program and
 * only the byte of state belongs there: a port that never asks
 * should not carry the code that asks.
 */
#include "stdl_internal.h"

int STDL_EnableMouse(int enable)
{
    const int was = stdl_mouse_on;

    if (enable >= 0) {
        stdl_mouse_on = (uint8_t)(enable != 0);
        /* The IKBD only while the library has the keyboard: the
         * install tells it then, and an exit puts the mouse back only
         * where the library took it - a $12 sent before would have
         * outlived the program. With two joysticks the port stays a
         * joystick; stdl_ikbd_mode says what is right for both. */
        if (stdl_events_installed) {
            stdl_ikbd_mode();
        }
    }
    return was;
}
