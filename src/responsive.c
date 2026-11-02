/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Responsive screens (stdl_responsive.h): on a TT or a Falcon, the
 * display mode, its pages, its colours and Flip, for a program that
 * asked for more than an ST has.
 *
 * The screen's surfaces are the model's: 8-plane ones for the colour
 * (planes8.c), whose drawing calls widen the box of the page drawn
 * into (resp_touch) so that Flip presents that box alone; or a 4-plane
 * screen for a tall one without it (tall4.c). On a TT each row goes on
 * two lines (TT Low cannot double them itself); on a Falcon the VIDEL
 * doubles them on VGA, and an RGB monitor or a TV has 200 lines, or
 * 240 in overscan, 384 pixels wide with the picture centred. A page's
 * box is what changed on it since it was last shown, as each page keeps
 * its own picture.
 */

#include <stdlib.h>
#include <string.h>
#include "responsive.h"

#ifdef __m68k__
#include <osbind.h>
#include <mint/falcon.h>
#else
/* host tests: the machine the screen is set on, a TT unless these say
 * a Falcon, on VGA or RGB */
int stdl_host_resp_falcon, stdl_host_resp_vga;
#endif

resp_state stdl_rs;

static int resp_wants(int w, int h, int bpp);
static void resp_st_mode(int h);
static void resp_update(int n, const STDL_Rect *rects);
static void resp_fade(const STDL_Palette *target, int frames);

static const stdl_resp_video_t resp_video = {
    resp_wants, resp_set_mode, resp_flip, resp_shutdown, resp_st_mode,
    resp_update, resp_fade
};

int stdl_use_responsive(unsigned what, const struct stdl_resp_model *colour,
                        const struct stdl_resp_model *tall,
                        void (*st_tall)(int h))
{
    if (what & ~(STDL_RESPONSIVE_COLOUR | STDL_RESPONSIVE_TALL
                 | STDL_RESPONSIVE_HEIGHT)) {
        STDL_SetError("unknown responsive option");
        return -1;
    }
    if (stdl_resp_screen != NULL) {
        STDL_SetError("a responsive screen is already set");
        return -1;
    }
    stdl_rs.colour = colour;
    stdl_rs.tall = tall;
    stdl_rs.st_tall = st_tall;
    stdl_resp_video = (colour != NULL || tall != NULL) ? &resp_video : NULL;
    return 0;
}

/* only a TT or a Falcon has its own screen for these; an ST's tall
 * screen is its own, with the bottom border open (resp_st_mode), and
 * the display's own height is its 200 lines */
static int resp_wants(int w, int h, int bpp)
{
    (void)w;
    if (stdl.mach.mch_cookie < 0x00020000UL) {
        return 0;
    }
    return (stdl_rs.colour != NULL && bpp >= 8)
        || (stdl_rs.tall != NULL && h > 200);
}

static void resp_st_mode(int h)
{
    if (stdl_rs.st_tall != NULL) {
        stdl_rs.st_tall(h);
    }
}

/* what a program drew on the screen itself (STDL_UpdateRects, the SDL
 * shim's SDL_UpdateRect): an 8-plane screen's box widens with it; a
 * 4-plane one's Flip finds every change itself */
static void resp_update(int n, const STDL_Rect *rects)
{
    int i;

    if (stdl_rs.model == NULL || stdl_rs.model->planes != 8) {
        return;
    }
    if (rects == NULL || n <= 0) {
        resp_touch(&stdl_rs.screen.s, 0, 0, stdl_rs.w - 1, stdl_rs.h - 1);
        return;
    }
    for (i = 0; i < n; i++) {
        resp_touch(&stdl_rs.screen.s, rects[i].x, rects[i].y,
                   rects[i].x + rects[i].w - 1, rects[i].y + rects[i].h - 1);
    }
}

void p8_init_surface(STDL_Surface *s, int w, int h)
{
    s->w = (int16_t)w;
    s->h = (int16_t)h;
    s->planes = 8;
    s->clip.x = 0;
    s->clip.y = 0;
    s->clip.w = (uint16_t)w;
    s->clip.h = (uint16_t)h;
    s->format = &stdl_rs.format;
}

