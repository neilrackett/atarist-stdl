/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: CC0-1.0
 *
 * Border overscan demo plus two probe keys, W and X. Not the
 * shipped example: it reaches into library internals, which an
 * example must not, and it exists to answer one question on real
 * hardware.
 *
 * W toggles stdl_ovsc_warm, the bottom ISR's cache pre-touch, on
 * at boot and shown as the third status block (green on, blue
 * off). On a Mega STE with the cache enabled, W off is the
 * behaviour that flickered the bottom border. Compare W on against
 * W off in one sitting and nothing else: a Mega STE's timings move
 * with code layout, so this binary against another one is not a
 * comparison, however tempting the two numbers look side by side.
 *
 * The strip's bar is a histogram, not a counter. With diag set the
 * ISR counts polls from its 50Hz write until the counter leaves
 * its parked value, which says how far before line 263's boundary
 * the restore landed; this draws sixteen columns, one per count,
 * each a doubling taller per frame in it. One tall column is a
 * border doing the same thing every frame. Two or more is a border
 * whose timing moves, which is what a cache-residency problem
 * looks like and what the eye reports as flicker. The shape is the
 * reading, so it photographs.
 *
 * A histogram and not a counter for two reasons. The count is
 * coarse per frame - a poll is a dozen-odd cycles and ordinary
 * interrupt jitter moves it - so a "frames that differed" tally
 * saturates on noise: it read seven distinct values on a perfectly
 * healthy emulated STE. And STDL_OverscanMisses() cannot see this
 * at all, a slipped frame reading exactly like a good one to it,
 * which is how a port once ran with 37% of its frames cut short
 * and nothing to show for it.
 *
 * X puts back the first version's pulse shape, the known-bad
 * placement, so the strip can be seen to change before anything is
 * concluded from it. A metric that has only ever read "fine" is
 * not evidence.
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
 * D toggles double buffering and 8 cycles a Mega STE's CPU through
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

#include "ovbuild.h"             /* generated; OVWARM_BUILD */

/* library internals; see the note above */
extern uint8_t stdl_ovsc_warm, stdl_ovsc_diag, stdl_ovsc_dpolls;
extern uint8_t stdl_ovsc_wide;

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

/*
 * A 3x5 hex font, and the build id drawn with it.
 *
 * The point is attribution, not legibility: several of these
 * probes have gone to the same filename on the same desktop, and a
 * result read off the screen has to say which binary produced it.
 * A console line would not survive the video mode being set, and
 * the file name cannot carry it - GEMDOS gives eight characters
 * and they are spent. So the commit is drawn on the screen the
 * reading is taken from, and a photograph carries its own
 * provenance.
 *
 * The id is compiled in from a generated header rather than a -D:
 * a changed -D does not trigger a rebuild, so a -D stamp goes
 * stale exactly when it matters and then prints the wrong commit
 * with confidence. The header is rewritten only when the value
 * changes, so a no-op build stays a no-op.
 */
static const uint8_t hexfont[16][5] = {
    { 7,5,5,5,7 }, { 2,6,2,2,7 }, { 7,1,7,4,7 }, { 7,1,7,1,7 },
    { 5,5,7,1,1 }, { 7,4,7,1,7 }, { 7,4,7,5,7 }, { 7,1,1,1,1 },
    { 7,5,7,5,7 }, { 7,5,7,1,7 }, { 7,5,7,5,5 }, { 4,4,7,5,7 },
    { 7,4,4,4,7 }, { 1,1,7,5,7 }, { 7,4,7,4,7 }, { 7,4,7,4,4 }
};

static void draw_build(STDL_Surface *screen)
{
    const char *p = OVWARM_BUILD;
    STDL_Rect r;
    int x = 130;

    for (; *p != '\0' && x < screen->w - 4; p++) {
        int d = -1;

        if (*p >= '0' && *p <= '9') {
            d = *p - '0';
        } else if (*p >= 'a' && *p <= 'f') {
            d = *p - 'a' + 10;
        }
        if (d < 0) {
            /* anything else - the '-dirty' marker above all - is a
             * red block, because a modified tree is the one case
             * where the commit alone would mislead */
            r.x = (int16_t)x; r.y = (int16_t)(STAT_Y + 1);
            r.w = 3; r.h = 5;
            STDL_FillRect(screen, &r, 9);
            x += 5;
            continue;
        }
        for (r.h = 1, r.w = 1; ; ) {
            int row, col;

            for (row = 0; row < 5; row++) {
                for (col = 0; col < 3; col++) {
                    if (hexfont[d][row] & (4 >> col)) {
                        r.x = (int16_t)(x + col);
                        r.y = (int16_t)(STAT_Y + 1 + row);
                        STDL_FillRect(screen, &r, 15);
                    }
                }
            }
            break;
        }
        x += 4;
    }
}

