/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Surface and sprite copying.
 *
 * Dispatch, in order of preference:
 *   1. Same 16px phase, unmasked  - word copies with edge masks
 *   2. Same phase, masked         - per-group mask/or
 *   3. Pre-shifted sprite variant - select variant, use (1)/(2)
 *   4. Unaligned                  - runtime shift chain (slow path)
 */

#ifndef STDL_BLIT_H
#define STDL_BLIT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdl/stdl_types.h>

/*
 * SDL 1.2 semantics: srcrect NULL = whole source, dstrect NULL =
 * top-left; only dstrect x/y are read, and the final clipped
 * rectangle is written back to dstrect. Uses the source mask when
 * STDL_SRCKEY is set (see STDL_SetColourKey).
 */
int STDL_BlitSurface(STDL_Surface *src, const STDL_Rect *srcrect,
                     STDL_Surface *dst, STDL_Rect *dstrect);

/*
 * As STDL_BlitSurface, plus destination-mask semantics for engines
 * that keep a foreground/priority plane in the mask:
 *
 *   STDL_BLIT_UNDER  destination mask bits protect their pixels, so
 *                    the blit passes behind marked foreground
 *   STDL_BLIT_MARK   set the destination mask under blitted pixels
 *                    (default maintenance clears it there)
 *
 * The bit values match STDL_I8_UNDER / STDL_I8_MARK, so a caller
 * composing the same scene from indexed and planar sources can use
 * one set of flags. flags == 0 is exactly STDL_BlitSurface, down to
 * the BLiTTER fast paths; UNDER and MARK take the CPU route.
 */
#define STDL_BLIT_UNDER  0x0004u
#define STDL_BLIT_MARK   0x0008u

int STDL_BlitSurfaceEx(STDL_Surface *src, const STDL_Rect *srcrect,
                       STDL_Surface *dst, STDL_Rect *dstrect,
                       unsigned flags);

void STDL_BlitSprite(STDL_Sprite *spr, int frame, STDL_Surface *dst,
                     int x, int y);
void STDL_BlitTile(STDL_Tileset *ts, int index, STDL_Surface *dst,
                   int x, int y);

#ifdef __cplusplus
}
#endif

#endif /* STDL_BLIT_H */
