/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: CC0-1.0
 *
 * STDL example program, dedicated to the public domain so it can
 * be used as a starting point without licence concerns.
 */
/*
 * respdemo - one program, each machine's best screen
 * (stdl_responsive.h).
 *
 * It asks for 256 colours and the display's own height, then branches
 * on what the screen it got says, never on the machine: on a TT or a
 * Falcon 320x240 in 256 colours, the background a grid of fifteen
 * ramps of sixteen shades above the ST's own sixteen; on an ST, an STE
 * or a Mega STE its 320x200 in 16, the same grid in those. A frame
 * round the very edge shows the rows it has, and a ball bounces over
 * the grid, a sprite restored from a background surface each frame -
 * every surface made after the mode set, so it has the screen's
 * planes. Asking for STDL_RESPONSIVE_TALL instead of _HEIGHT would
 * open an ST's bottom border for 240 rows there too. ESC quits.
 */

#include <stdio.h>
#include <stdl/stdl.h>

#define BALL 24

/* the ramps' hues at their brightest */
static const STDL_Colour hue[15] = {
    { 255, 64, 64, 0 }, { 255, 144, 32, 0 }, { 255, 224, 32, 0 },
    { 160, 255, 32, 0 }, { 32, 224, 64, 0 }, { 32, 224, 176, 0 },
    { 32, 224, 255, 0 }, { 64, 144, 255, 0 }, { 64, 64, 255, 0 },
    { 144, 64, 255, 0 }, { 224, 64, 255, 0 }, { 255, 64, 176, 0 },
    { 255, 160, 160, 0 }, { 176, 112, 64, 0 }, { 224, 224, 224, 0 }
};

static STDL_Rect rect(int x, int y, int w, int h)
{
    STDL_Rect r;

    r.x = (int16_t)x; r.y = (int16_t)y;
    r.w = (uint16_t)w; r.h = (uint16_t)h;
    return r;
}

int main(int argc, char *argv[])
{
    static STDL_Colour ramps[240];
    STDL_Surface *s, *bg, *ball;
    STDL_Sprite *spr;
    int deep, rows, cell, i, j, x = 40, y = 30, dx = 2, dy = 1, quit = 0;

    (void)argc;
    (void)argv;
    STDL_UseResponsive(STDL_RESPONSIVE_COLOUR | STDL_RESPONSIVE_HEIGHT);
    s = STDL_SetVideoMode(320, 240, 8, STDL_DOUBLEBUF);
    if (s == NULL) {
        fprintf(stderr, "video: %s\n", STDL_GetError());
        return 1;
    }
    deep = s->format->BitsPerPixel == 8;
    rows = s->h;
    fprintf(stderr, "respdemo: 320x%d, %d colours\n", rows,
            deep ? 256 : 16);

    /* 16-255: fifteen ramps of sixteen shades; 0-15 stay the ST's */
    if (deep) {
        for (i = 0; i < 15; i++) {
            for (j = 0; j < 16; j++) {
                STDL_Colour *c = &ramps[i * 16 + j];

                c->r = (uint8_t)((hue[i].r * (j + 1)) >> 4);
                c->g = (uint8_t)((hue[i].g * (j + 1)) >> 4);
                c->b = (uint8_t)((hue[i].b * (j + 1)) >> 4);
            }
        }
        STDL_SetColours(s, ramps, 16, 240);
    }

    /* the background: a grid of 16 columns by 15 rows, every row of
     * the screen covered, and a frame round the edge */
    bg = STDL_CreateSurface(320, rows);
    ball = STDL_CreateSurface(BALL, BALL);
    if (bg == NULL || ball == NULL) {
        STDL_Quit();
        fprintf(stderr, "surfaces: %s\n", STDL_GetError());
        return 1;
    }
    cell = rows == 240 ? 16 : 14;
    for (i = 0; i < 15; i++) {
        for (j = 0; j < 16; j++) {
            STDL_Rect r = rect(j * 20, i * cell, 20,
                               i == 14 ? rows - 14 * cell : cell);

            STDL_FillRect(bg, &r, (uint8_t)(deep ? 16 + i * 16 + j
                                                 : (i + j) & 15));
        }
    }
    for (i = 0; i < 2; i++) {
        STDL_Rect h = rect(0, i * (rows - 2), 320, 2);
        STDL_Rect v = rect(i * 318, 0, 2, rows);

        STDL_FillRect(bg, &h, 15);
        STDL_FillRect(bg, &v, 15);
    }

    /* the ball: a disc on colour 0, keyed */
    STDL_FillRect(ball, NULL, 0);
    STDL_FillCircle(ball, BALL / 2, BALL / 2, BALL / 2 - 1,
                    (uint8_t)(deep ? 16 + 2 * 16 + 15 : 14));
    STDL_FillCircle(ball, BALL / 2 - 4, BALL / 2 - 4, 3, 15);
    STDL_SetColourKey(ball, 1, 0);
    spr = STDL_SpriteFromSurface(ball, BALL, STDL_PRESHIFT);
    if (spr == NULL) {
        STDL_Quit();
        fprintf(stderr, "sprite: %s\n", STDL_GetError());
        return 1;
    }
    for (i = 0; i < 2; i++) {
        STDL_BlitSurface(bg, NULL, s, NULL);
        STDL_Flip();
    }

    while (!quit) {
        STDL_Event e;
        STDL_Rect r, d;

        while (STDL_PollEvent(&e)) {
            if (e.type == STDL_KEYDOWN
                && e.key.keysym.sym == STDLK_ESCAPE) {
                quit = 1;
            }
        }
        /* a double-buffered page last showed the ball two frames ago:
         * restore round both places */
        r = rect(x - 4, y - 2, BALL + 8, BALL + 4);
        d = r;                  /* the blit writes back what it drew */
        STDL_BlitSurface(bg, &r, s, &d);
        x += dx;
        y += dy;
        if (x < 2 || x > 318 - BALL) {
            dx = -dx;
        }
        if (y < 2 || y > rows - 2 - BALL) {
            dy = -dy;
        }
        STDL_BlitSprite(spr, 0, s, x, y);
        STDL_Flip();
    }
    STDL_FreeSprite(spr);
    STDL_FreeSurface(ball);
    STDL_FreeSurface(bg);
    STDL_Quit();
    return 0;
}