void resp_touch_points(const STDL_Surface *d, const STDL_Point *p, int n)
{
    int x0, y0, x1, y1;

    if (d != stdl_rs.track || n <= 0) {
        return;
    }
    x0 = x1 = p->x;
    y0 = y1 = p->y;
    while (--n > 0) {
        p++;
        if (p->x < x0) x0 = p->x;
        if (p->x > x1) x1 = p->x;
        if (p->y < y0) y0 = p->y;
        if (p->y > y1) y1 = p->y;
    }
    resp_touch(d, x0, y0, x1, y1);
}

/* ---------------------------------------------------------------- */
/* the bench's view of Flip (stdl_bench.h)                          */

const STDL_PresentStats *STDL_GetPresentStats(void)
{
    return &stdl_rs.stats;
}

/* ---------------------------------------------------------------- */
/* the colours                                                      */

/* colours [first, first + n) of `c` into the TT's or the Falcon's
 * registers */
static void resp_registers(const STDL_Colour *c, int first, int n)
{
#ifdef __m68k__
    int i;

    if (stdl_rs.falcon) {
        for (i = first; i < first + n; i++, c++) {
            STDL_FALCONPAL[i] = ((uint32_t)(c->r & 0xFC) << 24)
                              | ((uint32_t)(c->g & 0xFC) << 16)
                              | (uint32_t)(c->b & 0xFC);
        }
    } else {
        for (i = first; i < first + n; i++, c++) {
            STDL_TTPAL[i] = (uint16_t)(((c->r >> 4) << 8)
                                       | (c->g & 0xF0) | (c->b >> 4));
        }
    }
#else
    (void)c; (void)first; (void)n;
#endif
}

/* every word written to the ST's sixteen registers (stdl_pal_write),
 * staged or not, onto the TT's or the Falcon's: their modes read their
 * own. A word is the ST's 4 bits a channel, so an 8-plane screen keeps
 * that entry at 4 bits too */
static void resp_pal_word(int index, uint16_t word)
{
    stdl_hw_to_rgb(word, &stdl_rs.colours[index]);
    resp_registers(&stdl_rs.colours[index], index, 1);
}

int resp_set_colours(STDL_Surface *s, const STDL_Colour *c, int first,
                     int n)
{
    int i;

    /* the screen's colours alone: an 8-plane surface has no palette of
     * its own */
    if (s != &stdl_rs.screen.s || c == NULL || first < 0 || first > 255) {
        return 0;
    }
    if (n > 256 - first) {
        n = 256 - first;
    }
    for (i = 0; i < n; i++) {
        stdl_rs.colours[first + i] = c[i];
    }
    resp_registers(&stdl_rs.colours[first], first, n);
    return 1;
}

/* STDL_FadeTo on an 8-plane screen: all 256, each step worked out
 * before its blank and written after it, as the sixteen's are */
static void resp_fade(const STDL_Palette *target, int frames)
{
    STDL_Colour from[256], step[256];
    int f, i, n;

    if (frames > 32767) {
        frames = 32767;
    }
    n = target->ncolors > 256 ? 256 : target->ncolors;
    memcpy(from, stdl_rs.colours, sizeof(from));
    for (f = 1; f <= frames; f++) {
        for (i = 0; i < n; i++) {
            step[i].r = (uint8_t)(from[i].r + STDL_DivS16(STDL_MulS16(
                target->colors[i].r - from[i].r, f), frames));
            step[i].g = (uint8_t)(from[i].g + STDL_DivS16(STDL_MulS16(
                target->colors[i].g - from[i].g, f), frames));
            step[i].b = (uint8_t)(from[i].b + STDL_DivS16(STDL_MulS16(
                target->colors[i].b - from[i].b, f), frames));
            step[i].unused = 0;
        }
        STDL_WaitVBL();
        resp_set_colours(&stdl_rs.screen.s, step, 0, n);
    }
}

