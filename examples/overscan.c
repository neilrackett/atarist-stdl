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
 *
 * M turns IKBD mouse reporting off and on: moving a mouse with a
 * border open feeds three ACIA interrupts per movement into a
 * frame whose flick has to land on an exact cycle, and a game that
 * never reads the mouse is paying for all of it. D toggles double
 * buffering and 8 cycles a Mega STE's CPU through
 * 16MHz with its cache, 16MHz without it and 8MHz - the middle one
 * being the setting that says whether a timing problem is the clock
 * or the cache.
 *
 * Across the middle is a status strip: two state blocks (the CPU
 * setting as green, yellow or blue for those three; double
 * buffering) and a bar of one segment per missed timing window
 * since the last key press. A border that is open and steady
 * leaves the bar empty; a border that flickers fills it. It is
 * there because a miss is a single wrong frame, which the eye
 * catches but cannot count.
 *
 * It draws nothing while the bar is not growing, which matters more
 * than it sounds. Drawing with a border open is placed against the
 * beam, and a first version repainted the whole strip every frame -
 * on a real Mega STE at 16MHz that alone flickered the bottom
 * border, on a screen that was rock solid when nothing drew. So the
 * clean case here is a genuinely static screen, each new segment
 * costs one small fill once, and S turns the strip off altogether:
 * the control for "is it my drawing or the border" is a keypress
 * inside one binary, which is the only honest way to compare on a
 * machine whose timings move with code layout.
 *
 * The flip is not part of that. With D on the pages swap every
 * frame whether or not anything is drawn, because a page swap
 * inside the border's timing windows is the feature under test and
 * it costs a register write, not a blit. Each page keeps its own
 * count of the segments already on it, so a growing bar still only
 * ever draws the new ones.
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

/*
 * The status strip: two blocks and a miss bar, on the row through
 * the middle of the picture so it is visible in every mode. Drawn
 * every frame, so it is the only part of the scene that has to be
 * cheap - hence rectangles rather than a font, which would mean
 * the example loading an asset before it could say anything.
 */
#define STAT_Y    104
#define STAT_MAX  32            /* bar segments; 4px each */

/*
 * The three CPU settings 8 cycles through, fastest first, and the
 * colour each shows in the first status block. Mode 2 is on the
 * list because it is the interesting one: 16MHz with the cache off
 * is a 16MHz CPU still fetching over an 8MHz bus, which on paper
 * and in the library's own measurements is worth almost nothing
 * over 8MHz - the cache is the speedup. It is also the setting
 * that separates "the clock broke my timing" from "the cache broke
 * my timing" on a machine, which no amount of emulator time will.
 */
static const struct { int mode; uint8_t col; } speeds[3] = {
    { 1, 10 },                  /* 16MHz, cache on  - green  */
    { 2, 11 },                  /* 16MHz, cache off - yellow */
    { 0, 12 }                   /* 8MHz             - blue   */
};

/* the state blocks and an empty bar: on a key press only */
static void status_reset(STDL_Surface *screen, int spd, int dbuf,
                         int mouse)
{
    STDL_Rect r;

    r.x = 8; r.y = STAT_Y; r.w = (uint16_t)(screen->w - 16); r.h = 8;
    STDL_FillRect(screen, &r, 0);

    r.x = 12; r.w = 8; r.h = 8;
    STDL_FillRect(screen, &r, speeds[spd].col);
    r.x = 24;
    STDL_FillRect(screen, &r, (uint8_t)(dbuf ? 10 : 8));
    r.x = 36;
    STDL_FillRect(screen, &r, (uint8_t)(mouse ? 10 : 12));
}

/* one fill per new segment, nothing at all while the count holds */
static void status_bar(STDL_Surface *screen, uint32_t from, uint32_t to)
{
    STDL_Rect r;
    uint32_t i;

    if (to > (uint32_t)STAT_MAX) {
        to = (uint32_t)STAT_MAX;
    }
    r.y = STAT_Y; r.w = 3; r.h = 8;
    for (i = from; i < to; i++) {
        r.x = (int16_t)(40 + i * 4);
        STDL_FillRect(screen, &r, 9);
    }
}

static void close_borders(void)
{
    STDL_CloseTopBorder();
    STDL_CloseBottomBorder();
}

int main(int argc, char *argv[])
{
    STDL_Surface *screen;
    int top = 0, bot = 0, dbuf = 0, spd = 0, strip = 1, page = 0;
    int mouse = 1;
    uint32_t base = 0, shown[2] = { 0, 0 };

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
                     * Mega STE CPU speed, cycling 16MHz+cache,
                     * 16MHz without it, 8MHz. The borders are
                     * closed around it: opening one measures the
                     * flick's own instruction costs on the machine,
                     * so a calibration taken at 16MHz is wrong at 8
                     * and the other way about, and the library only
                     * re-measures on an open. A border left open
                     * across the change keeps the table it was
                     * given, so any port changing speed mid-run
                     * wants this bracket. STDL_Init takes the
                     * 16MHz+cache mode, so this is also how you get
                     * a Mega STE to behave like a plain STE.
                     */
                    int wt = top, wb = bot;
                    close_borders();
                    spd = (spd + 1) % 3;
                    STDL_UseMegaSteSpeedup(speeds[spd].mode);
                    top = wt ? (STDL_OpenTopBorder() != 0) : 0;
                    bot = wb ? (STDL_OpenBottomBorder() != 0) : 0;
                } else if (sym == STDLK_m) {
                    /*
                     * Mouse reporting, which a border makes worth
                     * knowing about: every movement is three ACIA
                     * interrupts about 1.3ms apart, and although
                     * they cannot preempt the border's timers they
                     * can delay them, which a flick that has to
                     * land on an exact cycle will not survive.
                     * Move the mouse with a border open and watch
                     * the bar, then turn the mouse off and do it
                     * again. A game that never reads the mouse
                     * should turn it off once at startup.
                     */
                    mouse = !mouse;
                    STDL_EnableMouse(mouse);
                } else if (sym == STDLK_s) {
                    /* the aimed control: no drawing at all while
                     * the border runs, which is the state the
                     * borders were first proved in */
                    strip = !strip;
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
                base = STDL_OverscanMisses();
                shown[0] = shown[1] = 0;
                page = 0;
                if (strip) {
                    status_reset(screen, spd, dbuf, mouse);
                    if (dbuf) {
                        STDL_Flip();
                        status_reset(screen, spd, dbuf, mouse);
                        STDL_Flip();
                    }
                }
            }
        }
        if (strip) {
            uint32_t n = STDL_OverscanMisses() - base;
            if (n > shown[page]) {
                status_bar(screen, shown[page], n);
                shown[page] = n;
            }
        }
        if (dbuf) {
            STDL_Flip();
            page ^= 1;
        } else {
            STDL_WaitVBL();
        }
    }
done:
    close_borders();
    STDL_Quit();
    return 0;
}
