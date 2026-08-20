/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_Surface: creation, colour keys, clipping.
 */

#include <stdlib.h>
#include <string.h>
#include "stdl_internal.h"

/* One group of slack either side of the pixel data: the unaligned
 * blit path may read (never write) one group before or after the
 * rectangle it copies, and the slack keeps that inside the block. */
#define GUARD 8

STDL_Surface *STDL_CreateSurface(int w, int h)
{
    STDL_Surface *s;
    int groups;
    uint32_t size;
    uint8_t *block;

    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        STDL_SetError("bad surface size");
        return NULL;
    }
    groups = (w + 15) >> 4;
    size = (uint32_t)groups * 8 * h;

    /*
     * Surface, format and palette are one allocation, not three.
     * They have exactly the same lifetime, and a tile-per-surface
     * game holds thousands of them: three malloc headers plus the
     * allocator's rounding cost more than a 16x16 tile's 128 bytes
     * of pixels. FreeNukum keeps 1259 surfaces alive, so this is
     * ~40K of a 1M machine's heap.
     */
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

    block = malloc(size + 2 * GUARD);
    if (block == NULL) {
        free(s);
        STDL_SetError("out of memory");
        return NULL;
    }
    memset(block, 0, size + 2 * GUARD);
    s->pixels = block + GUARD;
    s->w = (int16_t)w;
    s->h = (int16_t)h;
    s->stride = (uint16_t)(groups * 8);
    s->planes = 4;
    s->clip.x = 0;
    s->clip.y = 0;
    s->clip.w = (uint16_t)w;
    s->clip.h = (uint16_t)h;

    s->format->palette->ncolors = 16;
    memcpy(s->format->palette->colors, stdl.colours,
           16 * sizeof(STDL_Colour));
    s->format->BitsPerPixel = 4;
    s->format->BytesPerPixel = 1;
    return s;
}

void STDL_FreeSurface(STDL_Surface *s)
{
    if (s == NULL || (s->flags & STDL_SCREEN)) {
        return;
    }
    /* borrowed blocks (STDL_CreateSurfaceFrom) stay the caller's */
    if (!(s->flags & STDL_PREALLOC)) {
        if (s->pixels != NULL) {
            free(s->pixels - GUARD);
        }
        if (s->mask != NULL) {
            free(s->mask - GUARD);
        }
    }
    free(s);          /* format and palette share the same block */
}

STDL_Surface *STDL_DuplicateSurface(const STDL_Surface *s)
{
    STDL_Surface *d;

    if (s == NULL) {
        return NULL;
    }
    d = STDL_CreateSurface(s->w, s->h);
    if (d == NULL) {
        return NULL;
    }
    memcpy(d->pixels, s->pixels, (uint32_t)s->stride * s->h);
    memcpy(d->format->palette->colors, s->format->palette->colors,
           16 * sizeof(STDL_Colour));
    if (s->flags & STDL_SRCKEY) {
        STDL_SetColourKey(d, 1, s->colourkey);
    }
    return d;
}

/* allocate the mask block with the same guard slack as the pixels:
 * the unaligned blit path may read one word past either end */
static int mask_alloc(STDL_Surface *s)
{
    int groups = s->stride / 8;

    if (s->mask == NULL) {
        uint32_t size = (uint32_t)groups * 2 * s->h;
        uint8_t *block;
        /* a borrowed surface brings its own mask or none: a library
         * allocation here would leak when the header is freed */
        if (s->flags & STDL_PREALLOC) {
            STDL_SetError("borrowed surface has no mask");
            return -1;
        }
        block = malloc(size + 2 * GUARD);
        if (block == NULL) {
            STDL_SetError("out of memory for mask");
            return -1;
        }
        memset(block, 0, size + 2 * GUARD);
        s->mask = block + GUARD;
        s->maskstride = (uint16_t)(groups * 2);
    }
    return 0;
}

int STDL_SetColourKey(STDL_Surface *s, int enable, uint8_t key)
{
    int groups, y, g;

    if (s == NULL || (s->flags & STDL_SCREEN)) {
        return -1;
    }
    if (!enable) {
        if (s->mask != NULL && !(s->flags & STDL_PREALLOC)) {
            free(s->mask - GUARD);
        }
        s->mask = NULL;
        s->flags &= ~STDL_SRCKEY;
        return 0;
    }
    /* key >= STDL_TRANSPARENT: no pixel value is the key, so keep
     * the mask the surface already has (transparent fills,
     * STDL_PutGroup, decoders) instead of rebuilding it from the
     * pixels. A missing mask is created all-opaque - mask_alloc
     * zero-fills, and a clear bit means "destination overwritten". */
    if (key >= STDL_TRANSPARENT) {
        if (mask_alloc(s) < 0) {
            return -1;
        }
        s->colourkey = STDL_TRANSPARENT;
        s->flags |= STDL_SRCKEY;
        s->opaque_state = 0;
        return 0;
    }
    key &= 15;
    if (mask_alloc(s) < 0) {
        return -1;
    }
    groups = s->stride / 8;
    s->colourkey = key;
    s->flags |= STDL_SRCKEY;
    s->opaque_state = 0;

    /* mask bit set = pixel matches the key = destination preserved.
     * Word-parallel: a pixel matches when every plane word agrees
     * with the key's bit for that plane. */
    for (y = 0; y < s->h; y++) {
        const uint16_t *grp =
            (const uint16_t *)(s->pixels + (uint32_t)y * s->stride);
        uint16_t *mrow =
            (uint16_t *)(s->mask + (uint32_t)y * s->maskstride);
        for (g = 0; g < groups; g++) {
            uint16_t m;
            m  = (key & 1) ? grp[0] : (uint16_t)~grp[0];
            m &= (key & 2) ? grp[1] : (uint16_t)~grp[1];
            m &= (key & 4) ? grp[2] : (uint16_t)~grp[2];
            m &= (key & 8) ? grp[3] : (uint16_t)~grp[3];
            mrow[g] = m;
            grp += 4;
        }
    }
    return 0;
}

