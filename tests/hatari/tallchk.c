/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * A responsive tall screen (stdl_responsive.h) on whatever machine it
 * runs on: STDL_RESPONSIVE_TALL, and with -DTALLCOL the colour as
 * well. It sets 320x240, draws a test card on both pages - a frame
 * round the very edge, a block in each corner, a band every 40 rows,
 * rows 200-239 striped, and with the colour all 256 in a 16x16 grid -
 * shows each page for a second and a half, and prints what it got:
 *
 *   TC:start                 before the mode, for a script's timing
 *   TC:screen 320x240 planes=4 bpp=4 stride=160
 *   TC:done
 *
 * Build by hand (stems of eight characters, for Hatari's 8.3 drive):
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Iinclude -Ilib/xpad/src -o dist/TALLCHK.TOS \
 *       tests/hatari/tallchk.c libstdl.a
 *   ... -DTALLCOL -o dist/TALLCOL.TOS ...
 *   MACHINE=ste tests/hatari/run.sh tc dist/TALLCHK.TOS 8 \
 *       "waitfor TC:start;sleep 2;shot;sleep 1.5;shot;waitfor TC:done"
 */

#include <stdio.h>
#include <string.h>
#include <stdl/stdl.h>

#ifdef TALLCOL
#define WHAT (STDL_RESPONSIVE_COLOUR | STDL_RESPONSIVE_TALL)
#define BPP  8
#else
#define WHAT STDL_RESPONSIVE_TALL
#define BPP  4
#endif

static STDL_Rect rect(int x, int y, int w, int h)
{
    STDL_Rect r;

    r.x = (int16_t)x; r.y = (int16_t)y;
    r.w = (uint16_t)w; r.h = (uint16_t)h;
    return r;
}

static void card(STDL_Surface *s, int deep, int page)
{
    STDL_Rect r;
    int i, y;

    STDL_FillRect(s, NULL, 0);
    /* a band every 40 rows, its colour its number */
    for (y = 0; y < s->h; y += 40) {
        r = rect(40, y + 16, 240, 8);
        STDL_FillRect(s, &r, (uint8_t)(1 + y / 40));
    }
    /* rows 200 on, striped: what a 200-line screen cannot show */
    for (y = 200; y < s->h; y += 4) {
        STDL_HLine(s, 8, 311, y, 15);
    }
    /* a block in each corner, and the page's number by the top left */
    r = rect(0, 0, 16, 16);
    STDL_FillRect(s, &r, 9);
    r = rect(304, 0, 16, 16);
    STDL_FillRect(s, &r, 10);
    r = rect(0, s->h - 16, 16, 16);
    STDL_FillRect(s, &r, 11);
    r = rect(304, s->h - 16, 16, 16);
    STDL_FillRect(s, &r, 12);
    r = rect(24, 4, 8 + 8 * page, 8);
    STDL_FillRect(s, &r, 14);
    /* the frame round the very edge */
    STDL_HLine(s, 0, s->w - 1, 0, 15);
    STDL_HLine(s, 0, s->w - 1, s->h - 1, 15);
    STDL_VLine(s, 0, 0, s->h - 1, 15);
    STDL_VLine(s, s->w - 1, 0, s->h - 1, 15);
    if (deep) {
        for (i = 0; i < 256; i++) {
            r = rect(96 + (i & 15) * 8, 48 + (i >> 4) * 8, 8, 8);
            STDL_FillRect(s, &r, (uint8_t)i);
        }
    }
}

int main(void)
{
    STDL_Surface *s;
    STDL_Colour c[256];
    char got[96];
    int i, p, deep;
    uint32_t t;

    fprintf(stderr, "TC:start\n");
    if (STDL_Init(STDL_INIT_VIDEO) < 0
        || STDL_UseResponsive(WHAT) < 0) {
        fprintf(stderr, "TC:init %s\nTC:done\n", STDL_GetError());
        return 1;
    }
    s = STDL_SetVideoMode(320, 240, BPP, STDL_DOUBLEBUF);
    if (s == NULL) {
        STDL_Quit();
        fprintf(stderr, "TC:mode %s\nTC:done\n", STDL_GetError());
        return 1;
    }
    deep = s->format->BitsPerPixel == 8;
    snprintf(got, sizeof(got), "TC:screen %dx%d planes=%d bpp=%d stride=%d",
             s->w, s->h, s->planes, s->format->BitsPerPixel, s->stride);

    /* the sixteen: an ST's own spread; the 256: a red-green grid, blue
     * by quarter, so each cell of the swatch is its own */
    for (i = 0; i < 256; i++) {
        c[i].r = (uint8_t)((i & 15) * 17);
        c[i].g = (uint8_t)((i >> 4) * 17);
        c[i].b = (uint8_t)((i & 0xC0) | ((i & 3) << 4));
        c[i].unused = 0;
    }
    c[0].r = c[0].g = c[0].b = 0;
    c[15].r = c[15].g = c[15].b = 255;
    for (i = 1; i < 15; i++) {
        c[i].r = (uint8_t)((i & 1) ? 0xFF : 0x40);
        c[i].g = (uint8_t)((i & 2) ? 0xFF : 0x40);
        c[i].b = (uint8_t)((i & 4) ? 0xFF : 0x40);
    }
    STDL_SetColours(s, c, 0, deep ? 256 : 16);

    for (p = 0; p < 2; p++) {
        card(s, deep, p);
        STDL_Flip();
        t = STDL_GetTicks();
        while (STDL_GetTicks() - t < 1500) {
        }
    }
    STDL_Quit();
    fprintf(stderr, "%s\nTC:done\n", got);
    return 0;
}
