/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: CC0-1.0
 *
 * Border overscan demo: 228, 245 or 273 visible lines on any 50Hz
 * ST.
 *
 * The intended pattern: set the normal video mode, then ask for
 * borders and use whatever height comes back. The screen surface
 * is updated in place, so all drawing code just reads screen->h -
 * the same loop paints 200, 228, 245 or 273 lines. T toggles the
 * top border, B the bottom one; opening both combines them
 * automatically (273 rows) and closing one drops back to the other
 * alone. SPACE closes everything. The ruler makes the extra lines
 * countable, the one-pixel frame proves the first and last lines
 * are really displayed, and the chequered band across the old
 * picture's last line (surface row 200 alone, 227 combined) is
 * there to be looked at closely: the bottom variant once showed a
 * seam there, and a row displayed a cycle early or fetched a word
 * out of step would break the pattern. ESC quits.
 */

#include <stddef.h>
#include <stdl/stdl.h>

static void paint(STDL_Surface *screen)
{
    STDL_Rect r;
    int y;

    r.x = 0; r.y = 0; r.w = (uint16_t)screen->w; r.h = (uint16_t)screen->h;
    STDL_FillRect(screen, &r, 1);                  /* dark ground   */

    /* ruler: a line every 10 rows, brighter every 50 */
    for (y = 0; y < screen->h; y += 10) {
        r.x = 8; r.y = (int16_t)y; r.w = (uint16_t)(screen->w - 16); r.h = 1;
        STDL_FillRect(screen, &r, (uint8_t)((y % 50) ? 3 : 14));
    }

    /* where the stock 200-line screen's top edge sits when the top
     * border is open: everything above is ex-border territory */
    if (screen->h == 228 || screen->h == 273) {
        r.x = 0; r.y = 28;
        r.w = (uint16_t)screen->w; r.h = 2;
        STDL_FillRect(screen, &r, 12);
    }
    /* a chequer across the old picture's last line and its
     * neighbours: every row of it must line up with the next */
    if (screen->h == 245 || screen->h == 273) {
        int x, band = (screen->h == 245) ? 198 : 225;
        for (y = band; y < band + 5; y++) {
            for (x = 0; x < screen->w; x += 4) {
                r.x = (int16_t)x; r.y = (int16_t)y; r.w = 4; r.h = 1;
                STDL_FillRect(screen, &r, (uint8_t)(((x >> 2) + y) & 1 ? 14 : 15));
            }
        }
    }

    /* one-pixel frame on the true first and last lines */
    r.x = 0; r.y = 0; r.w = (uint16_t)screen->w; r.h = 1;
    STDL_FillRect(screen, &r, 15);
    r.y = (int16_t)(screen->h - 1);
    STDL_FillRect(screen, &r, 15);
    r.x = 0; r.y = 0; r.w = 1; r.h = (uint16_t)screen->h;
    STDL_FillRect(screen, &r, 15);
    r.x = (int16_t)(screen->w - 1);
    STDL_FillRect(screen, &r, 15);
}

static void close_borders(void)
{
    STDL_CloseTopBorder();
    STDL_CloseBottomBorder();
}

int main(int argc, char *argv[])
{
    STDL_Surface *screen;
    int top = 0, bot = 0, dbuf = 0, fast = 1;

    (void)argc; (void)argv;
    if (STDL_Init(STDL_INIT_VIDEO) < 0) {
        return 1;
    }
    screen = STDL_SetVideoMode(320, 200, 4, 0);
    if (screen == NULL) {
        STDL_Quit();
        return 1;
    }

    top = STDL_OpenTopBorder() != 0;
    paint(screen);

    for (;;) {
        STDL_Event ev;
        while (STDL_PollEvent(&ev)) {
            if (ev.type == STDL_QUIT) {
                goto done;
            }
            if (ev.type == STDL_KEYDOWN) {
                const int sym = ev.key.keysym.sym;
                if (sym == STDLK_ESCAPE) {
                    goto done;
                }
                if (sym == STDLK_t) {
                    if (top) {
                        STDL_CloseTopBorder();
                        top = 0;
                    } else {
                        top = STDL_OpenTopBorder() != 0;
                    }
                } else if (sym == STDLK_b) {
                    if (bot) {
                        STDL_CloseBottomBorder();
                        bot = 0;
                    } else {
                        bot = STDL_OpenBottomBorder() != 0;
                    }
                } else if (sym == STDLK_SPACE) {
                    close_borders();
                    top = bot = 0;
                } else if (sym == STDLK_8) {
                    /*
                     * Mega STE CPU speed. The borders are closed
                     * around it deliberately: opening one measures
                     * the flick's own instruction costs on the
                     * machine, so a calibration taken at 16MHz is
                     * wrong at 8 and the other way about. Any port
                     * changing speed mid-run wants the same
                     * bracket. STDL_Init takes the 16MHz mode, so
                     * this is how you get a Mega STE to behave like
                     * a plain STE for a test.
                     */
                    int wt = top, wb = bot;
                    close_borders();
                    fast = !fast;
                    STDL_UseMegaSteSpeedup(fast ? 1 : 0);
                    top = wt ? (STDL_OpenTopBorder() != 0) : 0;
                    bot = wb ? (STDL_OpenBottomBorder() != 0) : 0;
                } else if (sym == STDLK_d) {
                    /*
                     * Double buffering, with the borders open.
                     * The video mode has to be set again to get
                     * (or drop) the second page, and that closes
                     * any border, so they are reopened after.
                     * STDL_Flip then swaps two tall pages: the
                     * overscan module owns the video base while a
                     * border is open, so the flip goes through it
                     * rather than through Setscreen.
                     */
                    int wt = top, wb = bot;
                    close_borders();
                    dbuf = !dbuf;
                    screen = STDL_SetVideoMode(320, 200, 4,
                                 dbuf ? STDL_DOUBLEBUF : 0);
                    if (screen == NULL) {
                        goto done;
                    }
                    top = wt ? (STDL_OpenTopBorder() != 0) : 0;
                    bot = wb ? (STDL_OpenBottomBorder() != 0) : 0;
                } else {
                    continue;
                }
                paint(screen);
                if (dbuf) {
                    /* show it: with two pages the one just painted
                     * is not the one on screen until the flip */
                    STDL_Flip();
                    paint(screen);
                    STDL_Flip();
                }
            }
        }
        STDL_WaitVBL();
    }
done:
    close_borders();
    STDL_Quit();
    return 0;
}