/* the state blocks and an empty bar: on a key press only */
static void status_reset(STDL_Surface *screen, int spd, int dbuf)
{
    STDL_Rect r;

    r.x = 8; r.y = STAT_Y; r.w = (uint16_t)(screen->w - 16); r.h = 8;
    STDL_FillRect(screen, &r, 0);

    r.x = 12; r.w = 8; r.h = 8;
    STDL_FillRect(screen, &r, speeds[spd].col);
    draw_build(screen);
    r.x = 24;
    STDL_FillRect(screen, &r, (uint8_t)(dbuf ? 10 : 8));
    r.x = 36;
    STDL_FillRect(screen, &r, (uint8_t)(stdl_ovsc_warm ? 10 : 12));
    if (stdl_ovsc_wide) {
        r.x = 48; r.w = 2;
        STDL_FillRect(screen, &r, 9);
    }
}

/* a column's height: a doubling per pixel, so one tall column and
 * one short one are both still visible after thousands of frames */
static uint8_t bar_h(uint16_t n)
{
    uint8_t h = 0;

    while (n != 0 && h < 8) {
        h++;
        n = (uint16_t)(n >> 1);
    }
    return h;
}

/* one column of the histogram, cleared and redrawn */
static void status_col(STDL_Surface *screen, int b, uint8_t h)
{
    STDL_Rect r;

    r.x = (int16_t)(56 + b * 4); r.w = 3;
    r.y = STAT_Y; r.h = 8;
    STDL_FillRect(screen, &r, 0);
    if (h != 0) {
        r.y = (int16_t)(STAT_Y + 8 - h); r.h = h;
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
    uint16_t hist[16];
    uint8_t  drawn[2][16];
    int      k;

    (void)argc; (void)argv;
    if (STDL_Init(STDL_INIT_VIDEO) < 0) {
        return 1;
    }
    screen = STDL_SetVideoMode(320, 200, 4, 0);
    if (screen == NULL) {
        STDL_Quit();
        return 1;
    }

    stdl_ovsc_diag = 1;                 /* the ISR then keeps dpolls */
    for (k = 0; k < 16; k++) {
        hist[k] = 0;
        drawn[0][k] = drawn[1][k] = 0;
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
                } else if (sym == STDLK_w || sym == STDLK_x) {
                    /* close and reopen: the flags are read by the
                     * ISR every frame, but a reopen also re-runs
                     * the calibration, so each state gets a table
                     * measured under its own conditions */
                    int wt = top, wb = bot;
                    close_borders();
                    if (sym == STDLK_w) {
                        stdl_ovsc_warm = (uint8_t)!stdl_ovsc_warm;
                    } else {
                        stdl_ovsc_wide = (uint8_t)!stdl_ovsc_wide;
                    }
                    top = wt ? (STDL_OpenTopBorder() != 0) : 0;
                    bot = wb ? (STDL_OpenBottomBorder() != 0) : 0;
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
                for (k = 0; k < 16; k++) {
                    hist[k] = 0;
                    drawn[0][k] = drawn[1][k] = 0;
                }
                page = 0;
                if (strip) {
                    status_reset(screen, spd, dbuf);
                    if (dbuf) {
                        STDL_Flip();
                        status_reset(screen, spd, dbuf);
                        STDL_Flip();
                    }
                }
            }
        }
        if (bot) {
            int b = stdl_ovsc_dpolls < 16 ? stdl_ovsc_dpolls : 15;
            if (hist[b] < 0xffff) {
                hist[b]++;
            }
        }
        if (strip) {
            /* one column at a time, and only when its height has
             * actually changed - eight redraws per column over a
             * whole run, so the instrument stays out of the way of
             * the thing it is measuring */
            int b;
            for (b = 0; b < 16; b++) {
                uint8_t h = bar_h(hist[b]);
                if (h != drawn[page][b]) {
                    status_col(screen, b, h);
                    drawn[page][b] = h;
                }
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
