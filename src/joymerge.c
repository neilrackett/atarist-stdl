/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_UseJoystickMerge: whether a joystick's port and the Xpad pad of
 * its number are one joystick (1, the default) or the pads joysticks
 * of their own after the two ports (0).
 *
 * An object of its own, apart from joysticks.c: a program with one
 * joystick that wants the port alone, pad 0 not merged into it, needs
 * none of the rest.
 */

#include <string.h>

#include "stdl_internal.h"
#include "stdl_xpad.h"

int STDL_UseJoystickMerge(int on)
{
    /* the setting is joystick 0's pad slot: 0 merged, -1 apart */
    const int was = stdl_pad0.slot >= 0;

    if (on >= 0 && (on != 0) != was) {
        /* its rumble ends before its slot moves */
        stdl_xpad_rumble_stop(&stdl_pad0);
        if (on) {
            /* Pad 0 joins joystick 0 as at the start: let go of when it
             * left, its first reading is no news but for the five a
             * stick has. */
            stdl_pad0.slot = 0;
        } else {
            /* Pad 0 leaves, and joystick 0 lets go of what it held now,
             * as joysticks 1-5 let go of theirs (joysticks.c), so that
             * its releases come before its device events. */
            stdl_pad0.slot = -1;
            memset(&stdl_pad0.pad, 0, sizeof(stdl_pad0.pad));
            stdl_pad0_report(STDL_GetTicks());
        }
        /* joystick 0 stands for other sources */
        stdl_joy_device(STDL_JOYDEVICEREMOVED, 0);
        stdl_joy_device(STDL_JOYDEVICEADDED, 0);
        /* joysticks 1-5, at once, where there are any */
        if (stdl_joyn != NULL) {
            stdl_joyn->remap();
        }
    }
    return was;
}
