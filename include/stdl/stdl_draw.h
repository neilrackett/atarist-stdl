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

#ifdef __cplusplus
}
#endif

#endif /* STDL_DRAW_H */
