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

#endif /* STDL_DRAW_H */
