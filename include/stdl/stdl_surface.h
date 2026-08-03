/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Surface creation and management. Surfaces are always directly
 * addressable planar buffers; there is no lock/unlock.
 */

#ifndef STDL_SURFACE_H
#define STDL_SURFACE_H

#include <stdl/stdl_types.h>

/* Width is padded up to a multiple of 16 pixels internally; w keeps
 * the requested value. Pixels are cleared to colour 0. */
STDL_Surface *STDL_CreateSurface(int w, int h);
STDL_Surface *STDL_DuplicateSurface(const STDL_Surface *s);
void          STDL_FreeSurface(STDL_Surface *s);

/* Build (or drop) the transparency mask from a palette index.
 * Pixels equal to `key` become transparent in masked blits.
 * A key >= STDL_TRANSPARENT means "no pixel value is the key":
 * masked blitting is enabled using the mask the surface already
 * has (created all-opaque if there is none) and the pixels are
 * not scanned - for surfaces whose transparency is built while
 * decoding, with STDL_PutGroup / STDL_PutGroup8 or transparent
 * fills, rather than carried in the pixel values. */
int  STDL_SetColourKey(STDL_Surface *s, int enable, uint8_t key);
#define STDL_SetColorKey STDL_SetColourKey

/* NULL rect resets the clip to the full surface. */
void STDL_SetClipRect(STDL_Surface *s, const STDL_Rect *r);
void STDL_GetClipRect(STDL_Surface *s, STDL_Rect *r);

/* Expand 1bpp row-major bitmap data (MSB first, like ST fonts and
 * X bitmaps after bit reversal) into a planar surface: set bits get
 * colour fg, clear bits colour bg. bytes_per_row = (w + 7) / 8. */
STDL_Surface *STDL_SurfaceFrom1bpp(const uint8_t *bits, int w, int h,
                                   uint8_t fg, uint8_t bg);

/*
 * Byte-per-pixel (chunky) source art to planar - the conversion a
 * port would otherwise hand-roll around STDL_PutGroup8. Each source
 * byte is an ST palette index; `stride` is the source row pitch in
 * bytes, so a frame can be lifted straight out of a wider sheet
 * (pass w for tightly packed data). `keycolour` is a source byte
 * value that becomes transparent and gives the surface a mask; pass
 * -1 for a fully opaque surface.
 *
 * This is a load-time path, not a rendering one: convert once, then
 * blit the surface or freeze it into a sprite.
 */
STDL_Surface *STDL_SurfaceFromIndexed8(const uint8_t *bytes, int w,
                                       int h, int stride,
                                       int keycolour);

/*
 * Recolour in place through a 16-entry map: a pixel of index c
 * becomes map[c]. The mask is untouched, so transparency is
 * unchanged.
 *
 * This is how a port gets the same artwork in several colour schemes
 * - team colours, damage flashes, palette variants - without a
 * blit-time remap, which planar data cannot do cheaply (see
 * docs/limits.md). Convert or load the art once, duplicate the
 * surface per variant, remap, and freeze each into a sprite; every
 * variant then draws through the ordinary fast paths.
 */
void STDL_RemapSurface(STDL_Surface *s, const uint8_t map[16]);

/*
 * Logical origin of the surface's top-left pixel. Blits and rect
 * fills translate their coordinates by the origin (both as source
 * and destination), so a 320x40 stripe with origin (0, camera_y)
 * serves as the visible window of a tall level and the game keeps
 * using level coordinates. Draw primitives ignore the origin.
 */
void STDL_SetSurfaceOrigin(STDL_Surface *s, int x, int y);

/* Attach an all-transparent (or all-opaque) mask without scanning
 * for a colour key - for surfaces composed at runtime with
 * STDL_PutGroup / transparent fills. */
int  STDL_CreateMask(STDL_Surface *s, int transparent);

/* 1 if the surface has no transparent pixels (no mask, or a mask
 * with no bits set). The scan is cached and invalidated by blits,
 * fills and key changes. */
int  STDL_SurfaceIsOpaque(STDL_Surface *s);

/* Decoder fast path: write one 16-pixel group (4 plane words and,
 * if the surface has a mask, its mask word). x is rounded down to
 * a group boundary; the group must be fully inside the surface. */
void STDL_PutGroup(STDL_Surface *s, int x, int y,
                   const uint16_t planes[4], uint16_t mask);

/* Same for decoders whose data is byte-granular: writes the 8-pixel
 * half-group containing x (x is rounded down to a multiple of 8).
 * transmask follows the mask convention - bit set = transparent -
 * so a decoder holding an opacity byte passes its complement. */
void STDL_PutGroup8(STDL_Surface *s, int x, int y,
                    const uint8_t planes[4], uint8_t transmask);

#endif /* STDL_SURFACE_H */
