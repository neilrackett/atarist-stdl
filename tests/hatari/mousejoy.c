/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Does joystick 1's fire button survive STDL_EnableMouse(0)?
 *
 * This is a safety check on a new API rather than a demo. While
 * mouse reporting is on - and TOS leaves it on - the IKBD strips
 * the fire bit out of joystick 1's packets and reports it as the
 * right mouse button instead; event.c folds it back, with the
 * reason in the comment at its TAG_MOUSEBTN case. Turning the
 * mouse off should mean fire arrives in the joystick packet where
 * it belongs and the fold simply stops being needed.
 *
 * Should. If instead the IKBD keeps stealing it and sends no mouse
 * packets to steal it into, a joystick game that called
 * STDL_EnableMouse(0) would silently lose its fire button - which
 * is a far worse bug than the flicker the call exists to cure. No
 * emulator answers this: it is IKBD firmware behaviour.
 *
 * Five blocks across the middle: up, down, left, right, fire, lit
 * green while held. The band behind them is the mouse state -
 * green for reporting on, blue for off - and SPACE toggles it.
 *
 * Work the stick in both states. If the first four blocks respond
 * either way but fire only works in green, the API needs to say so
 * loudly or send something more than $12.
 */
#include <stddef.h>
#include <stdl/stdl.h>

int main(int argc, char *argv[])
{
    STDL_Surface *screen;
    int mouse = 1;

    (void)argc; (void)argv;
    if (STDL_Init(STDL_INIT_VIDEO | STDL_INIT_JOYSTICK) < 0) {
        return 1;
    }
    screen = STDL_SetVideoMode(320, 200, 4, 0);
    if (screen == NULL) {
        STDL_Quit();
        return 1;
    }

    for (;;) {
        STDL_Event ev;
        STDL_Rect r;
        uint8_t j;
        int i;

        while (STDL_PollEvent(&ev)) {
            if (ev.type == STDL_KEYDOWN) {
                if (ev.key.keysym.sym == STDLK_ESCAPE) {
                    goto done;
                }
                if (ev.key.keysym.sym == STDLK_SPACE) {
                    mouse = !mouse;
                    STDL_EnableMouse(mouse);
                }
            }
        }
        j = STDL_GetJoyState();

        /* the band says which mode we are in, so a photograph of a
         * held fire button carries its own context */
        r.x = 0; r.y = 80; r.w = 320; r.h = 40;
        STDL_FillRect(screen, &r, (uint8_t)(mouse ? 10 : 12));

        /* up, down, left, right, fire */
        for (i = 0; i < 5; i++) {
            static const uint8_t bit[5] = { 0x01, 0x02, 0x04, 0x08, 0x80 };

            r.x = (int16_t)(40 + i * 50); r.y = 90;
            r.w = 32; r.h = 20;
            STDL_FillRect(screen, &r, (uint8_t)((j & bit[i]) ? 15 : 0));
        }
        STDL_WaitVBL();
    }
done:
    STDL_Quit();
    return 0;
}
