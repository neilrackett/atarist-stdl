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
        if (stdl.initialised) {
            /* $12 disables mouse reporting, $08 puts it back in
             * relative mode - the state TOS left it in. Safe to
             * send at any time; the IKBD queues commands. */
            stdl_ikbd_send((uint8_t)(stdl_mouse_on ? 0x08 : 0x12));
        }
    }
    return was;
}
