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

#include "stdl_event.h"
#include "stdl_keys.h"

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
     * point of merging there rather than bolting XPad on beside it:
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
        STDL_JoyKeyEmulation(1);

        keys = STDL_GetKeyState(&nkeys);
        fprintf(stderr, "joykey right=%d space=%d up=%d\r\n",
                keys[STDLK_RIGHT] ? 1 : 0,
                keys[STDLK_SPACE] ? 1 : 0,
                keys[STDLK_UP] ? 1 : 0);
    }

    fprintf(stderr, "PROBE-DONE\r\n");

    SDL_Quit();
    return 0;
}
