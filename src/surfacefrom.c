/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_CreateSurfaceFrom: a surface over caller-owned planar memory
 * (the SDL_CreateRGBSurfaceFrom of this library).
 *
 * Ported engines often need their framebuffers to be plain blocks
 * they own - swapped by pointer, copied with one memcpy, allocated
 * inside larger structures. Wrapping such a block as a surface lets
 * every STDL primitive draw into it without the library taking over
 * the allocation. Lives in its own object so programs that never
 * borrow memory don't carry it.
 */

#include <stdlib.h>
#include <string.h>
#include "stdl_internal.h"

/*
 * `pixels` is interleaved planar per docs/format.md, `stride` bytes
 * per row (a multiple of 8, at least ((w+15)>>4)*8). `mask` is an
 * optional transparency/composition mask (bit set = destination
 * preserved), `maskstride` bytes per row; pass NULL/0 for none.
 * When a mask is given the surface is marked STDL_SRCKEY with key
 * STDL_TRANSPARENT, so masked blits use it as-is.
 *
 * The caller keeps ownership of both blocks: STDL_FreeSurface
 * releases only the surface header (STDL_PREALLOC). Lifetime and
 * alignment are the caller's contract - blocks must start on an
 * even address, and note that library-allocated surfaces carry one
 * group of guard slack either side because the unaligned blit path
 * may READ (never write) up to one group past the rectangle it
 * copies; give borrowed blocks the same slack, or keep operations
 * on the outermost groups 16-pixel aligned.
 */
STDL_Surface *STDL_CreateSurfaceFrom(void *pixels, int w, int h,
                                     int stride, uint8_t *mask,
                                     int maskstride)
{
    STDL_Surface *s;
    int groups;

    if (pixels == NULL || w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        STDL_SetError("bad surface parameters");
        return NULL;
    }
    groups = (w + 15) >> 4;
    if (stride < groups * 8 || (stride & 7) != 0
        || ((uintptr_t)pixels & 1) != 0) {
        STDL_SetError("bad surface stride or alignment");
        return NULL;
    }

    s = calloc(1, sizeof(STDL_Surface) + sizeof(STDL_PixelFormat)
                  + sizeof(STDL_Palette) + 16 * sizeof(STDL_Colour));
    if (s == NULL) {
        STDL_SetError("out of memory");
        return NULL;
    }
    s->format = (STDL_PixelFormat *)(s + 1);
    s->format->palette = (STDL_Palette *)(s->format + 1);
    s->format->palette->colors =
        (STDL_Colour *)(s->format->palette + 1);

    s->pixels = pixels;
    s->w = (int16_t)w;
    s->h = (int16_t)h;
    s->stride = (uint16_t)stride;
    s->planes = 4;
    s->flags = STDL_PREALLOC;
    s->clip.x = 0;
    s->clip.y = 0;
    s->clip.w = (uint16_t)w;
    s->clip.h = (uint16_t)h;
    if (mask != NULL) {
        s->mask = mask;
        s->maskstride = (uint16_t)maskstride;
        s->flags |= STDL_SRCKEY;
        s->colourkey = STDL_TRANSPARENT;
    }

    s->format->palette->ncolors = 16;
    memcpy(s->format->palette->colors, stdl.colours,
           16 * sizeof(STDL_Colour));
    s->format->BitsPerPixel = 4;
    s->format->BytesPerPixel = 1;
    return s;
}
