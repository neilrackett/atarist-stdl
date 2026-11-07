/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * More than one joystick: STDL_SetNumJoysticks, and the work behind
 * joysticks 1 to 5.
 *
 * Joystick 0 stays event.c's - the joystick port, and pad 0 while the
 * pads are merged - exactly as a program that never asks has it. This
 * adds the mouse port as joystick 1, whose $FE packets event.c's
 * parser hands over through stdl_joyn, and an Xpad provider's pads,
 * read at a pump with room for what they could say, through the code
 * event.c reads pad 0 with (stdl_xpad.c, each joystick its own
 * record). Each joystick's sources
 * are fixed by the merge setting (STDL_UseJoystickMerge): merged,
 * port 1 and pad 0, port 0 and pad 1, pad 2, pad 3; apart, port 1,
 * port 0, pads 0 to 3. So a player keeps their number when another's
 * pad comes or goes.
 *
 * Its own object, so that a program with one joystick pays nothing
 * for six. The readers, STDL_GetJoystickState and the rest, are
 * event.c's and stdl_xpad.c's, and reach this through stdl_joyn.
 */

#include <string.h>

#include "stdl_internal.h"
#include "stdl_xpad.h"

#define IKBD_FIRE 0x80

/* Each joystick's pad record ([0] is joystick 0's, stdl_pad0, never
 * used here), and what was last reported of the five a stick has. */
static stdl_pad_t pads[STDL_MAX_JOYSTICKS];
static stdl_joy_t joy[STDL_MAX_JOYSTICKS];
static uint8_t port0;       /* the mouse port's byte: joystick 1's    */
static uint8_t slots;       /* the provider's pad slots, 0 without one */
static uint8_t asked = 1;   /* what STDL_SetNumJoysticks was asked    */

/*
 * Joystick i's state from its port and its pad, and the events for
 * what moved of the five a stick has: axes 0 and 1 and fire, in
 * joystick 0's order (event.c's handle_joy). The pad's other inputs
 * are stdl_xpad_events', from a reading of the pad.
 */
static void report(int i)
{
    const stdl_pad_t *p = &pads[i];
    stdl_joy_t *j = &joy[i];
    const uint8_t port = (i == 1) ? port0 : 0;
    const uint8_t state = (uint8_t)(port | stdl_xpad_classic(p));
    int a;

    for (a = 0; a < 2; a++) {
        const int16_t v = stdl_xpad_axis_merged(p, a, port);

        if (v != j->axis[a]) {
            j->axis[a] = v;
            stdl_joy_event(STDL_JOYAXISMOTION, i, a, v);
        }
    }
    if ((state ^ j->state) & IKBD_FIRE) {
        stdl_joy_event((state & IKBD_FIRE) ? STDL_JOYBUTTONDOWN
                                           : STDL_JOYBUTTONUP,
                       i, 0, (state & IKBD_FIRE) != 0);
    }
    j->state = state;
}

static void joyn_stop(void)
{
    int i;

    for (i = 1; i < STDL_MAX_JOYSTICKS; i++) {
        stdl_xpad_rumble_stop(&pads[i]);
    }
}

static int joyn_read(int which, int what)
{
    const stdl_joy_t *j = &joy[which];

    if (what == STDL_JOYN_STATE) {
        return j->state;
    }
    if (what == STDL_JOYN_PAD) {
        return pads[which].slot >= 0;
    }
    if (what < 0 || what >= STDL_JOYSTICK_INPUTS) {
        return 0;
    }
    /* the directions and fire are the state byte's, in
     * STDL_JOYSTICK_UP's order; the rest the pad's own */
    if (what < STDL_JOYSTICK_FIRE) {
        return (j->state >> what) & 1;
    }
    if (what == STDL_JOYSTICK_FIRE) {
        return j->state >> 7;
    }
    return stdl_xpad_button(&pads[which], what - STDL_JOYSTICK_FIRE);
}

static void joyn_port0(uint8_t state)
{
    /* $FD answers every $16, with joystick 1 asked for or not */
    if (stdl_joy_count > 1) {
        port0 = state;
        report(1);
    }
}

