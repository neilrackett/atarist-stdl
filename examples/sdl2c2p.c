/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: CC0-1.0
 *
 * STDL example program, dedicated to the public domain so it can
 * be used as a starting point without licence concerns.
 */
/*
 * sdl2c2p - a program's own chunky screen, converted by the SDL2
 * add-on's conversion alone (SDL_stdl_c2p.h): no SDL.h, no renderer.
 *
 * The pattern for a port that draws a byte a pixel itself and puts the
 * picture on a TT's or a Falcon's 256-colour screen, as many engines
 * do: STDL's responsive screen (stdl_responsive.h) gives the page, and
 * one call a frame converts the box the program drew. The program is
 * built for the 68000, as a port that also runs on an ST is, and
 * converts only once the screen it got has 8 planes.
 *
 * The picture is drawn at half the width, every pixel twice, as a
 * game's low detail is, so it can go through the pair loops: P
 * switches between STDL_SDL2_ConvertBox and STDL_SDL2_ConvertBoxPairs,
 * and the bar under the picture is the conversion's time, 4 pixels a
 * millisecond, a tick every 10. One call for the whole box is what
 * makes pairs pay: called a row at a time they measured no faster
 * (docs/sdl2.md). The picture scrolls by an even number of pixels a
 * frame, so its pairs stay pairs; the source is a wider buffer read in
 * place, its pitch the buffer's. ESC quits, and the average of each
 * way goes to the console.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdl/stdl.h>
#include <SDL2/SDL_stdl_c2p.h>
#include "hold.h"

#define VIEW_W   320             /* the box converted: 20 groups */
#define VIEW_H   200
#define WORLD_W  (2 * VIEW_W)    /* the buffer it scrolls over */
#define BAR_Y    216

static uint8_t world[WORLD_W * VIEW_H];

/* a triangle wave, 0-255 over 256 steps */
static unsigned tri(unsigned t)
{
    t &= 255;
    return t < 128 ? t * 2 : (255 - t) * 2;
}

static STDL_Rect rect(int x, int y, int w, int h)
{
    STDL_Rect r;

    r.x = (int16_t)x; r.y = (int16_t)y;
    r.w = (uint16_t)w; r.h = (uint16_t)h;
    return r;
}

int main(int argc, char *argv[])
{
    static STDL_Colour colours[256];
    STDL_Surface *s;
    uint32_t sum[2] = { 0, 0 }, n[2] = { 0, 0 };
    int pairs = 1, quit = 0, two_lines, frame = 0, i, x, y;
    long line;

    (void)argc;
    (void)argv;
    hold_on_exit(NULL);
    STDL_UseResponsive(STDL_RESPONSIVE_COLOUR | STDL_RESPONSIVE_HEIGHT);
    s = STDL_SetVideoMode(320, 240, 8, STDL_DOUBLEBUF);
    if (s == NULL || s->format->BitsPerPixel != 8) {
        STDL_Quit();
        fprintf(stderr, "sdl2c2p: needs a TT's or a Falcon's 256 colours\n");
        return 1;
    }
    /* A row of the page is s->pitch bytes: on a TT two lines of TT
     * Low, which the conversion writes both of (two_lines), so Flip
     * has nothing to copy; on a Falcon one line, 320 or (on RGB) 384
     * pixels, s->pixels already at the picture's first */
    two_lines = s->pitch == 2 * VIEW_W;
    line = two_lines ? s->pitch / 2 : s->pitch;

    /* colours 1-255 a wheel of hues, 0 black for the border */
    for (i = 1; i < 256; i++) {
        colours[i].r = (uint8_t)tri((unsigned)i);
        colours[i].g = (uint8_t)tri((unsigned)i + 85);
        colours[i].b = (uint8_t)tri((unsigned)i + 170);
    }
    STDL_SetColours(s, colours, 0, 256);

    /* the picture at half the width: each value written to a pair, the
     * mean of three waves (each 254 at most, so 1 + v is a colour of
     * the wheel). A sum of 762 at most divided by 3 is the 68000's
     * 16-bit multiply and a shift, exact over that range, where / 3
     * would be a libgcc call a pixel. */
    for (y = 0; y < VIEW_H; y++) {
        const unsigned ty = tri((unsigned)y * 2);
        for (x = 0; x < WORLD_W; x += 2) {
            const unsigned h = (unsigned)x / 2;
            const unsigned v = (unsigned)(STDL_MulU16(tri(h * 3) + ty +
                                          tri((h + (unsigned)y) * 2),
                                          683) >> 11);
            world[y * WORLD_W + x] = world[y * WORLD_W + x + 1] =
                (uint8_t)(1 + v);
        }
    }

    while (!quit) {
        STDL_Event e;
        STDL_Rect r;
        uint32_t t0, t;
        /* an even step, so the pixels stay in pairs */
        const uint8_t *src = world + (frame * 2) % (WORLD_W - VIEW_W);

        while (STDL_PollEvent(&e)) {
            if (e.type == STDL_KEYDOWN) {
                if (e.key.keysym.sym == STDLK_ESCAPE) {
                    quit = 1;
                } else if (e.key.keysym.sym == STDLK_p) {
                    pairs = !pairs;
                }
            }
        }

        /* the whole box in one call: 20 groups by 200 rows */
        t0 = STDL_GetHz38400();
        if (pairs) {
            STDL_SDL2_ConvertBoxPairs(src, WORLD_W, s->pixels, line,
                                      VIEW_W / 16, VIEW_H, two_lines, 0);
        } else {
            STDL_SDL2_ConvertBox(src, WORLD_W, s->pixels, line,
                                 VIEW_W / 16, VIEW_H, two_lines, 0);
        }
        t = STDL_GetHz38400() - t0;
        sum[pairs] += t;
        n[pairs]++;

        /* the bar, drawn by STDL (which Flip doubles on a TT): 38,400
         * ticks a second, so 4 pixels a millisecond is t / 9.6 */
        r = rect(0, BAR_Y, 320, 8);
        STDL_FillRect(s, &r, 0);
        r = rect(0, BAR_Y, (int)(t * 10 / 96 > 320 ? 320 : t * 10 / 96), 8);
        STDL_FillRect(s, &r, (uint8_t)(pairs ? 96 : 200));
        for (x = 0; x < 320; x += 40) {
            r = rect(x, BAR_Y + 8, 1, 4);
            STDL_FillRect(s, &r, 128);
        }
        STDL_Flip();
        frame++;
    }
    STDL_Quit();
    for (i = 0; i < 2; i++) {
        if (n[i]) {
            /* 38,400 ticks a second: ms * 100 = ticks * 1000 / 384 */
            const unsigned long c = (unsigned long)(sum[i] / n[i]) * 1000UL
                                    / 384UL;
            printf("sdl2c2p: %s %lu.%02lums a frame (%lu frames)\n",
                   i ? "as pairs" : "whole", c / 100, c % 100,
                   (unsigned long)n[i]);
        }
    }
    return 0;
}
