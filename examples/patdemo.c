/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: CC0-1.0
 *
 * STDL example program, dedicated to the public domain so it can
 * be used as a starting point without licence concerns.
 */
/*
 * patdemo - pattern-filled polygons with sub-pixel vertices: ordered
 * dither shading and screen-door transparency, PICO-8's fillp.
 *
 * A wheel of sixteen triangles turns slowly about the centre, each
 * shaded with one of the seventeen 4x4 Bayer dither levels between
 * two colours - the way a 16-colour game shades a lit face without
 * spending palette entries on it. The vertices are 12.4 fixed point
 * (STDL_PointSub), so the wheel turns by fractions of a pixel a frame
 * and the edges creep rather than jump; neighbouring triangles share
 * edges and tile with no seam, dither or not. Over it, a square
 * drifts a quarter of a pixel a frame, filled with a transparent
 * pattern: its set bits leave the wheel showing through, a screen
 * door.
 *
 * The patterns are built once with STDL_MakePattern and kept; the
 * fill only reads them. The rotation is 16-bit fixed point through
 * STDL_MulS16, as in polydemo. Double-buffered, cleared each frame.
 * ESC quits.
 */

#include <stdio.h>
#include <stdl/stdl.h>

#define NSIN 256                /* a full turn */
#define NTRI 16                 /* triangles in the wheel */

static int16_t sintab[NSIN];

/* a sine table in Q14 without a float, as polydemo builds it */
static void make_sine(void)
{
    const int c2 = 32758;       /* 2 cos(2pi/256) in Q14 */
    int i;

    sintab[0] = 0;
    sintab[1] = 402;
    for (i = 2; i < NSIN; i++) {
        sintab[i] = (int16_t)((STDL_MulS16(c2, sintab[i - 1]) >> 14)
                              - sintab[i - 2]);
    }
}

/* sine at a sixteenth of a table step, linearly between entries:
 * at the wheel's rim one table step is over two pixels, so whole
 * steps would move the vertices in jumps and throw the sub-pixel
 * precision away */
static int fsin(int a16)
{
    const int i = (a16 >> 4) & (NSIN - 1), f = a16 & 15;
    const int s0 = sintab[i], s1 = sintab[(i + 1) & (NSIN - 1)];

    return s0 + (STDL_MulS16(s1 - s0, f) >> 4);
}

#define FSIN(a) fsin(a)
#define FCOS(a) fsin((a) + 16 * NSIN / 4)

/* The 4x4 Bayer matrix: dither level k sets the pixels whose entry
 * is below k, so each level adds one pixel to the last and the
 * seventeen levels run from all col0 to all col1. Bit 15 of the
 * pattern is the top-left pixel, row-major. */
static uint16_t bayer_bits(int level)
{
    static const uint8_t bayer[16] = {
        0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5
    };
    uint16_t bits = 0;
    int i;

    for (i = 0; i < 16; i++) {
        if (bayer[i] < level) {
            bits |= (uint16_t)(0x8000u >> i);
        }
    }
    return bits;
}

int main(int argc, char *argv[])
{
    static const STDL_Colour pal[8] = {
        { 0, 0, 0, 0 }, { 24, 32, 96, 0 }, { 64, 160, 240, 0 },
        { 240, 200, 64, 0 }, { 160, 48, 32, 0 }, { 255, 255, 255, 0 },
        { 32, 32, 32, 0 }, { 96, 224, 96, 0 }
    };
    STDL_Pattern shade[NTRI], door;
    STDL_Surface *s;
    int angle = 0, door_x = 40 * 16, door_dx = 4, bob = 0, quit = 0, k;

    (void)argc;
    (void)argv;
    s = STDL_SetVideoMode(320, 200, 4, STDL_DOUBLEBUF);
    if (s == NULL) {
        fprintf(stderr, "video: %s\n", STDL_GetError());
        return 1;
    }
    STDL_SetColours(s, pal, 0, 8);
    make_sine();
    /* once, at start-up: the fill reads the patterns, never builds
     * them. Levels 1 to 16 between dark blue and sky blue, the last
     * solid, so the wheel runs from nearly dark to light */
    for (k = 0; k < NTRI; k++) {
        STDL_MakePattern(&shade[k], 1, 2, bayer_bits(k + 1), 0);
    }
    /* a checkerboard whose set pixels are left alone: half the wheel
     * shows through the square */
    STDL_MakePattern(&door, 3, 0, 0x5A5A, 1);

    while (!quit) {
        STDL_Event e;
        STDL_PointSub tri[3], sq[4];

        while (STDL_PollEvent(&e)) {
            if (e.type == STDL_KEYDOWN
                && e.key.keysym.sym == STDLK_ESCAPE) {
                quit = 1;
            }
        }
        STDL_FillRect(s, NULL, 0);

        /* the wheel: the angle is in sixteenths of a table step, a
         * quarter step a frame - about half a pixel at the rim - and
         * the centre and radius are 12.4 too. Each triangle's two rim
         * corners are the next one's, so the edges are shared exactly
         * and the wheel shows no seam. */
        tri[0].x = 160 * 16;
        tri[0].y = 100 * 16;
        for (k = 0; k < NTRI; k++) {
            const int a0 = angle + k * 16 * NSIN / NTRI;
            const int a1 = a0 + 16 * NSIN / NTRI;
            const int r = 90 * 16;

            tri[1].x = (int16_t)(160 * 16 + (STDL_MulS16(FCOS(a0), r) >> 14));
            tri[1].y = (int16_t)(100 * 16 + (STDL_MulS16(FSIN(a0), r) >> 14));
            tri[2].x = (int16_t)(160 * 16 + (STDL_MulS16(FCOS(a1), r) >> 14));
            tri[2].y = (int16_t)(100 * 16 + (STDL_MulS16(FSIN(a1), r) >> 14));
            STDL_FillPolygonPattern(s, tri, 3, &shade[k]);
        }

        /* the screen door: 4 sub-pixel units a frame is a quarter of
         * a pixel, back and forth across the wheel */
        {
            const int x = door_x, y = 60 * 16 + (FSIN(bob) >> 6);

            sq[0].x = (int16_t)x;           sq[0].y = (int16_t)y;
            sq[1].x = (int16_t)(x + 640);   sq[1].y = (int16_t)y;
            sq[2].x = (int16_t)(x + 640);   sq[2].y = (int16_t)(y + 640);
            sq[3].x = (int16_t)x;           sq[3].y = (int16_t)(y + 640);
            STDL_FillPolygonPattern(s, sq, 4, &door);
        }
        STDL_Flip();
        angle += 4;
        bob += 16;
        door_x += door_dx;
        if (door_x <= 40 * 16 || door_x >= 240 * 16) {
            door_dx = -door_dx;
        }
    }
    STDL_Quit();
    return 0;
}
