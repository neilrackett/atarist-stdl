/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_DuplicateSurface: a copy with its source's rows and mask. Lives
 * in its own object so programs that never copy a surface don't carry
 * it: in surface.o, which every program links, keeping a borrowed
 * surface's rows and a mask no key holds cost 112 bytes.
 */

#include <string.h>
#include "stdl_internal.h"

/* rows of n bytes from one stride to another: a borrowed surface's
 * (STDL_CreateSurfaceFrom) may be wider than its copy's, which one
 * copy of its whole block ran past */
static void copy_rows(uint8_t *d, uint16_t ds, const uint8_t *s,
                      uint16_t ss, uint16_t n, int rows)
{
    while (--rows >= 0) {
        memcpy(d, s, n);
        d += ds;
        s += ss;
    }
}

STDL_Surface *STDL_DuplicateSurface(const STDL_Surface *s)
{
    STDL_Surface *d;

    if (s == NULL) {
        return NULL;
    }
    if (STDL_IS_RESP(s)) {
        return stdl_resp->dup((STDL_Surface *)s);
    }
    d = stdl_surface_create(s->w, s->h);
    if (d == NULL) {
        return NULL;
    }
    copy_rows(d->pixels, d->stride, s->pixels, s->stride, d->stride, s->h);
    memcpy(d->format->palette->colors, s->format->palette->colors,
           16 * sizeof(STDL_Colour));
    /* the mask itself, not one scanned from the key again: a mask made
     * by holes, STDL_PutGroup or a decoder (key STDL_TRANSPARENT) is in
     * no pixel value, and the copy came out opaque */
    if (s->mask != NULL) {
        if (STDL_CreateMask(d, 0) < 0) {
            STDL_FreeSurface(d);
            return NULL;
        }
        copy_rows(d->mask, d->maskstride, s->mask, s->maskstride,
                  d->maskstride, s->h);
        d->flags = (d->flags & ~STDL_SRCKEY) | (s->flags & STDL_SRCKEY);
        d->colourkey = s->colourkey;
    }
    return d;
}
