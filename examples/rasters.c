/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: CC0-1.0
 *
 * STDL example program, dedicated to the public domain so it can
 * be used as a starting point without licence concerns.
 */
/*
 * rasters - colour registers changed down the screen by Timer B.
 *
 * Colour 0 is the background and the border, so bars written into it
 * run edge to edge; colour 15 gets a gradient of its own, which the
 * box in the middle (drawn once, in colour 15) shows. Each frame
 * fills the next raster table - black, the scrolled gradient, then
 * the bars over it - and shows it with the flip, without the copy
 * into the next table that STDL_ShowRasters(1) would make for a
 * program changing only a few entries.
 * STDL_OpenRasters refuses while a border is open: both run on
 * Timer B. ESC quits; the rasters close on the way out, however the
 * program ends.
 */

#include <stdio.h>
#include <stdl/stdl.h>

#define STEP 2                  /* lines an entry */

/* a bar's shading, out from its centre: STE 4-bit levels */
static const uint16_t bar[8] = {
    STDL_HW444(0xFFF), STDL_HW444(0xEDB), STDL_HW444(0xCA8),
    STDL_HW444(0xA86), STDL_HW444(0x864), STDL_HW444(0x643),
    STDL_HW444(0x422), STDL_HW444(0x211)
};

/* a triangle wave 0..range..0 over a period of 128 */
static int wave(int t, int range)
{
    t &= 127;
    return STDL_MulS16(t < 64 ? t : 127 - t, range) >> 6;
}

int main(int argc, char *argv[])
{
    STDL_Surface *s;
    STDL_Rect box = { 96, 60, 128, 80 };
    int n, k, frame = 0, quit = 0;

    (void)argc;
    (void)argv;
    s = STDL_SetVideoMode(320, 200, 4, STDL_DOUBLEBUF);
    if (s == NULL) {
        fprintf(stderr, "video: %s\n", STDL_GetError());
        return 1;
    }
    /* the box on both pages, drawn once */
    for (k = 0; k < 2; k++) {
        STDL_FillRect(s, NULL, 0);
        STDL_FillRect(s, &box, 15);
        STDL_Flip();
    }
    n = STDL_OpenRasters(STEP, 0, 15);
    if (n < 0) {
        STDL_Quit();
        fprintf(stderr, "rasters: %s\n", STDL_GetError());
        return 1;
    }

    while (!quit) {
        STDL_Event e;
        uint16_t *t = STDL_RasterTable();
        int i;

        while (STDL_PollEvent(&e)) {
            if (e.type == STDL_KEYDOWN
                && e.key.keysym.sym == STDLK_ESCAPE) {
                quit = 1;
            }
        }
        /* colour 0: black, then three bars bouncing at their own
         * speeds; colour 15: a gradient that scrolls */
        for (i = 0; i < n; i++) {
            t[i * 2] = 0;
            t[i * 2 + 1] = STDL_HW444(((i + frame) >> 3 & 15) << 4
                                      | 0x0F);
        }
        for (k = 0; k < 3; k++) {
            const int c = 8 + wave(STDL_MulS16(frame, k + 2) + k * 40,
                                   n - 16);

            for (i = -7; i <= 7; i++) {
                t[(c + i) * 2] = bar[i < 0 ? -i : i];
            }
        }
        STDL_ShowRasters(0);    /* every entry rewritten: no copy */
        STDL_Flip();
        frame++;
    }
    STDL_CloseRasters();
    STDL_Quit();
    return 0;
}