void STDL_SetSurfaceOrigin(STDL_Surface *s, int x, int y)
{
    if (s != NULL) {
        s->org_x = (int16_t)x;
        s->org_y = (int16_t)y;
    }
}

int STDL_CreateMask(STDL_Surface *s, int transparent)
{
    if (s == NULL || (s->flags & STDL_SCREEN)) {
        return -1;
    }
    if (mask_alloc(s) < 0) {
        return -1;
    }
    memset(s->mask, transparent ? 0xFF : 0x00,
           (uint32_t)s->maskstride * s->h);
    s->flags |= STDL_SRCKEY;
    s->opaque_state = 0;
    return 0;
}

int STDL_SurfaceIsOpaque(STDL_Surface *s)
{
    if (s == NULL) {
        return 0;
    }
    if (s->mask == NULL) {
        return 1;
    }
    if (s->opaque_state == 0) {
        uint32_t n = (uint32_t)s->maskstride * s->h;
        const uint8_t *m = s->mask;
        s->opaque_state = 1;
        while (n--) {
            if (*m++ != 0) {
                s->opaque_state = 2;
                break;
            }
        }
    }
    return s->opaque_state == 1;
}

void STDL_PutGroup(STDL_Surface *s, int x, int y,
                   const uint16_t planes[4], uint16_t mask)
{
    uint16_t *grp;
    int g;

    if (s == NULL || planes == NULL || y < 0 || y >= s->h) {
        return;
    }
    g = x >> 4;
    if (g < 0 || g >= s->stride / 8) {
        return;
    }
    grp = (uint16_t *)(s->pixels + (uint32_t)y * s->stride + g * 8);
    /* words above the plane budget are dropped, not stored: no
     * public entry point may break the "high planes are zero"
     * invariant the budget rests on */
    stdl_put_planes(grp, planes[0], planes[1], planes[2], planes[3],
                    stdl_planes);
    if (s->mask != NULL) {
        ((uint16_t *)(s->mask + (uint32_t)y * s->maskstride))[g] = mask;
        s->opaque_state = 0;
    }
}

/*
 * Half-group variant for decoders whose source data is byte (8
 * pixel) granular. In a group the EVEN byte of a plane word holds
 * pixels 0-7 and the ODD byte pixels 8-15, so the plane byte for x
 * sits at group_base + p * 2 + ((x >> 3) & 1); the mask byte lives
 * at the matching offset inside the group's mask word.
 */
void STDL_PutGroup8(STDL_Surface *s, int x, int y,
                    const uint8_t planes[4], uint8_t transmask)
{
    uint8_t *grp;
    int g, half;

    if (s == NULL || planes == NULL || y < 0 || y >= s->h || x < 0) {
        return;
    }
    g = x >> 4;
    if (g >= s->stride / 8) {
        return;
    }
    half = STDL_WORD_BYTE((x >> 3) & 1);
    grp = s->pixels + (uint32_t)y * s->stride + g * 8 + half;
    grp[0] = planes[0];                     /* budget-truncated, as */
    if (stdl_planes > 1) grp[2] = planes[1];    /* STDL_PutGroup is */
    if (stdl_planes > 2) grp[4] = planes[2];
    if (stdl_planes > 3) grp[6] = planes[3];
    if (s->mask != NULL) {
        s->mask[(uint32_t)y * s->maskstride + g * 2 + half] = transmask;
        s->opaque_state = 0;
    }
}

void STDL_SetClipRect(STDL_Surface *s, const STDL_Rect *r)
{
    int x1, y1, x2, y2;

    if (s == NULL) {
        return;
    }
    if (r == NULL) {
        s->clip.x = 0;
        s->clip.y = 0;
        s->clip.w = (uint16_t)s->w;
        s->clip.h = (uint16_t)s->h;
        return;
    }
    x1 = r->x < 0 ? 0 : r->x;
    y1 = r->y < 0 ? 0 : r->y;
    x2 = r->x + r->w > s->w ? s->w : r->x + r->w;
    y2 = r->y + r->h > s->h ? s->h : r->y + r->h;
    if (x2 < x1) x2 = x1;
    if (y2 < y1) y2 = y1;
    s->clip.x = (int16_t)x1;
    s->clip.y = (int16_t)y1;
    s->clip.w = (uint16_t)(x2 - x1);
    s->clip.h = (uint16_t)(y2 - y1);
}

void STDL_GetClipRect(STDL_Surface *s, STDL_Rect *r)
{
    if (s != NULL && r != NULL) {
        *r = s->clip;
    }
}

/* STDL_SurfaceFrom1bpp lives in indexed.c, with the other converters
 * from non-planar source art: surface.o is in every linked program
 * and a port whose art is already planar must not carry them. */
