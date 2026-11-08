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
 *
 * One implementation for every joystick's pad: each has a record
 * (stdl_pad_t), joystick 0's here and the others' in joysticks.c, and
 * the reading, the events and the rumble below take the record.
 */

#ifndef STDL_XPAD_H
#define STDL_XPAD_H

#include <stdint.h>

#include "xpad.h"

/* SDL button indices. Zero stays the fire button so a port that
 * hardcodes "button 0" keeps working when a pad appears. */
#define STDL_XPAD_BUTTONS 13
#define STDL_XPAD_AXES 6

/* The most events one pad's poll pushes: its six axes, its thirteen
 * buttons and its hat. A pad is read only when the queue has room for
 * them, so none is lost: they come a pump late instead. */
#define STDL_XPAD_EVENTS (STDL_XPAD_AXES + STDL_XPAD_BUTTONS + 1)

/* A joystick's pad: what it read, what it last reported, and its
 * rumble. Joystick 0's is stdl_pad0; joysticks.c keeps the others'. */
typedef struct stdl_pad {
    XPAD_PAD  pad;          /* the last reading                       */
    XPAD_PAD  prev;         /* as last reported                       */
    int8_t    slot;         /* the provider's slot, or -1 for none    */
    uint8_t   have_prev;    /* reported since it came: until then its
                               reading is no news                     */
    XPAD_REQ *rumble_req;   /* where a running pulse was written, or 0 */
    uint32_t  rumble_until;
} stdl_pad_t;

/* Joystick 0's. Its slot is 0, or -1 while the pads are kept apart
 * from the ports (STDL_UseJoystickMerge(0)), which is how the rest of
 * the library tells. */
extern stdl_pad_t stdl_pad0;

/* The provider's block, or 0; valid once stdl_xpad_present() has
 * looked. */
extern const XPAD *stdl_xpad;

/* Look for a provider, and start joystick 0's pad afresh. Called from
 * the event install. */
void stdl_xpad_open(void);

/* Non-zero when a provider was found and is readable. */
int stdl_xpad_present(void);

/*
 * Read p's slot into p->pad. Non-zero when there is something to
 * report: it differs from what was last reported. Zero for a record
 * without a slot, which let go as it lost it.
 */
int stdl_xpad_read(stdl_pad_t *p);

/* The classic five in IKBD shape (bit 0 up, 1 down, 2 left, 3 right,
 * 7 fire), to OR with a port's byte. */
uint8_t stdl_xpad_classic(const stdl_pad_t *p);

/* SDL-scaled reads, for the polling half of the SDL joystick API.
 * axis 0/1 left stick, 2/3 right stick, 4/5 triggers. */
int16_t stdl_xpad_axis(const stdl_pad_t *p, int axis);
int stdl_xpad_button(const stdl_pad_t *p, int button);
uint8_t stdl_xpad_hat(const stdl_pad_t *p);

/*
 * The value to report for axis 0 or 1, given the port's own byte.
 * Whichever source is pushed further wins.
 *
 * `ikbd` must be the port byte alone, never the joystick's state: that
 * one already has the pad folded into it, so passing it would have the
 * pad competing with itself, and its digital rendering would beat its
 * own analogue reading every time.
 */
int16_t stdl_xpad_axis_merged(const stdl_pad_t *p, int axis, uint8_t ikbd);

/* Events, as joystick `which`, for everything the classic five cannot
 * carry - the extra buttons, the second stick, the triggers and the
 * hat - and p's reading becomes what was reported. Nothing on a pad's
 * first reading. */
void stdl_xpad_events(stdl_pad_t *p, int which);

/* A record's rumble (STDL_PadRumble starts it): its end once its time
 * has passed, which the pump calls, and its end now. The stop is
 * memory writes only, so the terminate path may call it. A record's
 * slot changes only after its rumble is stopped. */
void stdl_xpad_rumble_tick(stdl_pad_t *p, uint32_t now);
void stdl_xpad_rumble_stop(stdl_pad_t *p);

/* The joystick port's byte without the pad merged in. event.c owns it. */
uint8_t stdl_joy_ikbd(void);

#endif /* STDL_XPAD_H */
