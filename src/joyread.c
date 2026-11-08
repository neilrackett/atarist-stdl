/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_GetJoystickAxis and STDL_GetJoystickHat: a joystick's six axes
 * and its hat as its events last carried them, for the program that
 * polls. An object of its own, as most programs read their sticks
 * through STDL_GetJoystickState and the events and never call these;
 * the SDL 1.2 shim and the SDL2 add-on read every joystick through
 * them rather than keeping copies built from the events.
 */

#include "stdl_internal.h"
#include "stdl_xpad.h"

/* joystick `which`'s pad record: stdl_pad0, or joysticks.c's */
static const stdl_pad_t *pad_of(int which)
{
    return which ? &stdl_joyn->pads[which] : &stdl_pad0;
}

int16_t STDL_GetJoystickAxis(int which, int axis)
{
    if ((unsigned)which >= stdl_joy_count
        || (unsigned)axis >= STDL_JOYSTICK_AXES) {
        return 0;
    }
    /*
     * X and Y have two sources, a port and a pad. Joystick 0's are
     * worked out by the helper its events use, so a poll and an event
     * cannot disagree; the others' are what their events last said.
     */
    if (axis < 2) {
        return which ? stdl_joyn->joy[which].axis[axis]
                     : stdl_xpad_axis_merged(&stdl_pad0, axis,
                                             stdl_joy_ikbd());
    }
    /* the rest exist only on a pad */
    return STDL_HasPad(which) ? stdl_xpad_axis(pad_of(which), axis) : 0;
}

uint8_t STDL_GetJoystickHat(int which)
{
    /* the pad's d-pad; a joystick without a pad has its record at
     * rest, and an ST joystick no hat, its directions being the axes */
    return (unsigned)which < stdl_joy_count ? stdl_xpad_hat(pad_of(which))
                                            : 0;
}
