/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: CC0-1.0
 *
 * JOYBOXES: two joysticks, two boxes. The stick in the joystick port
 * moves the red box, the stick in the mouse port the blue one; fire
 * lights a box white while it is held. Escape quits.
 *
 * STDL_SetNumJoysticks(2) is the whole of the set-up: the mouse port
 * stops being a mouse and becomes joystick 1, and STDL_GetJoystickState
 * reads either stick by its number. A second stick always costs the
 * mouse, as on any ST, and STDL gives the mouse back at exit however
 * the program leaves. Where an Xpad provider is present its first two
 * pads drive the same boxes, merged with the sticks; a program that
 * wants pads as players of their own asks for STDL_UseJoystickMerge(0)
 * before counting them.
 *
 * Each frame puts back only what the last frame on that page drew
 * (STDL_Dirty) and draws the boxes again, double-buffered. On exit it
 * prints how far each box moved.
 */

#include <stdio.h>
#include <stdl/stdl.h>

#define BOX     24
#define SPEED   2               /* pixels a frame                    */
#define BOXES   2

/* colours: 0 black, 1 red, 2 blue, 15 white */
#define C_WHITE 15

int main(void)
{
    static const uint8_t colour[BOXES] = { 1, 2 };
    int x[BOXES] = { 80, 216 };
    int y[BOXES] = { 88, 88 };
    unsigned long moved[BOXES] = { 0, 0 };
    STDL_Surface *background;
    STDL_Event ev;
    int i, running = 1;

    if (STDL_Init(STDL_INIT_VIDEO | STDL_INIT_JOYSTICK) < 0
        || STDL_SetVideoMode(320, 200, 4, STDL_DOUBLEBUF) == NULL) {
        fprintf(stderr, "JOYBOXES: %s\n", STDL_GetError());
        return 1;
    }
    /* two joysticks: the mouse port becomes joystick 1. Two can always
     * be had, as every ST has both ports */
    STDL_SetNumJoysticks(BOXES);
    STDL_SetColour(0, STDL_HWColour(0, 0, 0));
    STDL_SetColour(1, STDL_HWColour(224, 32, 32));
    STDL_SetColour(2, STDL_HWColour(64, 96, 255));
    STDL_SetColour(C_WHITE, STDL_HWColour(255, 255, 255));

    /* a plain black background, which the boxes are put back from */
    background = STDL_CreateSurface(320, 200);
    if (background == NULL || STDL_DirtyInit(background, 8) < 0) {
        fprintf(stderr, "JOYBOXES: %s\n", STDL_GetError());
        STDL_Quit();
        return 1;
    }
    fprintf(stderr, "JOYBOXES:ready\n");

    while (running) {
        STDL_Surface *page = STDL_GetVideoSurface();

        while (STDL_PollEvent(&ev)) {
            if (ev.type == STDL_QUIT || (ev.type == STDL_KEYDOWN
                && ev.key.keysym.sym == STDLK_ESCAPE)) {
                running = 0;
            }
        }
        STDL_DirtyRestore(page);
        for (i = 0; i < BOXES; i++) {
            /* bit 0 up, 1 down, 2 left, 3 right, 7 fire */
            const uint8_t s = STDL_GetJoystickState(i);
            const int ox = x[i], oy = y[i];
            STDL_Rect box;

            if (s & 0x01) y[i] -= SPEED;
            if (s & 0x02) y[i] += SPEED;
            if (s & 0x04) x[i] -= SPEED;
            if (s & 0x08) x[i] += SPEED;
            x[i] = x[i] < 0 ? 0 : x[i] > 320 - BOX ? 320 - BOX : x[i];
            y[i] = y[i] < 0 ? 0 : y[i] > 200 - BOX ? 200 - BOX : y[i];
            moved[i] += (unsigned long)((x[i] != ox) + (y[i] != oy));

            box.x = (int16_t)x[i];
            box.y = (int16_t)y[i];
            box.w = BOX;
            box.h = BOX;
            STDL_FillRect(page, &box, (s & 0x80) ? C_WHITE : colour[i]);
            STDL_DirtyPush(&box);
        }
        STDL_Flip();
    }

    STDL_DirtyQuit();
    STDL_FreeSurface(background);
    STDL_Quit();                /* the mouse comes back here */
    printf("JOYBOXES moved=%lu,%lu\n", moved[0], moved[1]);
    return 0;
}
