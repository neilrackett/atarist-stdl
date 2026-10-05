/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Drawing primitives. All shapes decompose to masked spans; fills
 * route through per-plane fill words.
 */

#ifndef STDL_DRAW_H
#define STDL_DRAW_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdl/stdl_types.h>

/*
 * SDL 1.2 semantics: r is translated by the surface origin,
 * clipped, and the final rectangle is written back in logical
 * coordinates (w = h = 0 when fully clipped). NULL fills the whole
 * clip rect. Colour STDL_TRANSPARENT punches a hole in a masked
 * surface; fills, spans and STDL_PutPixel all maintain the mask.
 */
void STDL_FillRect(STDL_Surface *dst, STDL_Rect *r, uint8_t col);
void STDL_HLine(STDL_Surface *dst, int x1, int x2, int y, uint8_t col);
void STDL_VLine(STDL_Surface *dst, int x, int y1, int y2, uint8_t col);
void STDL_Line(STDL_Surface *dst, int x1, int y1, int x2, int y2,
               uint8_t col);
void STDL_Circle(STDL_Surface *dst, int cx, int cy, int r, uint8_t col);
void STDL_FillCircle(STDL_Surface *dst, int cx, int cy, int r,
                     uint8_t col);

/* Slow: one read-modify-write per plane. Fine for cursors and
 * debugging, wrong inside loops - use spans or sprites instead. */
void    STDL_PutPixel(STDL_Surface *dst, int x, int y, uint8_t col);
uint8_t STDL_GetPixel(const STDL_Surface *src, int x, int y);

/*
 * XOR raster op: invert the planes selected by `col`, leaving the
 * planes whose colour bit is clear untouched (so col 0 is a no-op).
 * Drawing the same shape twice restores the destination exactly -
 * the CGA-era idiom for erasable overlays (terrain outlines, rubber
 * bands, cursors) without a save-under buffer.
 *
 * Clipping and coordinates match the matching fill primitives, and
 * masked surfaces have the touched pixels marked opaque as a fill
 * would. STDL_TRANSPARENT has no XOR meaning; only bits 0-3 of col
 * are used. These paths are always CPU - the shapes worth XORing
 * are small - so they stay identical with STDL_UseBlitter either
 * way.
 */
void STDL_XorPixel(STDL_Surface *dst, int x, int y, uint8_t col);
void STDL_XorHLine(STDL_Surface *dst, int x1, int x2, int y,
                   uint8_t col);
void STDL_XorVLine(STDL_Surface *dst, int x, int y1, int y2,
                   uint8_t col);
void STDL_XorRect(STDL_Surface *dst, STDL_Rect *r, uint8_t col);

/*
 * Batched spans: draw a whole field of spans in one call. A span
 * is `len` pixels from (x, y), running right for the H entry
 * points and down for the V ones; len <= 0 is skipped. Results are
 * identical to calling the matching single-span function once per
 * entry, in list order - overlapping and unsorted lists are fine,
 * and each span is clipped independently.
 *
 * Use these wherever a shape decomposes into many short spans -
 * terrain outlines, column fields, raycaster walls. A short span
 * costs far more to call than to draw (clip setup, colour
 * dispatch, the row-address multiply), so the batched call is
 * several times faster for one- and two-pixel spans; for a handful
 * of long spans there is nothing in it. The lists are the
 * caller's own memory and are only read.
 */
/*
 * Batched single pixels: same result as one STDL_PutPixel per entry,
 * in list order, each clipped independently. This is the primitive
 * for particle fields - the thing a span list still charges too much
 * for, because a one-pixel span pays a length clamp, two edge masks
 * and a two-group tail test to draw one bit. Measured on a plain ST,
 * a particle costs roughly a third of what the equivalent
 * STDL_HSpans list does.
 *
 * `col` is a palette index; STDL_TRANSPARENT punches holes in a
 * masked surface exactly as a fill would. The list is the caller's
 * memory and is only read.
 */