/* ---------------------------------------------------------------- */
/* the mode                                                         */

STDL_Surface *resp_set_mode(int w, int h, int bpp, uint32_t flags)
{
    const int colour = stdl_rs.colour != NULL && bpp >= 8;
    const int tall = stdl_rs.tall != NULL && h > 200;
    const resp_model *m = colour ? stdl_rs.colour : stdl_rs.tall;
    int i, rows, wide, vga, lines;
    uint8_t *p;
#ifdef __m68k__
    int16_t mode = 0;
#endif

    (void)w;
    (void)flags;
    if (stdl_resp_screen != NULL) {
        return stdl_resp_screen;
    }
    if (m == NULL || (!colour && !tall)) {
        STDL_SetError("no responsive screen asked for");
        return NULL;
    }
    rows = tall ? 240 : 200;
    /* 320 pixels in the screen's planes, in bytes */
    wide = m->planes == 8 ? 320 : 160;
#ifdef __m68k__
    stdl_rs.falcon = (uint8_t)stdl_is_falcon();
    vga = stdl_rs.falcon && VgetMonitor() == MON_VGA;
#else
    stdl_rs.falcon = (uint8_t)stdl_host_resp_falcon;
    vga = stdl_host_resp_vga;
#endif
    stdl_rs.left = 0;
    stdl_rs.dbl = 0;
    if (!stdl_rs.falcon) {
        /* TT Low, 8 planes whatever the screen's */
        lines = 480;
        stdl_rs.line = 320;
        stdl_rs.dbl = 1;
    } else if (vga || rows == 200) {
        lines = vga ? 240 : 200;
        stdl_rs.line = (int16_t)wide;
    } else {
        /* 240 lines on RGB or a TV take overscan: 384 pixels a line,
         * the picture 32 in */
        lines = 240;
        stdl_rs.line = (int16_t)(m->planes == 8 ? 384 : 192);
        stdl_rs.left = (int16_t)(m->planes == 8 ? 32 : 16);
    }
#ifdef __m68k__
    if (stdl_rs.falcon) {
        mode = (int16_t)((m->planes == 8 ? BPS8 : BPS4) | COL40
                         | (vga ? (VGA | VERTFLAG)
                                : (PAL | (rows > 200 ? OVERSCAN : 0))));
    }
#endif
    stdl_rs.page_bytes = stdl_row_off(lines, (uint16_t)stdl_rs.line);
    for (i = 0; i < 2; i++) {
        stdl_rs.page_alloc[i] = stdl_stram_alloc(stdl_rs.page_bytes + 256);
        if (stdl_rs.page_alloc[i] == NULL) {
            STDL_SetError("out of memory for a responsive screen page");
            resp_shutdown();
            return NULL;
        }
        p = (uint8_t *)(((uintptr_t)stdl_rs.page_alloc[i] + 255)
                        & ~(uintptr_t)255);
        memset(p, 0, stdl_rs.page_bytes);
        stdl_rs.page[i] = p;
    }
    stdl_rs.w = 320;
    stdl_rs.h = (int16_t)rows;
    stdl_rs.top = (int16_t)((((lines >> stdl_rs.dbl) - rows) >> 1)
                            << stdl_rs.dbl);
    for (i = 0; i < 2; i++) {
        stdl_rs.box[i].x0 = 1;
        stdl_rs.box[i].x1 = 0;
    }

    /* the ST's default sixteen, as an ST screen has; an 8-plane
     * screen's other 240 black */
    stdl_default_colours();
    memset(stdl_rs.colours, 0, sizeof(stdl_rs.colours));
    memcpy(stdl_rs.colours, stdl.colours, 16 * sizeof(STDL_Colour));
    if (m->planes == 8) {
        stdl_rs.palette.ncolors = 256;
        stdl_rs.palette.colors = stdl_rs.colours;
    } else {
        stdl_rs.palette.ncolors = 16;
        stdl_rs.palette.colors = stdl.colours;
    }
    stdl_rs.format.palette = &stdl_rs.palette;
    stdl_rs.format.BitsPerPixel = m->planes;
    stdl_rs.format.BytesPerPixel = 1;

    memset(&stdl_rs.screen, 0, sizeof(stdl_rs.screen));
    p8_init_surface(&stdl_rs.screen.s, 320, rows);
    stdl_rs.screen.s.planes = m->planes;
    stdl_rs.screen.s.flags = STDL_SCREEN | STDL_HWSURFACE | STDL_FULLSCREEN
                           | STDL_HWPALETTE | STDL_DOUBLEBUF;
    stdl_rs.model = m;
    /* kept after a tall screen, for 8-plane surfaces an earlier
     * screen left */
    if (m->ops != NULL) {
        stdl_resp = m->ops;
    }
    stdl_rs.track = &stdl_rs.screen.s;  /* a model's init may clear it */
    if (m->screen_init() < 0) {
        resp_shutdown();
        return NULL;
    }
    stdl_rs.back = 1;
    m->screen_page(1);

#ifdef __m68k__
    /* a resolution change clears the screen, as the console is reset
     * with it, and sets its own colours: ours go after */
    if (stdl_rs.falcon) {
        (void)VsetScreen(stdl_rs.page[0], stdl_rs.page[0], 3, mode);
    } else {
        (void)Setscreen(stdl_rs.page[0], stdl_rs.page[0], 7);
    }
    stdl_vbl_hz = stdl_measure_vbl();
    stdl.asked = STDL_FRCLOCK - 1;
    if (stdl_rs.top > 0) {
        /*
         * The console writes at the top of page 0, and a screen of 200
         * rows centred has rows above it that nothing redraws: what a
         * program printed stayed there, every other frame. The cursor
         * goes past them, into the picture, where the program's own
         * drawing covers it as an ST's does (a cell 8 or 16 lines
         * high: the line is at least `top` either way).
         */
        char esc[7] = { 27, 'f', 27, 'Y', 0, 32, 0 };

        esc[4] = (char)(32 + ((stdl_rs.top + 7) >> 3));
        (void)Cconws(esc);
    }
#endif
    stdl_pal_write = resp_pal_word;
    if (m->planes == 8) {
        resp_registers(stdl_rs.colours, 0, 256);
    } else {
        stdl_palette_apply_hw();
    }
    stdl_resp_screen = &stdl_rs.screen.s;
    return stdl_resp_screen;
}

