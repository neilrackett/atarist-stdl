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
 * pixels, in the same order where order could matter. A shape whose
 * bounding box is inside the clip rectangle goes in unclipped - the
 * batch calls clip anyway - and one that is not has each point or span
 * clipped before it goes in, which also keeps it inside the batch's
 * 16-bit coordinates whatever the caller's ints were. Measured on a
 * plain ST against drawing a pixel or a row at a time: a 100x60 line
 * 15.0ms to 7.6, a radius-20 circle 16.7 to 7.9, a radius-12 filled
 * circle 14.5 to 4.7.
 *
 * Their own translation unit: a program that draws no lines or
 * circles does not carry them.
 */

#include "stdl_internal.h"

#define SHAPE_BATCH 64

/*
 * Points and spans collect in an array on the stack through a walking
 * pointer, and go to the batch call when it is full. The clip edges
 * (cx0..cx1, cy0..cy1, inclusive) are locals of the caller, so they
 * stay in registers where a struct of them did not.
 */
#define POINT_PUT(xx, yy) \
    do { \
        p->x = (int16_t)(xx); \
        p->y = (int16_t)(yy); \
        if (++p == pts + SHAPE_BATCH) { \
            STDL_Points(dst, pts, SHAPE_BATCH, col); \
            p = pts; \
        } \
    } while (0)
/* each axis one unsigned compare: below the edge wraps above it */
#define POINT_ADD(xx, yy) \
    do { \
        const int px_ = (xx), py_ = (yy); \
        if (!clipped || ((unsigned)(px_ - cx0) <= cw \
                         && (unsigned)(py_ - cy0) <= ch)) { \
            POINT_PUT(px_, py_); \
        } \
    } while (0)

/* the clip rectangle's edges, inclusive, as locals */
#define CLIP_EDGES(dst) \
    const int cx0 = (dst)->clip.x, cx1 = (dst)->clip.x + (dst)->clip.w - 1; \
    const int cy0 = (dst)->clip.y, cy1 = (dst)->clip.y + (dst)->clip.h - 1
/* its origin and the last offsets inside it, for POINT_ADD's unsigned
 * compares */
#define CLIP_OFFSETS(dst) \
    const int cx0 = (dst)->clip.x, cy0 = (dst)->clip.y; \
    const unsigned cw = (unsigned)(dst)->clip.w - 1u; \
    const unsigned ch = (unsigned)(dst)->clip.h - 1u

/* Bresenham, x1 != x2 and y1 != y2; `clipped` false when the whole
 * line is inside the clip rectangle */
static void line_run(STDL_Surface *dst, int x1, int y1, int x2, int y2,
                     uint8_t col, const int clipped)
{
    STDL_Point pts[SHAPE_BATCH], *p = pts;
    const int dx = x2 > x1 ? x2 - x1 : x1 - x2;
    const int dy = y2 > y1 ? y1 - y2 : y2 - y1;  /* negative magnitude */
    const int sx = x1 < x2 ? 1 : -1;
    const int sy = y1 < y2 ? 1 : -1;
    int err = dx + dy, e2;
    CLIP_OFFSETS(dst);

    for (;;) {
        POINT_ADD(x1, y1);
        if (x1 == x2 && y1 == y2) {
            break;
        }
        e2 = err * 2;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
    if (p != pts) {
        STDL_Points(dst, pts, (int)(p - pts), col);
    }
}

void STDL_Line(STDL_Surface *dst, int x1, int y1, int x2, int y2,
               uint8_t col)
{
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
    {
        CLIP_EDGES(dst);

        line_run(dst, x1, y1, x2, y2, col,
                 !((x1 < x2 ? x1 : x2) >= cx0 && (x1 > x2 ? x1 : x2) <= cx1
                   && (y1 < y2 ? y1 : y2) >= cy0
                   && (y1 > y2 ? y1 : y2) <= cy1));
    }
}

/* the midpoint circle's eight points a step; `clipped` as line_run */
static void circle_run(STDL_Surface *dst, int cx, int cy, int r,
                       uint8_t col, const int clipped)
{
    STDL_Point pts[SHAPE_BATCH], *p = pts;
    int x = r, y = 0, err = 1 - r;
    CLIP_OFFSETS(dst);

    while (x >= y) {
        POINT_ADD(cx + x, cy + y);
        POINT_ADD(cx - x, cy + y);
        POINT_ADD(cx + x, cy - y);
        POINT_ADD(cx - x, cy - y);
        POINT_ADD(cx + y, cy + x);
        POINT_ADD(cx - y, cy + x);
        POINT_ADD(cx + y, cy - x);
        POINT_ADD(cx - y, cy - x);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
    if (p != pts) {
        STDL_Points(dst, pts, (int)(p - pts), col);
    }
}

void STDL_Circle(STDL_Surface *dst, int cx, int cy, int r, uint8_t col)
{
    if (dst == NULL || r < 0) {
        return;
    }
    {
        CLIP_EDGES(dst);

        circle_run(dst, cx, cy, r, col,
                   !(cx - r >= cx0 && cx + r <= cx1 && cy - r >= cy0
                     && cy + r <= cy1));
    }
}

/* one row of a filled circle, clipped, into the span batch */
#define SPAN_ADD(xa, xb, yy) \
    do { \
        const int sy_ = (yy); \
        int sa_ = (xa), sb_ = (xb); \
        if (sy_ >= cy0 && sy_ <= cy1) { \
            if (sa_ < cx0) sa_ = cx0; \
            if (sb_ > cx1) sb_ = cx1; \
            if (sa_ <= sb_) { \
                q->x = (int16_t)sa_; \
                q->y = (int16_t)sy_; \
                q->len = (int16_t)(sb_ - sa_ + 1); \
                if (++q == sp + SHAPE_BATCH) { \
                    STDL_HSpans(dst, sp, SHAPE_BATCH, col); \
                    q = sp; \
                } \
            } \
        } \
    } while (0)

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
    STDL_Span sp[SHAPE_BATCH], *q = sp;
    int x = r, y = 0, err = 1 - r;

    if (dst == NULL || r < 0) {
        return;
    }
    {
    CLIP_EDGES(dst);

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
            SPAN_ADD(cx - hw[dy], cx + hw[dy], cy + dy);
            if (dy != 0) {
                SPAN_ADD(cx - hw[dy], cx + hw[dy], cy - dy);
            }
        }
    } else {
        while (x >= y) {
            SPAN_ADD(cx - x, cx + x, cy + y);
            SPAN_ADD(cx - x, cx + x, cy - y);
            SPAN_ADD(cx - y, cx + y, cy + x);
            SPAN_ADD(cx - y, cx + y, cy - x);
            y++;
            if (err < 0) {
                err += 2 * y + 1;
            } else {
                x--;
                err += 2 * (y - x) + 1;
            }
        }
    }
    }
    if (q != sp) {
        STDL_HSpans(dst, sp, (int)(q - sp), col);
    }
}
