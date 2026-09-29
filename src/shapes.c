/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Lines and circles, built on the batched primitives.
 *
 * They used to draw a pixel or a row at a time through STDL_PutPixel
 * and STDL_HLine, each paying the whole of a call's clipping and
 * set-up for one pixel or one row: about 150us a pixel for a line on
 * a plain ST, 400us a row for a filled circle. They now collect what
 * they would have drawn and hand it to STDL_Points or STDL_HSpans in
 * batches, which pay that set-up once for the batch - the same
 * pixels, in the same order where order could matter. Each point or
 * span is clipped before it goes in, which also keeps it inside the
 * batch's 16-bit coordinates whatever the caller's ints were.
 * Measured on a plain ST: a 100x60 line 15.0ms to 10.0, a radius-20
 * circle 16.7 to 10.6, a radius-12 filled circle 14.5 to 6.2.
 *
 * Their own translation unit: a program that draws no lines or
 * circles does not carry them.
 */

#include "stdl_internal.h"

#define SHAPE_BATCH 64

typedef struct {
    STDL_Surface *dst;
    uint8_t col;
    int cx0, cy0, cx1, cy1;     /* the clip rectangle, inclusive */
    int n;
    STDL_Point pts[SHAPE_BATCH];
} point_batch_t;

static void batch_open(point_batch_t *b, STDL_Surface *dst, uint8_t col)
{
    b->dst = dst;
    b->col = col;
    b->cx0 = dst->clip.x;
    b->cy0 = dst->clip.y;
    b->cx1 = dst->clip.x + dst->clip.w - 1;
    b->cy1 = dst->clip.y + dst->clip.h - 1;
    b->n = 0;
}

static void batch_flush(point_batch_t *b)
{
    if (b->n > 0) {
        STDL_Points(b->dst, b->pts, b->n, b->col);
        b->n = 0;
    }
}

static __inline__ void batch_add(point_batch_t *b, int x, int y)
{
    if (x < b->cx0 || x > b->cx1 || y < b->cy0 || y > b->cy1) {
        return;
    }
    b->pts[b->n].x = (int16_t)x;
    b->pts[b->n].y = (int16_t)y;
    if (++b->n == SHAPE_BATCH) {
        batch_flush(b);
    }
}

void STDL_Line(STDL_Surface *dst, int x1, int y1, int x2, int y2,
               uint8_t col)
{
    int dx, dy, sx, sy, err, e2;
    point_batch_t b;

    if (dst == NULL) {
        return;
    }
    if (y1 == y2) {
        STDL_HLine(dst, x1, x2, y1, col);
        return;
    }
    if (x1 == x2) {
        STDL_VLine(dst, x1, y1, y2, col);
        return;
    }
    batch_open(&b, dst, col);
    dx = x2 > x1 ? x2 - x1 : x1 - x2;
    dy = y2 > y1 ? y1 - y2 : y2 - y1;   /* negative magnitude */
    sx = x1 < x2 ? 1 : -1;
    sy = y1 < y2 ? 1 : -1;
    err = dx + dy;
    for (;;) {
        batch_add(&b, x1, y1);
        if (x1 == x2 && y1 == y2) {
            break;
        }
        e2 = err * 2;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
    batch_flush(&b);
}

void STDL_Circle(STDL_Surface *dst, int cx, int cy, int r, uint8_t col)
{
    int x = r, y = 0, err = 1 - r;
    point_batch_t b;

    if (dst == NULL || r < 0) {
        return;
    }
    batch_open(&b, dst, col);
    while (x >= y) {
        batch_add(&b, cx + x, cy + y);
        batch_add(&b, cx - x, cy + y);
        batch_add(&b, cx + x, cy - y);
        batch_add(&b, cx - x, cy - y);
        batch_add(&b, cx + y, cy + x);
        batch_add(&b, cx - y, cy + x);
        batch_add(&b, cx + y, cy - x);
        batch_add(&b, cx - y, cy - x);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
    batch_flush(&b);
}

/* one row of a filled circle, clipped, into a span batch */
static void span_add(STDL_Span *sp, int *n, STDL_Surface *dst, int x1,
                     int x2, int y, uint8_t col)
{
    const int cx0 = dst->clip.x, cx1 = dst->clip.x + dst->clip.w - 1;

    if (y < dst->clip.y || y >= dst->clip.y + dst->clip.h) {
        return;
    }
    if (x1 < cx0) x1 = cx0;
    if (x2 > cx1) x2 = cx1;
    if (x1 > x2) {
        return;
    }
    sp[*n].x = (int16_t)x1;
    sp[*n].y = (int16_t)y;
    sp[*n].len = (int16_t)(x2 - x1 + 1);
    if (++*n == SHAPE_BATCH) {
        STDL_HSpans(dst, sp, *n, col);
        *n = 0;
    }
}

/*
 * The midpoint circle draws each row several times over (the rows at
 * cy +- x once for every y that shares that x); the filled circle is
 * the union of those spans, and a union of spans centred on the same
 * column is the widest of them. So the half-width of each row is
 * worked out first and every row drawn once. Radii past the table
 * fall back to drawing each step's four spans, as before.
 */
#define FILL_HW_MAX 256

void STDL_FillCircle(STDL_Surface *dst, int cx, int cy, int r,
                     uint8_t col)
{
    STDL_Span sp[SHAPE_BATCH];
    int x = r, y = 0, err = 1 - r, n = 0;

    if (dst == NULL || r < 0) {
        return;
    }
    if (r < FILL_HW_MAX) {
        int16_t hw[FILL_HW_MAX];
        int dy;

        for (dy = 0; dy <= r; dy++) {
            hw[dy] = -1;
        }
        while (x >= y) {
            if (hw[y] < x) hw[y] = (int16_t)x;
            if (hw[x] < y) hw[x] = (int16_t)y;
            y++;
            if (err < 0) {
                err += 2 * y + 1;
            } else {
                x--;
                err += 2 * (y - x) + 1;
            }
        }
        for (dy = 0; dy <= r; dy++) {
            if (hw[dy] < 0) {
                continue;
            }
            span_add(sp, &n, dst, cx - hw[dy], cx + hw[dy], cy + dy, col);
            if (dy != 0) {
                span_add(sp, &n, dst, cx - hw[dy], cx + hw[dy], cy - dy,
                         col);
            }
        }
    } else {
        while (x >= y) {
            span_add(sp, &n, dst, cx - x, cx + x, cy + y, col);
            span_add(sp, &n, dst, cx - x, cx + x, cy - y, col);
            span_add(sp, &n, dst, cx - y, cx + y, cy + x, col);
            span_add(sp, &n, dst, cx - y, cx + y, cy - x, col);
            y++;
            if (err < 0) {
                err += 2 * y + 1;
            } else {
                x--;
                err += 2 * (y - x) + 1;
            }
        }
    }
    if (n > 0) {
        STDL_HSpans(dst, sp, n, col);
    }
}