void resp_flip(void)
{
    resp_box *b = &stdl_rs.box[stdl_rs.back];

    if (stdl_rs.model->present != NULL) {
        const uint32_t t0 = STDL_GetHz38400();

        stdl_rs.model->present(stdl_rs.back, b);
        stdl_rs.stats.present_time += STDL_GetHz38400() - t0;
    }
    b->x0 = 1;
    b->x1 = 0;
    stdl_rs.stats.flips++;
    stdl_rs.stats.shown = stdl_rs.page[stdl_rs.back]
                        + stdl_row_off(stdl_rs.top, (uint16_t)stdl_rs.line);
    stdl_rs.stats.shown_bytes = stdl_row_off(stdl_rs.h << stdl_rs.dbl,
                                             (uint16_t)stdl_rs.line);
#ifdef __m68k__
    (void)Setscreen((void *)-1L, stdl_rs.page[stdl_rs.back], -1);
    STDL_WaitVBL();
#endif
    stdl_rs.back ^= 1;
    stdl_rs.model->screen_page(stdl_rs.back);
}

/* after the desktop is back (restore_all), or a failed mode set */
void resp_shutdown(void)
{
    int i;

    if (stdl_rs.model != NULL) {
        stdl_rs.model->screen_free();
        stdl_rs.model = NULL;
    }
    stdl_pal_write = NULL;
    stdl_rs.track = NULL;
    for (i = 0; i < 2; i++) {
        stdl_stram_free(stdl_rs.page_alloc[i]);
        stdl_rs.page_alloc[i] = NULL;
        stdl_rs.page[i] = NULL;
    }
    stdl_rs.page_bytes = 0;
}
