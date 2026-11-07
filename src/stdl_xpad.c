/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Xpad consumer.
 *
 * Xpad (github.com/neilrackett/atarist-xpad) is a shared state block
 * published through the cookie jar by whatever owns the controller: a
 * cartridge-port device, a MIDI adapter, a driver synthesising one from
 * the IKBD. Reading it needs no vector and no hook, which matters here
 * more than usual: STDL replaces ikbdsys with its own packet parser, so
 * TOS never dispatches joyvec and a provider that injects there cannot
 * reach us. A provider that publishes a block can.
 *
 * Its own file because there is no section garbage collection on
 * m68k-atari-mint: the linker's granularity is the object, so anything
 * folded into event.c would cost every program that opens a screen.
 *
 * Every joystick's pad goes through here, with its record
 * (stdl_pad_t): joystick 0's, stdl_pad0, and joysticks.c's for the
 * others. The reading, the events and the rumble were once written
 * twice, here and in joysticks.c, and a test of the same pad in two
 * slots held the two together; now there is one of each.
 *
 * The library is in supervisor mode throughout, but xpad_find() goes
 * through Supexec, which is safe from either mode, so this does not
 * need to care.
 */

#include "stdl_xpad.h"

#include <string.h>

#include "stdl_internal.h"

/* Classic five, in IKBD shape, so the caller can OR it straight into
 * the byte the packet parser produces. */
#define IKBD_UP 0x01
#define IKBD_DOWN 0x02
#define IKBD_LEFT 0x04
#define IKBD_RIGHT 0x08
#define IKBD_FIRE 0x80

const XPAD *stdl_xpad;
static int opened;        /* looked for a provider yet */
stdl_pad_t stdl_pad0;     /* joystick 0's: slot 0 unless apart */

/*
 * SDL button order. Zero is the south face button, so a port that only
 * knows about fire keeps working; the rest follow the bitmask order so
 * the mapping is guessable from xpad.h rather than arbitrary.
 *
 * The d-pad is deliberately absent: it is the hat, and reporting it
 * twice would make "any button" checks fire on a direction.
 */
static const uint32_t button_bits[STDL_XPAD_BUTTONS] = {
    XPAD_SOUTH, XPAD_EAST, XPAD_WEST, XPAD_NORTH,
    XPAD_TL, XPAD_TR, XPAD_TL2, XPAD_TR2,
    XPAD_SELECT, XPAD_START, XPAD_MODE,
    XPAD_THUMBL, XPAD_THUMBR
};

void stdl_xpad_open(void)
{
    stdl_xpad = xpad_find();
    opened = 1;

    /* its slot and its rumble stay: the merge setting, and a pulse
     * the exit has still to end */
    memset(&stdl_pad0.pad, 0, sizeof(stdl_pad0.pad));
    memset(&stdl_pad0.prev, 0, sizeof(stdl_pad0.prev));
    stdl_pad0.have_prev = 0;
}

/*
 * Opens on first ask rather than waiting to be told. A game is entitled
 * to count joysticks before it sets a video mode, and the video mode is
 * what installs the event handler: without this, SDL_JoystickNumAxes()
 * would answer 2 before the mode was set and 6 after.
 */
int stdl_xpad_present(void)
{
    if (!opened) {
        stdl_xpad_open();
    }
    return stdl_xpad != 0;
}

/* A pad's twelve bytes as three longs, for the compare a pump makes of
 * every pad: may_alias, as they are an XPAD_PAD's, and aligned to two,
 * as a 68000's longs are. */
typedef uint32_t pad_long __attribute__((__may_alias__, aligned(2)));
typedef char pad_is_12[sizeof(XPAD_PAD) == 12 ? 1 : -1];

static int same(const XPAD_PAD *a, const XPAD_PAD *b)
{
    const pad_long *x = (const pad_long *)a;
    const pad_long *y = (const pad_long *)b;

    return x[0] == y[0] && x[1] == y[1] && x[2] == y[2];
}

/*
 * Most pumps find a pad as it was, and the reports would find nothing
 * to say: three compares, and the pump skips them. A pad without a
 * slot (joystick 0's while the pads are apart) is not read at all: it
 * let go of what it held as it lost the slot (joymerge.c, joysticks.c).
 * One that cannot be read is at rest.
 */
int stdl_xpad_read(stdl_pad_t *p)
{
    if (p->slot < 0) {
        return 0;
    }
    if (!stdl_xpad || !xpad_read(stdl_xpad, p->slot, &p->pad)) {
        memset(&p->pad, 0, sizeof(p->pad));
    }
    /* the reading as reported needs no report: its events would all
     * be the same as before */
    return !p->have_prev || !same(&p->pad, &p->prev);
}

