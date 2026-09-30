/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Test fixture: polls every joystick input and prints it, so the
 * button and hat mapping can be checked against a known provider
 * state. testjoystick only reports events, and events fire on change,
 * which a static provider never produces.
 */

#include <SDL.h>
#include <stdio.h>

#include "stdl_xpad.h"
#include "stdl_event.h"
#include "stdl_keys.h"
#include "xpad.h"

/* Pump for `ms`, which is what ends a rumble. */
static void pump_for(uint32_t ms)
{
    uint32_t t0 = STDL_GetTicks();

    while (STDL_GetTicks() - t0 < ms) {
        STDL_PumpEvents();
    }
}

/*
 * What the request area held at one moment. Taken into memory and
 * printed afterwards: a console print costs about 150ms under Hatari,
 * which is longer than the pulse being timed.
 */
typedef struct {
    const char *what;
    int ret;
    uint8_t lo, hi, seq;
} RumbleSnap;

static void snap(RumbleSnap *s, const char *what, int ret,
                 const XPAD_REQ *req)
{
    s->what = what;
    s->ret = ret;
    s->lo = req->rumble[0][0];
    s->hi = req->rumble[0][1];
    s->seq = req->seq;
}

int main(void)
{
    SDL_Joystick *js;
    int i, n;

    SDL_Init(SDL_INIT_VIDEO | SDL_INIT_JOYSTICK);
    SDL_SetVideoMode(320, 200, 4, 0);

    js = SDL_JoystickOpen(0);
    if (!js) {
        fprintf(stderr, "no joystick\r\n");
        return 1;
    }

    SDL_JoystickUpdate();

    fprintf(stderr, "axes=%d hats=%d buttons=%d\r\n",
            SDL_JoystickNumAxes(js), SDL_JoystickNumHats(js),
            SDL_JoystickNumButtons(js));

    n = SDL_JoystickNumAxes(js);
    for (i = 0; i < n; i++) {
        fprintf(stderr, "axis%d=%d\r\n", i, (int)SDL_JoystickGetAxis(js, i));
    }

    n = SDL_JoystickNumButtons(js);
    for (i = 0; i < n; i++) {
        if (SDL_JoystickGetButton(js, i)) {
            fprintf(stderr, "button%d=down\r\n", i);
        }
    }

    fprintf(stderr, "hat0=%d\r\n", (int)SDL_JoystickGetHat(js, 0));

    /*
     * Key emulation is driven from the merged joystick byte, so a pad
     * should synthesise keys exactly as a stick does. That is the whole
     * point of merging there rather than bolting Xpad on beside it:
     * keyboard-driven ports get a controller without knowing.
     */
    {
        const uint8_t *keys;
        int nkeys = 0;

        /* Deliberately enabled *after* the pump, with the pad already
         * holding a direction: emulation fires on change, so this is
         * the case that used to synthesise nothing at all. */
        STDL_JoyKeyMapping(STDLK_UP, STDLK_DOWN, STDLK_LEFT,
                           STDLK_RIGHT, STDLK_SPACE);
        /* A pad-only button: the stub holds the right shoulder, which
         * an ST joystick has no way to express. */
        STDL_JoyKeyBind(STDL_JOYKEY_TR, STDLK_RETURN);
        STDL_JoyKeyBind(STDL_JOYKEY_START, STDLK_ESCAPE);
        STDL_JoyKeyEmulation(1);

        keys = STDL_GetKeyState(&nkeys);
        fprintf(stderr, "joykey right=%d space=%d up=%d\r\n",
                keys[STDLK_RIGHT] ? 1 : 0,
                keys[STDLK_SPACE] ? 1 : 0,
                keys[STDLK_UP] ? 1 : 0);
        /* The native poll path, which a game with its own notion of
         * buttons uses instead of binding keys. */
        fprintf(stderr, "native pad=%d south=%d tr=%d start=%d right=%d\r\n",
                STDL_HavePad() ? 1 : 0,
                STDL_JoyInputHeld(STDL_JOYKEY_SOUTH),
                STDL_JoyInputHeld(STDL_JOYKEY_TR),
                STDL_JoyInputHeld(STDL_JOYKEY_START),
                STDL_JoyInputHeld(STDL_JOYKEY_RIGHT));
        fprintf(stderr, "joykey return=%d escape=%d\r\n",
                keys[STDLK_RETURN] ? 1 : 0,
                keys[STDLK_ESCAPE] ? 1 : 0);
    }

    /*
     * The axis merge, driven with fabricated joystick bytes. Hatari's
     * emulated joystick cannot be moved from this harness, so the case
     * that matters - both sources active at once - is unreachable any
     * other way. The stub holds lx +25800 and ly -12900.
     */
    fprintf(stderr, "merge x idle=%d right=%d left=%d\r\n",
            (int)stdl_xpad_axis_merged(0, 0x00),
            (int)stdl_xpad_axis_merged(0, 0x08),
            (int)stdl_xpad_axis_merged(0, 0x04));
    fprintf(stderr, "merge y idle=%d down=%d\r\n",
            (int)stdl_xpad_axis_merged(1, 0x00),
            (int)stdl_xpad_axis_merged(1, 0x02));

    /*
     * Rumble. The stub offers a request area and XPAD_CAP_RUMBLE, and a
     * provider's area is plain memory, so what STDL_PadRumble wrote can
     * be read straight back. The cast is the test's alone: it drops the
     * cap below to play a pad that loses its motors mid-pulse.
     */
    {
        XPAD *x = (XPAD *)xpad_find();
        XPAD_REQ *req = xpad_req(x);
        RumbleSnap s[7];
        int taken = 0;

        if (!req) {
            fprintf(stderr, "rumble no request area\r\n");
            fprintf(stderr, "PROBE-DONE\r\n");
            SDL_Quit();
            return 0;
        }

        snap(&s[taken++], "before", 0, req);
        snap(&s[taken++], "start", STDL_PadRumble(200, 60, 100), req);
        pump_for(50);
        snap(&s[taken++], "at 50ms", 0, req);
        pump_for(100);
        snap(&s[taken++], "at 150ms", 0, req);

        STDL_PadRumble(90, 0, 1000);
        snap(&s[taken++], "stop", STDL_PadRumble(0, 0, 0), req);

        STDL_PadRumble(90, 0, 1000);
        x->caps &= (uint16_t)~XPAD_CAP_RUMBLE;
        snap(&s[taken++], "refused", STDL_PadRumble(90, 90, 100), req);
        x->caps |= XPAD_CAP_RUMBLE;

        /* Left running on purpose: SDL_Quit must end it. */
        STDL_PadRumble(120, 120, 5000);
        SDL_Quit();
        snap(&s[taken++], "after quit", 0, req);

        for (i = 0; i < taken; i++) {
            fprintf(stderr, "rumble %s ret=%d lo=%d hi=%d seq=%d\r\n",
                    s[i].what, s[i].ret, s[i].lo, s[i].hi, s[i].seq);
        }
        fprintf(stderr, "PROBE-DONE\r\n");
    }
    return 0;
}
