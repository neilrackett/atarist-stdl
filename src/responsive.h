/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * What the responsive objects share (responsive.c, planes8.c,
 * tall4.c): the screen's state, and the seam between the screen and
 * its surfaces (resp_model): 8-plane surfaces for the colour, or a
 * 4-plane screen for a tall one without it. The host tests plug their
 * reference (tests/host/p8ref.c) into the same seam.
 */

#ifndef STDL_RESPONSIVE_INT_H
#define STDL_RESPONSIVE_INT_H

#include "stdl_internal.h"
#include <stdl/stdl_responsive.h>
#include <stdl/stdl_bench.h>

/* rows and pixels inclusive; x0 > x1 is empty */
typedef struct {
    int16_t x0, y0, x1, y1;
} resp_box;

typedef struct {
    STDL_Surface s;             /* first: the pointer handed out      */
    void *block, *mblock;       /* what the pixels and mask came in   */
} p8_surface;

typedef struct stdl_resp_model {
    const stdl_resp_ops_t *ops;
    uint8_t planes;             /* the screen's: 8, or 4              */
    /* the screen's surfaces over the display pages, set up once the
     * pages are; then page p as the one drawn into; then gone */
    int  (*screen_init)(void);
    void (*screen_page)(int p);
    void (*screen_free)(void);
    /* what page p's drawing owes display page p, if anything */
    void (*present)(int p, const resp_box *b);
} resp_model;

typedef struct {
    const resp_model *colour;   /* what the program asked for         */
    const resp_model *tall;
    /* an ST's own screen after its mode set: the bottom border opened
     * for a tall one (tallst.c), NULL for the display's own height */
    void (*st_tall)(int h);
    const resp_model *model;    /* the screen's, while it is up       */
    p8_surface screen;
    uint8_t *page[2];           /* the display pages                  */
    void *page_alloc[2];
    uint32_t page_bytes;
    int16_t w, h;
    int16_t line;               /* bytes a display line               */
    int16_t left;               /* bytes into a line to the picture   */
    int16_t top;                /* the display line of row 0          */
    uint8_t dbl;                /* a TT: each row on two lines        */
    uint8_t falcon;
    uint8_t back;               /* the page drawn into                */
    resp_box box[2];            /* drawn on each since it was shown   */
    const STDL_Surface *track;  /* the screen, where present reads the
                                   box; NULL where it never does      */
    STDL_Colour colours[256];
    STDL_Palette palette;
    STDL_PixelFormat format;
    STDL_PresentStats stats;
} resp_state;

extern resp_state stdl_rs;

/* what drawing on an 8-plane screen changed: the page drawn into owes
 * it; none of it where the present reads no box (a Falcon) */
static __inline__ void resp_touch(const STDL_Surface *d, int x0, int y0,
                                  int x1, int y1)
{
    resp_box *b;

    if (d != stdl_rs.track) {
        return;
    }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= stdl_rs.w) x1 = stdl_rs.w - 1;
    if (y1 >= stdl_rs.h) y1 = stdl_rs.h - 1;
    if (x0 > x1 || y0 > y1) {
        return;
    }
    b = &stdl_rs.box[stdl_rs.back];
    if (b->x0 > b->x1) {
        b->x0 = (int16_t)x0; b->y0 = (int16_t)y0;
        b->x1 = (int16_t)x1; b->y1 = (int16_t)y1;
        return;
    }
    if (x0 < b->x0) b->x0 = (int16_t)x0;
    if (y0 < b->y0) b->y0 = (int16_t)y0;
    if (x1 > b->x1) b->x1 = (int16_t)x1;
    if (y1 > b->y1) b->y1 = (int16_t)y1;
}

void resp_touch_points(const STDL_Surface *d, const STDL_Point *p, int n);
STDL_Surface *resp_set_mode(int w, int h, int bpp, uint32_t flags);
void resp_flip(void);
void resp_shutdown(void);
int  resp_set_colours(STDL_Surface *s, const STDL_Colour *c, int first,
                      int n);
void p8_init_surface(STDL_Surface *s, int w, int h);

#endif /* STDL_RESPONSIVE_INT_H */
