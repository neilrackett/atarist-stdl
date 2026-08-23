/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Xpad consumer: a modern controller, when a provider is present.
 *
 * Internal to the library. Games reach this through the SDL joystick
 * API in compat.c, which is the point: a port gains sticks, triggers
 * and a full button set without knowing Xpad exists.
 */

#ifndef STDL_XPAD_H
#define STDL_XPAD_H

#include <stdint.h>

/* SDL button indices. Zero stays the fire button so a port that
 * hardcodes "button 0" keeps working when a pad appears. */
#define STDL_XPAD_BUTTONS 13
#define STDL_XPAD_AXES 6

/* Look for a provider. Called once, from the event install. */
void stdl_xpad_open(void);

/* Non-zero when a provider was found and is readable. */
int stdl_xpad_present(void);

/*
 * Refresh the cached snapshot and return the classic five inputs in
 * IKBD shape: bit0 up, bit1 down, bit2 left, bit3 right, bit7 fire.
 * The caller ORs that with the real joystick, so a stick in port 1
 * and a pad both work, and either alone works.
 */
uint8_t stdl_xpad_poll(void);

/* Emit events for everything the classic five cannot carry: the extra
 * buttons, the second stick, the triggers and the hat. */
void stdl_xpad_events(void);

/* SDL-scaled reads, for the polling half of the SDL joystick API.
 * axis 0/1 left stick, 2/3 right stick, 4/5 triggers. */
int16_t stdl_xpad_axis(int axis);
int stdl_xpad_button(int button);
uint8_t stdl_xpad_hat(void);

/*
 * The value to report for axis 0 or 1: the pad's analogue reading when
 * it is off centre, otherwise full deflection from the digital bits,
 * otherwise nothing. neg and pos are the IKBD bits for that axis.
 */
int16_t stdl_xpad_axis_merged(int axis, uint8_t state, uint8_t neg,
                              uint8_t pos);

#endif /* STDL_XPAD_H */
