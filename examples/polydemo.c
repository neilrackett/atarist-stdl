/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: CC0-1.0
 *
 * STDL example program, dedicated to the public domain so it can
 * be used as a starting point without licence concerns.
 */
/*
 * polydemo - filled polygons XORed into single planes, a glenz look.
 *
 * Three shapes turn at different speeds, each XORed into a plane of
 * its own with STDL_FillPolygon and STDL_DRAW_XOR: where they cross,
 * the planes add up to the colours between, and the palette decides
 * what those look like. Polygons that share an edge tile exactly
 * under the fill rule: the star is ten triangles fanned about its
 * centre, each sharing an edge with the next, drawn one by one, and
 * it shows no seam - XOR would cancel any pixel drawn twice.
 *
 * The rotation is 16-bit fixed point through STDL_MulS16: gcc 4.6
 * turns `int * int` into a library call, and a sine-table multiply
 * per vertex per frame is exactly the kind of code where that hides.
 * Double-buffered, cleared each frame. ESC quits.
 */

#include <stdio.h>
#include <stdl/stdl.h>

#define NSIN 256                /* a full turn */

static int16_t sintab[NSIN];

/* a sine table in Q14 without a float: the recurrence
 * s[n+1] = 2cos(d) s[n] - s[n-1], one 16-bit multiply a step */
static void make_sine(void)
{
    const int c2 = 32758;       /* 2 cos(2pi/256) in Q14 */
    int i;

    sintab[0] = 0;
    sintab[1] = 402;            /* sin(2pi/256) in Q14 */
    for (i = 2; i < NSIN; i++) {
        sintab[i] = (int16_t)((STDL_MulS16(c2, sintab[i - 1]) >> 14)
                              - sintab[i - 2]);
    }
}

#define SIN(a) sintab[(a) & (NSIN - 1)]
#define COS(a) sintab[((a) + NSIN / 4) & (NSIN - 1)]

/* turn shape src (n points about the origin) by angle a, scale it by
 * s/64 and put it at (cx, cy) */
static void place(STDL_Point *out, const STDL_Point *src, int n, int a,
                  int s, int cx, int cy)
{
    const int c = COS(a), sn = SIN(a);
    int i;

    for (i = 0; i < n; i++) {
        const int x = STDL_MulS16(src[i].x, s) >> 6;
        const int y = STDL_MulS16(src[i].y, s) >> 6;

        out[i].x = (int16_t)(cx + ((STDL_MulS16(x, c)
                                    - STDL_MulS16(y, sn)) >> 14));
        out[i].y = (int16_t)(cy + ((STDL_MulS16(x, sn)
                                    + STDL_MulS16(y, c)) >> 14));
    }
}

int main(int argc, char *argv[])
{
    static const STDL_Colour pal[8] = {
        { 0, 0, 0, 0 }, { 32, 64, 224, 0 }, { 224, 48, 48, 0 },
        { 224, 96, 224, 0 }, { 48, 192, 64, 0 }, { 64, 224, 224, 0 },
        { 240, 224, 64, 0 }, { 255, 255, 255, 0 }
    };
    /* a square, a hexagon, and the five points of a star's triangles */
    static const STDL_Point square[4] = {
        { -40, -40 }, { 40, -40 }, { 40, 40 }, { -40, 40 }
    };
    static const STDL_Point hexagon[6] = {
        { 46, 0 }, { 23, 40 }, { -23, 40 }, { -46, 0 }, { -23, -40 },
        { 23, -40 }
    };
    STDL_Surface *s;
    STDL_Point star[10];
    int star_angle[10];
    int frame = 0, quit = 0, k;

    (void)argc;
    (void)argv;
    s = STDL_SetVideoMode(320, 200, 4, STDL_DOUBLEBUF);
    if (s == NULL) {
        fprintf(stderr, "video: %s\n", STDL_GetError());
        return 1;
    }
    STDL_SetColours(s, pal, 0, 8);
    make_sine();
    for (k = 0; k < 10; k++) {
        star_angle[k] = k * NSIN / 10;      /* once, so not per frame */
    }

    while (!quit) {
        STDL_Event e;
        STDL_Point p[6], tri[3];

        while (STDL_PollEvent(&e)) {
            if (e.type == STDL_KEYDOWN
                && e.key.keysym.sym == STDLK_ESCAPE) {
                quit = 1;
            }
        }
        STDL_FillRect(s, NULL, 0);

        place(p, square, 4, frame * 2, 64 + (SIN(frame) >> 9), 130, 100);
        STDL_FillPolygon(s, p, 4, 1, STDL_DRAW_XOR);
        place(p, hexagon, 6, -frame * 3, 64, 190, 100);
        STDL_FillPolygon(s, p, 6, 2, STDL_DRAW_XOR);

        /* a five-pointed star: its ten corners, points and notches,
         * worked out once, then ten triangles fanned about the centre,
         * each sharing an edge - the centre to a corner - with the
         * next. Drawn one at a time, with no seam between them. */
        for (k = 0; k < 10; k++) {
            const int a = frame + star_angle[k], r = (k & 1) ? 30 : 90;

            star[k].x = (int16_t)(160 + (STDL_MulS16(COS(a), r) >> 14));
            star[k].y = (int16_t)(100 + (STDL_MulS16(SIN(a), r) >> 14));
        }
        tri[0].x = 160;
        tri[0].y = 100;
        for (k = 0; k < 10; k++) {
            tri[1] = star[k];
            tri[2] = star[k < 9 ? k + 1 : 0];
            STDL_FillPolygon(s, tri, 3, 4, STDL_DRAW_XOR);
        }
        STDL_Flip();
        frame++;
    }
    STDL_Quit();
    return 0;
}
