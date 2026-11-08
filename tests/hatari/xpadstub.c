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
 *
 * Built by hand, against the xpad submodule:
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -m68000 -Ilib/xpad/src \
 *       tests/hatari/xpadstub.c lib/xpad/src/xpad.c \
 *       lib/xpad/src/xpad_provider.c -o dist/XPADSTUB.PRG
 *
 * With -DXPADSTUB_PADS=4 it publishes four slots, pads 1-3 each held in
 * a state of its own besides pad 0's, for more than one joystick
 * (tests/hatari/joychk.c).
 */

#include <mint/basepage.h>
#include <mint/osbind.h>
#include <stdio.h>

#include "xpad.h"

#ifndef XPADSTUB_PADS
#define XPADSTUB_PADS 1
#endif

static XPAD block;
static XPAD_REQ req;    /* read back by xpadprobe's rumble checks */

int main(void)
{
    XPAD_PAD *pad;
    int i;

    xpad_init(&block, XPADSTUB_PADS,
              XPAD_CAP_ANALOG | XPAD_CAP_HOTPLUG | XPAD_CAP_RUMBLE,
              "STDL test stub 1.0", &req);

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
#if XPADSTUB_PADS > 1
    /* Each of the others in a state of its own: a direction and a
     * button no other pad holds, and a stick or a trigger. */
    pad[1].type = XPAD_TYPE_GAMEPAD;
    pad[1].flags = XPAD_PAD_ANALOG;
    pad[1].buttons = XPAD_LEFT | XPAD_EAST | XPAD_TL;
    pad[1].lx = -60;
#endif
#if XPADSTUB_PADS > 2
    pad[2].type = XPAD_TYPE_GAMEPAD;
    pad[2].flags = XPAD_PAD_ANALOG;
    pad[2].buttons = XPAD_UP | XPAD_START;
    pad[2].lt = 128;
#endif
#if XPADSTUB_PADS > 3
    pad[3].type = XPAD_TYPE_GAMEPAD;
    pad[3].flags = XPAD_PAD_ANALOG;
    pad[3].buttons = XPAD_DOWN | XPAD_SOUTH;
    pad[3].ry = 127;
#endif
    xpad_commit(&block);

    /* Both buffers, so a consumer reading either sees the same thing. */
    pad = xpad_back(&block);
    for (i = 0; i < XPADSTUB_PADS; i++) {
        pad[i] = *(XPAD_PAD *)XPAD_PAD_AT(&block, block.active, i);
    }
    xpad_commit(&block);

    if (!xpad_publish(&block)) {
        printf("xpadstub: could not install the XPAD cookie\r\n");
        return 1;
    }

    printf("xpadstub: Xpad provider installed\r\n");

    Ptermres(_base->p_tlen + _base->p_dlen + _base->p_blen + 256, 0);

    return 0; /* not reached */
}