void STDL_Points(STDL_Surface *dst, const STDL_Point *pts,
                 int count, uint8_t col);

/*
 * The same, with a colour per point: `cols[i]` is the palette index
 * for `pts[i]`, and the result is one STDL_PutPixel per entry in
 * list order. Use it when the field is multi-coloured - sorting a
 * list into colour runs so it can go through STDL_Points costs more
 * than the per-point colour does (measured on a plain ST at 250
 * particles: a counting sort plus fifteen batched calls, 23ms; one
 * STDL_PointsC call, 14ms). Both arrays are the caller's memory and
 * are only read.
 */
void STDL_PointsC(STDL_Surface *dst, const STDL_Point *pts,
                  const uint8_t *cols, int count);

void STDL_HSpans(STDL_Surface *dst, const STDL_Span *spans,
                 int count, uint8_t col);
void STDL_VSpans(STDL_Surface *dst, const STDL_Span *spans,
                 int count, uint8_t col);
void STDL_XorHSpans(STDL_Surface *dst, const STDL_Span *spans,
                    int count, uint8_t col);
void STDL_XorVSpans(STDL_Surface *dst, const STDL_Span *spans,
                    int count, uint8_t col);

/*
 * What a fill does to the pixels it covers. SET makes them `col`;
 * the others work on the planes `col` has set and leave the rest
 * alone - XOR flips them, OR sets them, CLEAR clears them - which is
 * how a glenz or a planar overlay draws one plane over another.
 */
#define STDL_DRAW_SET   0
#define STDL_DRAW_XOR   1
#define STDL_DRAW_OR    2
#define STDL_DRAW_CLEAR 3

/*
 * Fill the convex polygon pts[0..n-1] (either winding, n >= 3) under
 * `op`. Coverage follows the rectangle's rule: the top and left edges
 * are inside, the bottom and right ones outside, so polygons that
 * share an edge tile with no gap and no overlap - XORed faces of one
 * object do not cancel along their seams - and the four corners of
 * {x, y, w, h} fill exactly that rectangle. Clipped to dst->clip.
 * Like the other drawing primitives, and unlike blits and rectangle
 * fills, it ignores the surface origin. A SET in STDL_TRANSPARENT
 * punches a hole in a masked surface, as STDL_FillRect does; XOR,
 * OR and CLEAR take bits 0-3 of `col`, as the XOR primitives do, and
 * mark what they touch opaque. Coordinates are any int16_t. A
 * polygon that is not convex is drawn as something, but not as
 * itself.
 *
 * The edges are stepped down the rows with no span list. A polygon
 * inside the clip's columns, no taller than 256 rows, SET or XORed
 * onto a surface with no mask - a 3D object's face, a glenz's - is
 * filled start to finish in 68000, both edges in 16.16 fixed point in
 * registers (exact at that height: the same pixels as the general
 * path); anything else goes through column tables. XOR, OR and CLEAR
 * touch only the planes `col` has, so a one-plane colour writes one
 * word a group. Measured on a plain ST (PRIMCOST): a 160-row diamond
 * XORed into two planes 12.6ms against 23.9 for the same rows through
 * STDL_XorHSpans; a 22-row triangle 1.55ms against 2.26, and 1.27ms
 * in one plane; 24 one-plane triangles of a glenz-like object 49ms.
 * A polygon's set-up - the vertex scan, and two divides an edge - is
 * about 5000 cycles (a 2-row triangle takes 0.68ms), so from those
 * figures spans are the cheaper way below about ten rows. CPU only.
 * Its own object, 6KB: a program that never calls it does not carry
 * it.
 */
void STDL_FillPolygon(STDL_Surface *dst, const STDL_Point *pts, int n,
                      uint8_t col, int op);

#ifdef __cplusplus
}
#endif

#endif /* STDL_DRAW_H */