uint8_t stdl_xpad_classic(const stdl_pad_t *p)
{
    const uint32_t b = p->pad.buttons;

    /* The provider has already folded stick direction into the d-pad
     * bits, which are the IKBD's own low four, so this does not repeat
     * the deadzone work. */
    return (uint8_t)((b & (IKBD_UP | IKBD_DOWN | IKBD_LEFT | IKBD_RIGHT))
                     | ((b & XPAD_SOUTH) ? IKBD_FIRE : 0));
}

/*
 * Xpad axes are -127..127; SDL wants -32768..32767. 258 is the scale
 * that puts full deflection at 32766, and it is a shift and two adds
 * rather than a __mulsi3 libcall, which a 68000 has no instruction for.
 */
static int16_t axis_scale(int8_t v)
{
    return (int16_t)((int16_t)v * 258);
}

/*
 * Triggers are 0..255 unsigned, reported as an axis resting at the
 * negative end and reaching positive full scale when pulled, which is
 * what a controller-aware game expects. 257 spreads 0..255 across the
 * whole 16-bit range exactly, and is a shift and an add.
 */
static int16_t trigger_scale(uint8_t v)
{
    return (int16_t)((int)(v * 257) - 32768);
}

int16_t stdl_xpad_axis(const stdl_pad_t *p, int axis)
{
    switch (axis) {
        case 0: return axis_scale(p->pad.lx);
        case 1: return axis_scale(p->pad.ly);
        case 2: return axis_scale(p->pad.rx);
        case 3: return axis_scale(p->pad.ry);
        case 4: return trigger_scale(p->pad.lt);
        case 5: return trigger_scale(p->pad.rt);
        default: return 0;
    }
}

int stdl_xpad_button(const stdl_pad_t *p, int button)
{
    if (button < 0 || button >= STDL_XPAD_BUTTONS) {
        return 0;
    }
    return (p->pad.buttons & button_bits[button]) ? 1 : 0;
}

uint8_t stdl_xpad_hat(const stdl_pad_t *p)
{
    const uint32_t b = p->pad.buttons;
    uint8_t hat = 0;

    /* STDL_HAT_* share SDL's values, as everything numeric here does. */
    if (b & XPAD_UP) hat |= 0x01;
    if (b & XPAD_RIGHT) hat |= 0x02;
    if (b & XPAD_DOWN) hat |= 0x04;
    if (b & XPAD_LEFT) hat |= 0x08;

    return hat;
}

int16_t stdl_xpad_axis_merged(const stdl_pad_t *p, int axis, uint8_t ikbd)
{
    /* IKBD bits per axis: 0 is left/right, 1 is up/down. */
    static const uint8_t bits[2][2] = { {0x04, 0x08}, {0x01, 0x02} };
    int16_t stick = 0;
    int16_t pad;
    long a, b;

    if (axis < 0 || axis > 1) {
        return 0;
    }

    if (ikbd & bits[axis][0]) {
        stick = -32768;
    } else if (ikbd & bits[axis][1]) {
        stick = 32767;
    }

    /* a joystick with no pad has its reading at rest */
    pad = stdl_xpad_axis(p, axis);

    /*
     * Whichever is pushed further wins, which is what makes this a
     * merge rather than a priority: a stick on the port and a pad both
     * work, and neither can mask the other by sitting still. Taking the
     * pad whenever it was off centre - the obvious rule, and what this
     * used to do - meant a pad resting off centre, or one with a worn
     * stick that never quite reads zero, silently swallowed the
     * joystick entirely.
     *
     * Compared as magnitudes in long arithmetic, because -32768 has no
     * positive counterpart in an int16_t.
     */
    a = stick < 0 ? -(long)stick : (long)stick;
    b = pad < 0 ? -(long)pad : (long)pad;

    return (b > a) ? pad : stick;
}

/*
 * Events for everything the classic five cannot carry. Axes 0 and 1 and
 * button 0 belong to the joystick's own report (event.c's handle_joy for
 * joystick 0, joysticks.c's for the others), which diffs the reported
 * axis value rather than the direction bits, so it catches stick
 * movement too small to move a d-pad bit. Emitting them here as well
 * would double every one.
 */
