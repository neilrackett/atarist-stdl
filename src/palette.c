/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_Palette. Hardware words are composed in STE rotated-nibble
 * format: bits 2-0 hold the top three bits of the channel and bit 3
 * the fourth. A plain ST reads bits 2-0 of each nibble, so the same
 * word programs both register layouts correctly.
 */

#include <stddef.h>
#include "stdl_internal.h"

static uint16_t rot_nibble(uint8_t c)   /* 8-bit channel -> nibble */
{
    uint8_t v = c >> 4;
    return (uint16_t)((v >> 1) | ((v & 1) << 3));
}

uint16_t STDL_HWColour(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)((rot_nibble(r) << 8) | (rot_nibble(g) << 4)
                      | rot_nibble(b));
}

void STDL_SetColour(int index, uint16_t stColour)
{
    if (index >= 0 && index < 16) {
        STDL_HWPAL[index] = stColour;
    }
}

/* While a border overscan is open the display starts fetching at
 * line 34 instead of 63, so a palette write from the main loop is
 * far more likely to land mid-display and flash wrong colours for
 * part of a frame. overscan.c sets stdl_pal_defer and drains the
 * staged palette from a VBL callback instead, inside the blanking. */
uint8_t  stdl_pal_defer;
volatile uint8_t stdl_pal_pending_dirty;
static uint16_t pal_pending[16];

void stdl_palette_flush(void)
{
    int i;
    if (stdl_pal_pending_dirty) {
        stdl_pal_pending_dirty = 0;
        for (i = 0; i < 16; i++) {
            STDL_HWPAL[i] = pal_pending[i];
        }
    }
}

void stdl_palette_apply_hw(void)
{
    int i;
    if (stdl_pal_defer) {
        for (i = 0; i < 16; i++) {
            pal_pending[i] = STDL_HWColour(stdl.colours[i].r,
                                           stdl.colours[i].g,
                                           stdl.colours[i].b);
        }
        stdl_pal_pending_dirty = 1;
        return;
    }
    for (i = 0; i < 16; i++) {
        STDL_HWPAL[i] = STDL_HWColour(stdl.colours[i].r,
                                      stdl.colours[i].g,
                                      stdl.colours[i].b);
    }
}

int STDL_SetColours(STDL_Surface *s, const STDL_Colour *cols,
                    int first, int n)
{
    STDL_Colour *dst;
    int i, count;

    if (s == NULL || s->format == NULL || s->format->palette == NULL) {
        return 0;
    }
    dst = s->format->palette->colors;
    count = s->format->palette->ncolors;
    for (i = 0; i < n && first + i < count; i++) {
        dst[first + i] = cols[i];
    }
    if (s->flags & STDL_SCREEN) {
        stdl_palette_apply_hw();
    }
    return 1;
}

void STDL_SetPalette(STDL_Surface *s, const STDL_Palette *pal)
{
    if (pal != NULL) {
        STDL_SetColours(s, pal->colors, 0,
                        pal->ncolors > 16 ? 16 : pal->ncolors);
    }
}

uint8_t STDL_MapRGB(const STDL_PixelFormat *fmt,
                    uint8_t r, uint8_t g, uint8_t b)
{
    const STDL_Colour *cols = stdl.colours;
    int n = 16;
    uint8_t best = 0;
    int32_t bestdist = 0x7FFFFFFF;
    int i;

    if (fmt != NULL && fmt->palette != NULL) {
        cols = fmt->palette->colors;
        n = fmt->palette->ncolors;
    }
    /* only the first 2^budget entries can appear on screen, so never
     * hand back an index the drawing paths would truncate */
    if (n > (1 << stdl_planes)) {
        n = 1 << stdl_planes;
    }
    for (i = 0; i < n; i++) {
        int32_t dr = (int32_t)r - cols[i].r;
        int32_t dg = (int32_t)g - cols[i].g;
        int32_t db = (int32_t)b - cols[i].b;
        int32_t dist = dr * dr + dg * dg + db * db;
        if (dist < bestdist) {
            bestdist = dist;
            best = (uint8_t)i;
        }
    }
    return best;
}

void STDL_GetRGB(uint32_t index, const STDL_PixelFormat *fmt,
                 uint8_t *r, uint8_t *g, uint8_t *b)
{
    const STDL_Colour *cols = stdl.colours;
    uint32_t n = 16;

    if (fmt != NULL && fmt->palette != NULL) {
        cols = fmt->palette->colors;
        n = (uint32_t)fmt->palette->ncolors;
    }
    if (index >= n) {
        index = 0;
    }
    *r = cols[index].r;
    *g = cols[index].g;
    *b = cols[index].b;
}

void STDL_FadeTo(const STDL_Palette *target, int frames)
{
    STDL_Colour from[16];
    int f, i, n;

    if (target == NULL || frames <= 0) {
        return;
    }
    n = target->ncolors > 16 ? 16 : target->ncolors;
    for (i = 0; i < 16; i++) {
        from[i] = stdl.colours[i];
    }
    for (f = 1; f <= frames; f++) {
        STDL_WaitVBL();
        for (i = 0; i < n; i++) {
            stdl.colours[i].r = (uint8_t)(from[i].r
                + ((target->colors[i].r - from[i].r) * f) / frames);
            stdl.colours[i].g = (uint8_t)(from[i].g
                + ((target->colors[i].g - from[i].g) * f) / frames);
            stdl.colours[i].b = (uint8_t)(from[i].b
                + ((target->colors[i].b - from[i].b) * f) / frames);
        }
        stdl_palette_apply_hw();
    }
}
