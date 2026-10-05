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
 *
 * A whole source onto a destination the same size - srcrect NULL,
 * dstrect NULL or at 0,0, no colour key, no masks, no clipping, a
 * width that is a multiple of 16 - is one memcpy on a machine without
 * a BLiTTER: 19.5ms for 320x200 on an 8MHz ST, against 42 row by
 * row. A full-size srcrect is not spotted, so pass NULL.
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
 * the BLiTTER fast paths, save the whole-surface memcpy above (every
 * blit through here would pay to test for it); UNDER and MARK take
 * the CPU route.
 */
#define STDL_BLIT_UNDER  0x0004u
#define STDL_BLIT_MARK   0x0008u

int STDL_BlitSurfaceEx(STDL_Surface *src, const STDL_Rect *srcrect,
                       STDL_Surface *dst, STDL_Rect *dstrect,
                       unsigned flags);

void STDL_BlitSprite(STDL_Sprite *spr, int frame, STDL_Surface *dst,
                     int x, int y);

/*
 * One sprite at many places: each entry drawn as STDL_BlitSprite would
 * draw it, in list order. What does not change between them is worked
 * out once, and each one wholly inside the clip that STDL_BlitSprite
 * would give its aligned four-plane loop - a pre-shifted sprite, or an
 * unshifted one on a group edge, on the CPU - goes straight to that
 * loop; any other is drawn by STDL_BlitSprite. For the bobs and
 * particles of a scene, where most of a small sprite's cost was the
 * call. Its own object: a program that never calls it does not carry
 * it.
 */
typedef struct { int16_t x, y; uint16_t frame; } STDL_SpriteAt;
void STDL_BlitSprites(STDL_Sprite *spr, const STDL_SpriteAt *at,
                      int count, STDL_Surface *dst);
void STDL_BlitTile(STDL_Tileset *ts, int index, STDL_Surface *dst,
                   int x, int y);

#ifdef __cplusplus
}
#endif

#endif /* STDL_BLIT_H */