void stdl_xpad_events(stdl_pad_t *p, int which)
{
    const XPAD_PAD *n = &p->pad;
    const XPAD_PAD *o = &p->prev;
    int i;

    /* Its first reading publishes nothing, or a pad resting at centre
     * would arrive as a burst of motion events. */
    if (p->have_prev) {
        if (n->rx != o->rx) {
            stdl_joy_event(STDL_JOYAXISMOTION, which, 2, axis_scale(n->rx));
        }
        if (n->ry != o->ry) {
            stdl_joy_event(STDL_JOYAXISMOTION, which, 3, axis_scale(n->ry));
        }
        if (n->lt != o->lt) {
            stdl_joy_event(STDL_JOYAXISMOTION, which, 4, trigger_scale(n->lt));
        }
        if (n->rt != o->rt) {
            stdl_joy_event(STDL_JOYAXISMOTION, which, 5, trigger_scale(n->rt));
        }
        /* Button 0 is fire, which the report has from the merged
         * byte, so start at 1. */
        for (i = 1; i < STDL_XPAD_BUTTONS; i++) {
            const uint32_t bit = button_bits[i];

            if ((n->buttons ^ o->buttons) & bit) {
                stdl_joy_event((n->buttons & bit) ? STDL_JOYBUTTONDOWN
                                                  : STDL_JOYBUTTONUP,
                               which, i, (n->buttons & bit) != 0);
            }
        }
        if ((n->buttons ^ o->buttons) & XPAD_DPAD) {
            stdl_joy_event(STDL_JOYHATMOTION, which, 0, stdl_xpad_hat(p));
        }
    }
    p->prev = *n;
    /* a pad without a slot has let go now: no more to say until it
     * has one again, which starts it afresh */
    p->have_prev = (uint8_t)(p->slot >= 0);
}

/*
 * Rumble, the one thing this file writes. A provider holds a rumble
 * until something replaces it, so every pulse needs an end: the pump
 * stops it once `until` has passed, and so does the shutdown path,
 * since a game that quits or crashes mid-pulse would otherwise leave
 * the pad buzzing on the desktop.
 *
 * `rumble_req` is where the running pulse was written, and NULL when
 * nothing is running. It is kept rather than looked up again because
 * the provider may drop XPAD_CAP_RUMBLE mid-pulse (a COMpad adapter
 * does, when its last rumble-capable pad leaves), and the zeroes still
 * have to reach the area the magnitudes went into.
 *
 * Written through a volatile pointer: the provider reads the area
 * from an interrupt, and seq has to land after both magnitudes, which
 * a plain store lets the compiler move.
 */
void stdl_xpad_rumble_stop(stdl_pad_t *p)
{
    volatile XPAD_REQ *r = p->rumble_req;

    if (r) {
        r->rumble[p->slot][0] = 0;
        r->rumble[p->slot][1] = 0;
        r->seq++;                   /* last, so the provider sees both */
        p->rumble_req = 0;
    }
}

void stdl_xpad_rumble_tick(stdl_pad_t *p, uint32_t now)
{
    if (p->rumble_req && (int32_t)(now - p->rumble_until) >= 0) {
        stdl_xpad_rumble_stop(p);
    }
}

/* One function with the record's lookup at its head: a lookup in front
 * of a call made two tail calls, each widening the arguments again,
 * 148 bytes before the rumble's own 290. */
int STDL_PadRumble(int which, uint8_t low, uint8_t high, uint16_t ms)
{
    stdl_pad_t *p = &stdl_pad0;
    XPAD_REQ *req = 0;

    /* Joysticks 1-5's records are joysticks.c's, there whenever the
     * count is above one. */
    if (which != 0) {
        if ((unsigned)which >= stdl_joy_count) {
            return -1;
        }
        p = &stdl_joyn->pads[which];
    }
    /* caps is read on every call, not cached at open: it may change
     * under a live block. */
    if (p->slot >= 0 && stdl_xpad_present()
        && (stdl_xpad->caps & XPAD_CAP_RUMBLE)) {
        req = xpad_req(stdl_xpad);
    }

    /*
     * Anything running stops before a refusal or a stop request, never
     * after: a refusal that left the old magnitudes in place would
     * leave a motor running with nothing to end it.
     */
    if (!req || (low == 0 && high == 0) || ms == 0) {
        stdl_xpad_rumble_stop(p);
        return req ? 0 : -1;
    }

    /* A new pulse overwrites the old one in place rather than stopping
     * it first, so the provider never sees a gap between the two. */
    if (p->rumble_req && p->rumble_req != req) {
        stdl_xpad_rumble_stop(p);
    }
    {
        volatile XPAD_REQ *r = req;

        r->rumble[p->slot][0] = low;
        r->rumble[p->slot][1] = high;
        r->seq++;                   /* last, as in the stop */
    }
    p->rumble_req = req;
    p->rumble_until = STDL_GetTicks() + ms;
    return 0;
}


