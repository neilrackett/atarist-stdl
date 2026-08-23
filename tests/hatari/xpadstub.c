/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Test fixture: publishes a static Xpad block and stays resident, so
 * the consumer path can be exercised under Hatari.
 *
 * A real provider is not usable here. The IKBD example drivers in
 * atarist-xpad hook joyvec and kbdvec, and STDL replaces ikbdsys, so
 * TOS never dispatches either while a game is running; and the
 * cartridge-port providers need hardware Hatari does not emulate. What
 * this proves is the half STDL owns: find the cookie, read the block,
 * merge it, and report it through the SDL joystick API.
 *
 * Values are fixed and deliberately asymmetric, so a wrong axis or a
 * wrong button index shows up as a wrong number rather than a plausible
 * one. Drop it in AUTO\ on the test drive.
 */

#include <mint/basepage.h>
#include <mint/osbind.h>
#include <stdio.h>

#include "../../src/xpad.h"

static XPAD block;

int main(void)
{
    XPAD_PAD *pad;

    xpad_init(&block, 1, XPAD_CAP_ANALOG | XPAD_CAP_HOTPLUG,
              "STDL test stub 1.0", 0);

    pad = xpad_back(&block);
    pad[0].type = XPAD_TYPE_GAMEPAD;
    pad[0].flags = XPAD_PAD_ANALOG | XPAD_PAD_WIRELESS;
    /* A direction, the fire button, and a shoulder that only exists on
     * a pad: one of each kind the merge has to carry. */
    pad[0].buttons = XPAD_RIGHT | XPAD_SOUTH | XPAD_TR;
    pad[0].lx = 100;
    pad[0].ly = -50;
    pad[0].rx = 25;
    pad[0].ry = 0;
    pad[0].lt = 255;
    pad[0].rt = 0;
    xpad_commit(&block);

    /* Both buffers, so a consumer reading either sees the same thing. */
    pad = xpad_back(&block);
    pad[0] = *(XPAD_PAD *)XPAD_PAD_AT(&block, block.active, 0);
    xpad_commit(&block);

    if (!xpad_publish(&block)) {
        printf("xpadstub: could not install the XPAD cookie\r\n");
        return 1;
    }

    printf("xpadstub: Xpad provider installed\r\n");

    Ptermres(_base->p_tlen + _base->p_dlen + _base->p_blen + 256, 0);

    return 0; /* not reached */
}
