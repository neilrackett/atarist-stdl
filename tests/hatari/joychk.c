/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Test fixture: more than one joystick (STDL_SetNumJoysticks) on the
 * machine. Prints every joystick event with its `which`, every mouse
 * button, and what each call answers, so a run can be read back from
 * the console log:
 *
 *   J which axis|down|up|hat index value
 *   M down|up button
 *   N asked got count merge M pads(0..5)
 *
 * Keys: 1-6 ask for that many joysticks, and m merges or parts the
 * pads (STDL_UseJoystickMerge); s prints each joystick's
 * state and a few held inputs; r rumbles each pad for 200ms and prints
 * the provider's request area at the start and once the pump has ended
 * it; Esc quits through STDL_Quit.
 *
 * Started with a command line (tests/hatari/mousebk.c does), it asks
 * for the number its first character says, pumps for a second, and
 * leaves by the route the second says: q STDL_Quit, e exit() and the
 * atexit handler, a abort(), which reaches the terminate vector alone,
 * b an address error, a TOS exception.
 *
 * Hatari can drive both ports: tests/hatari/joykeys.cfg emulates both
 * from the keyboard, port 1 (joystick 0) on I K J L and H, port 0
 * (joystick 1 once there are two) on W X A D and F, and run.sh's
 * `keydown`/`keyup` of those letters move them. Drop XPADSTUB.PRG built with -DXPADSTUB_PADS=4 in AUTO\ for
 * four pads; remove it afterwards.
 *
 * Built by hand:
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 -Iinclude \
 *       -Ilib/xpad/src tests/hatari/joychk.c libstdl.a -o dist/JOYCHK.TOS
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <stdl/stdl.h>
#include "xpad.h"

static const XPAD *block;

static void say_count(int asked, int got)
{
    fprintf(stderr, "N %d %d %d merge %d pads %d%d%d%d%d%d\r\n", asked, got,
            STDL_NumJoysticks(), STDL_UseJoystickMerge(-1),
            STDL_HasPad(0), STDL_HasPad(1), STDL_HasPad(2),
            STDL_HasPad(3), STDL_HasPad(4), STDL_HasPad(5));
}

static void say_event(const STDL_Event *e)
{
    switch (e->type) {
    case STDL_JOYAXISMOTION:
        fprintf(stderr, "J %d axis %d %d\r\n", e->jaxis.which,
                e->jaxis.axis, e->jaxis.value);
        break;
    case STDL_JOYBUTTONDOWN:
    case STDL_JOYBUTTONUP:
        fprintf(stderr, "J %d %s %d\r\n", e->jbutton.which,
                e->type == STDL_JOYBUTTONDOWN ? "down" : "up",
                e->jbutton.button);
        break;
    case STDL_JOYHATMOTION:
        fprintf(stderr, "J %d hat %d %d\r\n", e->jhat.which, e->jhat.hat,
                e->jhat.value);
        break;
    case STDL_MOUSEBUTTONDOWN:
    case STDL_MOUSEBUTTONUP:
        fprintf(stderr, "M %s %d\r\n",
                e->type == STDL_MOUSEBUTTONDOWN ? "down" : "up",
                e->button.button);
        break;
    default:
        break;
    }
}

static void say_state(void)
{
    int i;

    for (i = 0; i < STDL_MAX_JOYSTICKS; i++) {
        fprintf(stderr, "S %d state %02x fire %d tl %d start %d\r\n", i,
                STDL_GetJoystickState(i),
                STDL_JoystickInputHeld(i, STDL_JOYSTICK_FIRE),
                STDL_JoystickInputHeld(i, STDL_JOYSTICK_TL),
                STDL_JoystickInputHeld(i, STDL_JOYSTICK_START));
    }
}

static void pump_for(uint32_t ms)
{
    const uint32_t t0 = STDL_GetTicks();
    STDL_Event e;

    while (STDL_GetTicks() - t0 < ms) {
        while (STDL_PollEvent(&e)) {
            say_event(&e);
        }
    }
}

/* Snapshots taken first and printed after: a console print costs
 * about 150ms under Hatari, as long as the pulse. Each joystick's
 * return ("R joystick ret"), then the request area a pad slot at a
 * time ("P slot on off"), joystick i writing 0x10+i and 0x20+i. */
static void rumble(void)
{
    XPAD_REQ *req = block ? xpad_req(block) : NULL;
    uint8_t on[XPAD_MAX_PADS][2], off[XPAD_MAX_PADS][2];
    int ret[STDL_MAX_JOYSTICKS];
    int i;

    for (i = 0; i < STDL_MAX_JOYSTICKS; i++) {
        ret[i] = STDL_PadRumble(i, (uint8_t)(0x10 + i), (uint8_t)(0x20 + i),
                                200);
    }
    for (i = 0; req && i < XPAD_MAX_PADS; i++) {
        on[i][0] = req->rumble[i][0];
        on[i][1] = req->rumble[i][1];
    }
    pump_for(400);
    for (i = 0; req && i < XPAD_MAX_PADS; i++) {
        off[i][0] = req->rumble[i][0];
        off[i][1] = req->rumble[i][1];
    }
    for (i = 0; i < STDL_MAX_JOYSTICKS; i++) {
        fprintf(stderr, "R %d ret %d\r\n", i, ret[i]);
    }
    for (i = 0; req && i < XPAD_MAX_PADS; i++) {
        fprintf(stderr, "P %d on %02x%02x off %02x%02x\r\n", i, on[i][0],
                on[i][1], off[i][0], off[i][1]);
    }
}

int main(int argc, char **argv)
{
    const char *route = argc > 1 ? argv[1] : "";

    if (STDL_Init(STDL_INIT_VIDEO) < 0) {
        return 1;
    }
    block = xpad_find();
    if (route[0] >= '1' && route[0] <= '6') {
        say_count(route[0] - '0', STDL_SetNumJoysticks(route[0] - '0'));
    }
    STDL_OpenInput();
    fprintf(stderr, "JOYCHK ready\r\n");

    if (route[0] != '\0') {
        pump_for(1000);
        fprintf(stderr, "JOYCHK leaving by %c\r\n", route[1]);
        switch (route[1]) {
        case 'q':
            STDL_Quit();
            return 0;
        case 'e':
            exit(3);
        case 'a':
            abort();
        case 'b':
            *(volatile long *)0x00000001L = 0;      /* address error */
            break;
        default:
            break;
        }
        return 4;
    }

    for (;;) {
        STDL_Event e;

        while (STDL_PollEvent(&e)) {
            say_event(&e);
            if (e.type != STDL_KEYDOWN) {
                continue;
            }
            switch (e.key.keysym.sym) {
            case STDLK_ESCAPE:
                fprintf(stderr, "JOYCHK done\r\n");
                STDL_Quit();
                return 0;
            case '1': case '2': case '3': case '4': case '5': case '6':
                say_count(e.key.keysym.sym - '0',
                          STDL_SetNumJoysticks(e.key.keysym.sym - '0'));
                break;
            case 'm':
                STDL_UseJoystickMerge(!STDL_UseJoystickMerge(-1));
                say_count(0, STDL_NumJoysticks());
                break;
            case 's':
                say_state();
                break;
            case 'r':
                rumble();
                break;
            default:
                break;
            }
        }
    }
}
