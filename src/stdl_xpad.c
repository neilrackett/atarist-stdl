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
 * The library is in supervisor mode throughout, but xpad_find() goes
 * through Supexec, which is safe from either mode, so this does not
 * need to care.
 */

#include "stdl_xpad.h"

#include <string.h>

#include "stdl_internal.h"
#include "xpad.h"

/* Classic five, in IKBD shape, so the caller can OR it straight into
 * the byte the packet parser produces. */
#define IKBD_UP 0x01
#define IKBD_DOWN 0x02
#define IKBD_LEFT 0x04
#define IKBD_RIGHT 0x08
#define IKBD_FIRE 0x80

static const XPAD *xpad;
static int opened;        /* looked for a provider yet */
static XPAD_PAD pad;      /* this frame */
static XPAD_PAD prev;     /* last frame, for the event diff */
static int have_prev;

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
    xpad = xpad_find();
    opened = 1;

    memset(&pad, 0, sizeof(pad));
    memset(&prev, 0, sizeof(prev));
    have_prev = 0;
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
    return xpad != 0;
}

uint8_t stdl_xpad_poll(void)
{
    uint8_t classic = 0;

    if (!xpad || !xpad_read(xpad, 0, &pad)) {
        memset(&pad, 0, sizeof(pad));
        return 0;
    }

    /* The provider has already folded stick direction into the d-pad
     * bits, so this does not repeat the deadzone work. */
    if (pad.buttons & XPAD_UP) classic |= IKBD_UP;
    if (pad.buttons & XPAD_DOWN) classic |= IKBD_DOWN;
    if (pad.buttons & XPAD_LEFT) classic |= IKBD_LEFT;
    if (pad.buttons & XPAD_RIGHT) classic |= IKBD_RIGHT;
    if (pad.buttons & XPAD_SOUTH) classic |= IKBD_FIRE;

    return classic;
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

int16_t stdl_xpad_axis(int axis)
{
    switch (axis) {
        case 0: return axis_scale(pad.lx);
        case 1: return axis_scale(pad.ly);
        case 2: return axis_scale(pad.rx);
        case 3: return axis_scale(pad.ry);
        case 4: return trigger_scale(pad.lt);
        case 5: return trigger_scale(pad.rt);
        default: return 0;
    }
}

int stdl_xpad_button(int button)
{
    if (button < 0 || button >= STDL_XPAD_BUTTONS) {
        return 0;
    }
    return (pad.buttons & button_bits[button]) ? 1 : 0;
}

uint8_t stdl_xpad_hat(void)
{
    uint8_t hat = 0;

    /* STDL_HAT_* share SDL's values, as everything numeric here does. */
    if (pad.buttons & XPAD_UP) hat |= 0x01;
    if (pad.buttons & XPAD_RIGHT) hat |= 0x02;
    if (pad.buttons & XPAD_DOWN) hat |= 0x04;
    if (pad.buttons & XPAD_LEFT) hat |= 0x08;

    return hat;
}

int16_t stdl_xpad_axis_merged(int axis, uint8_t state, uint8_t neg,
                              uint8_t pos)
{
    if (stdl_xpad_present()) {
        int16_t v = stdl_xpad_axis(axis);

        if (v != 0) {
            return v;
        }
    }
    if (state & neg) {
        return -32768;
    }
    if (state & pos) {
        return 32767;
    }
    return 0;
}

static void push_axis(uint8_t axis, int16_t value)
{
    STDL_Event ev;

    memset(&ev, 0, sizeof(ev));
    ev.jaxis.type = STDL_JOYAXISMOTION;
    ev.jaxis.which = 0;
    ev.jaxis.axis = axis;
    ev.jaxis.value = value;
    STDL_PushEvent(&ev);
}

/*
 * Events for everything the classic five cannot carry. Axes 0 and 1 and
 * button 0 are left to event.c, which drives them from the merged
 * joystick byte: emitting them here as well would double every fire.
 */
void stdl_xpad_events(void)
{
    STDL_Event ev;
    int i;

    if (!xpad) {
        return;
    }

    if (!have_prev) {
        /* First poll after opening: publish nothing, or a pad resting
         * at centre would arrive as a burst of motion events. */
        prev = pad;
        have_prev = 1;
        return;
    }

    if (pad.rx != prev.rx) push_axis(2, axis_scale(pad.rx));
    if (pad.ry != prev.ry) push_axis(3, axis_scale(pad.ry));
    if (pad.lt != prev.lt) push_axis(4, trigger_scale(pad.lt));
    if (pad.rt != prev.rt) push_axis(5, trigger_scale(pad.rt));

    /*
     * The left stick reaches event.c as d-pad bits, so it only reports
     * axes 0 and 1 when a direction crosses the provider's deadzone.
     * Movement inside the deadzone, and movement between two points on
     * the same side of it, still has to be reported: hence these two,
     * which fire on any change the digital byte cannot express.
     */
    if (pad.lx != prev.lx &&
        (pad.buttons & (XPAD_LEFT | XPAD_RIGHT)) ==
            (prev.buttons & (XPAD_LEFT | XPAD_RIGHT))) {
        push_axis(0, axis_scale(pad.lx));
    }
    if (pad.ly != prev.ly &&
        (pad.buttons & (XPAD_UP | XPAD_DOWN)) ==
            (prev.buttons & (XPAD_UP | XPAD_DOWN))) {
        push_axis(1, axis_scale(pad.ly));
    }

    /* Button 0 is the fire button, and event.c already reports it from
     * the merged byte, so start at 1. */
    for (i = 1; i < STDL_XPAD_BUTTONS; i++) {
        uint32_t bit = button_bits[i];

        if ((pad.buttons & bit) == (prev.buttons & bit)) {
            continue;
        }
        memset(&ev, 0, sizeof(ev));
        ev.jbutton.type = (pad.buttons & bit) ? STDL_JOYBUTTONDOWN
                                              : STDL_JOYBUTTONUP;
        ev.jbutton.which = 0;
        ev.jbutton.button = (uint8_t)i;
        ev.jbutton.state = (pad.buttons & bit) ? STDL_PRESSED
                                               : STDL_RELEASED;
        STDL_PushEvent(&ev);
    }

    if ((pad.buttons & XPAD_DPAD) != (prev.buttons & XPAD_DPAD)) {
        memset(&ev, 0, sizeof(ev));
        ev.jhat.type = STDL_JOYHATMOTION;
        ev.jhat.which = 0;
        ev.jhat.hat = 0;
        ev.jhat.value = stdl_xpad_hat();
        STDL_PushEvent(&ev);
    }

    prev = pad;
}
