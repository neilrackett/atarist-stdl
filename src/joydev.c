/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_UseJoystickDeviceEvents: STDL_JOYDEVICEADDED and REMOVED, for
 * the program that asks. joysticks.c and joymerge.c push them as they
 * re-map (stdl_joy_device), straight after the releases; this turns
 * them on, and says what is there when it does. An object of its own:
 * the SDL2 add-on asks at its first pump, and a program that never
 * asks links none of it.
 */

#include "stdl_internal.h"

int STDL_UseJoystickDeviceEvents(int on)
{
    const int was = stdl_joy_devices;
    int i;

    if (on >= 0 && (on != 0) != was) {
        stdl_joy_devices = (uint8_t)(on != 0);
        /* every joystick there is now, as SDL2 says at start-up */
        for (i = 0; on && i < stdl_joy_count; i++) {
            stdl_joy_device(STDL_JOYDEVICEADDED, i);
        }
    }
    return was;
}