static void joyn_poll(uint32_t now)
{
    int i;

    for (i = 1; i < stdl_joy_count; i++) {
        stdl_pad_t *p = &pads[i];

        stdl_xpad_rumble_tick(p, now);
        /* as joystick 0's pad (event.c): only with room for all it
         * could say, and only when it has something to say */
        if (stdl_event_room() >= STDL_XPAD_EVENTS && stdl_xpad_read(p)) {
            report(i);
            stdl_xpad_events(p, i);
        }
    }
}

/*
 * Joystick i's pad slot (it is 1 or more; joystick 0's is stdl_pad0's),
 * or -1. Merged (STDL_UseJoystickMerge), pad i, beside the mouse port
 * on joystick 1; apart, the pads follow the two ports, pad 0 on
 * joystick 2. Only slots the provider has.
 */
static int slot_for(int i)
{
    const int slot = (stdl_pad0.slot < 0) ? i - 2 : i;

    return (slot >= 0 && slot < slots) ? slot : -1;
}

/* How many there are for `asked`: one, or the two ports and as many
 * pads as there are slots for, merged four at most and apart six. */
static int count_for(int n)
{
    int most;

    if (n < 2) {
        return 1;
    }
    most = (stdl_pad0.slot < 0) ? 2 + slots : (slots > 2 ? slots : 2);
    return n < most ? n : most;
}

static const stdl_joyn_ops ops;

/*
 * The joysticks for `asked` under the merge setting, at once. One
 * going, or whose pad changes hands, first lets go of what it held -
 * the events a stick let go would send - and its rumble ends; one
 * coming, or taking a new pad, starts that pad at rest, as joystick 0
 * does. Joystick 1's port stays its own across a change of pad. Each
 * says so with device events where they are asked for: REMOVED after
 * its releases, ADDED for one there now.
 */
static int joyn_remap(void)
{
    const int was = stdl_joy_count;
    const int now = count_for(asked);
    int i;

    /* joystick 1 going lets go of its port too; one whose pad changes
     * hands keeps it. Here rather than in the loop, which gcc then
     * peeled for joystick 1: 80 bytes more. */
    if (now < 2) {
        port0 = 0;
    }
    for (i = 1; i < STDL_MAX_JOYSTICKS; i++) {
        stdl_pad_t *p = &pads[i];
        const int slot = i < now ? slot_for(i) : -1;

        if (i < was) {
            if (i < now && slot == p->slot) {
                continue;               /* as it was */
            }
            stdl_xpad_rumble_stop(p);
            memset(&p->pad, 0, sizeof(p->pad));
            report(i);
            stdl_xpad_events(p, i);
            p->slot = (int8_t)slot;
            p->have_prev = 0;
            /* gone, or standing for other sources: after its releases */
            stdl_joy_device(STDL_JOYDEVICEREMOVED, i);
        } else if (i < now) {
            memset(p, 0, sizeof(*p));
            memset(&joy[i], 0, sizeof(joy[i]));
            p->slot = (int8_t)slot;
        } else {
            continue;
        }
        if (i < now) {
            stdl_joy_device(STDL_JOYDEVICEADDED, i);
        }
    }
    stdl_joyn = &ops;
    stdl_joy_count = (uint8_t)now;

    /* The IKBD, if the library has the keyboard; otherwise
     * stdl_events_install says it when it takes it. Then $16, whose
     * $FD reply seeds both ports' state. No quiesce, as the vector
     * stays where it is. */
    if (stdl_events_installed && (was > 1) != (now > 1)) {
        stdl_ikbd_mode();
        stdl_ikbd_send(0x16);
    }
    return now;
}

static const stdl_joyn_ops ops = {
    joyn_port0, joyn_poll, joyn_stop, joyn_read, joyn_remap, pads, joy
};

int STDL_SetNumJoysticks(int n)
{
    if (n < 1 || n > STDL_MAX_JOYSTICKS) {
        STDL_SetError("STDL_SetNumJoysticks: 1 to 6 joysticks");
        return -1;
    }
    /* A provider is in the jar before the program starts and stays
     * for the session (xpad.h); stdl_xpad.c looks once. */
    slots = stdl_xpad_present() ? stdl_xpad->pad_count : 0;
    asked = (uint8_t)n;
    return joyn_remap();
}
