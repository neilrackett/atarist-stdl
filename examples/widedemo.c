/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: CC0-1.0
 *
 * STDL example program, dedicated to the public domain so it can
 * be used as a starting point without licence concerns.
 */
/*
 * widedemo - 640x480 in 16 colours on a TT or a Falcon
 * (STDL_RESPONSIVE_WIDE, stdl_responsive.h).
 *
 * It asks for the wide screen and branches on what it got, never on
 * the machine: 640x480 on a TT (TT Medium) or a Falcon (640x400 on an
 * RGB monitor or a TV that cannot open to 480), an ST's 320x200
 * elsewhere. The background - a frame round the very edge, a grid and
 * the sixteen colours - is drawn once into a surface the screen's size,
 * each page is given it whole once (STDL_DirtyBackground), and each
 * frame puts back only what the balls covered on the page it draws
 * (STDL_Dirty keeps a list a page), then draws them. That is the
 * pattern the screen needs: 640x480 is 4.8 times an ST's pixels and a
 * TT has no BLiTTER, so a frame that redraws the whole screen costs
 * more than a frame has. Three pages, so a frame starts while the last
 * waits for the display. The mouse moves a box over the whole screen.
 * ESC quits, printing the frames drawn a second.
 */

#include <stdio.h>
#include <stdl/stdl.h>
#include "hold.h"

#define BALLS 24
#define BALL  24

static STDL_Rect rect(int x, int y, int w, int h)
{
    STDL_Rect r;

    r.x = (int16_t)x; r.y = (int16_t)y;
    r.w = (uint16_t)w; r.h = (uint16_t)h;
    return r;
}

/* the background, once: a frame on the very edge shows every row and
 * column there is */
static void draw_background(STDL_Surface *bg)
{
    STDL_Rect r;
    int i, x, y;
    const int cw = bg->w / 16;

    r = rect(0, 0, bg->w, bg->h);
    STDL_FillRect(bg, &r, 1);
    for (x = 0; x < bg->w; x += 32) {
        STDL_VLine(bg, x, 0, bg->h - 1, 2);
    }
    for (y = 0; y < bg->h; y += 32) {
        STDL_HLine(bg, 0, bg->w - 1, y, 2);
    }
    /* the sixteen colours along the bottom */
    for (i = 0; i < 16; i++) {
        r = rect(i * cw, bg->h - 24, cw, 16);
        STDL_FillRect(bg, &r, (uint8_t)i);
    }
    STDL_HLine(bg, 0, bg->w - 1, 0, 15);
    STDL_HLine(bg, 0, bg->w - 1, bg->h - 1, 15);
    STDL_VLine(bg, 0, 0, bg->h - 1, 15);
    STDL_VLine(bg, bg->w - 1, 0, bg->h - 1, 15);
}

int main(int argc, char *argv[])
{
    STDL_Surface *s, *bg, *art;
    STDL_Sprite *spr;
    STDL_Event e;
    int bx[BALLS], by[BALLS], dx[BALLS], dy[BALLS];
    int i, w, h, mx, my, quit = 0;
    uint32_t t0, frames = 0;

    (void)argc;
    (void)argv;
    hold_on_exit(NULL);
    STDL_UseResponsive(STDL_RESPONSIVE_WIDE);
    s = STDL_SetVideoMode(640, 480, 4, STDL_TRIPLEBUF);
    if (s == NULL) {
        fprintf(stderr, "video: %s\n", STDL_GetError());
        return 1;
    }
    w = s->w;
    h = s->h;
    fprintf(stderr, "widedemo: %dx%d%s%s\n", w, h,
            w < 640 ? ": " : "", w < 640 ? STDL_GetError() : "");

    bg = STDL_CreateSurface(w, h);
    art = STDL_CreateSurface(BALL, BALL);
    if (bg == NULL || art == NULL) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    draw_background(bg);
    /* a ball: rings of colours 8-14 round a white core, 0 the key */
    STDL_FillRect(art, NULL, 0);
    for (i = BALL / 2; i > 0; i -= 2) {
        STDL_FillCircle(art, BALL / 2, BALL / 2, i - 1,
                        (uint8_t)(i > 2 ? 8 + (i >> 1) % 7 : 15));
    }
    STDL_SetColourKey(art, 1, 0);
    spr = STDL_SpriteFromSurface(art, BALL, STDL_PRESHIFT);
    if (spr == NULL) {
        spr = STDL_SpriteFromSurface(art, BALL, 0);
    }
    if (spr == NULL || STDL_DirtyInit(bg, BALLS + 4) < 0) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    /* each page gets the whole background at its first restore, and
     * from then only what was drawn over it */
    STDL_DirtyBackground(NULL);
    for (i = 0; i < BALLS; i++) {
        bx[i] = (i * 97) % (w - BALL);
        by[i] = (i * 61) % (h - BALL - 24);
        dx[i] = (i & 1) ? 3 : -2;
        dy[i] = (i & 2) ? 2 : -3;
    }

    t0 = STDL_GetTicks();
    while (!quit) {
        STDL_Rect r;

        while (STDL_PollEvent(&e)) {
            if (e.type == STDL_QUIT || (e.type == STDL_KEYDOWN
                                        && e.key.keysym.sym == STDLK_ESCAPE)) {
                quit = 1;
            }
        }
        /* what the balls covered on this page, from the background */
        STDL_DirtyRestore(s);
        for (i = 0; i < BALLS; i++) {
            bx[i] += dx[i];
            by[i] += dy[i];
            if (bx[i] < 0 || bx[i] > w - BALL) {
                dx[i] = -dx[i];
                bx[i] += 2 * dx[i];
            }
            if (by[i] < 0 || by[i] > h - BALL) {
                dy[i] = -dy[i];
                by[i] += 2 * dy[i];
            }
            STDL_BlitSprite(spr, 0, s, bx[i], by[i]);
            r = rect(bx[i], by[i], BALL, BALL);
            STDL_DirtyPush(&r);
        }
        /* the mouse's box, reaching every edge of the screen */
        STDL_GetMouseState(&mx, &my);
        r = rect(mx - 4, my - 4, 9, 9);
        STDL_FillRect(s, &r, 15);
        STDL_DirtyPush(&r);
        STDL_Flip();
        frames++;
    }
    t0 = STDL_GetTicks() - t0;
    STDL_DirtyQuit();
    STDL_FreeSprite(spr);
    STDL_FreeSurface(art);
    STDL_FreeSurface(bg);
    STDL_Quit();
    printf("widedemo: %lu frames in %lums, %lu.%lu a second\n",
           (unsigned long)frames, (unsigned long)t0,
           t0 ? (unsigned long)(frames * 1000UL / t0) : 0UL,
           t0 ? (unsigned long)((frames * 10000UL / t0) % 10) : 0UL);
    return 0;
}
