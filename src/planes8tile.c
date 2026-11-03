/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Tiles on 8 planes (stdl_responsive.h): a tileset cut from an 8-plane
 * surface, and tiles of either kind drawn onto one (tileset.c hands
 * them here). An 8-plane tile's group is a surface's, four longs,
 * planes 0-1 to 6-7; a masked tile's has its mask in front, a long with
 * the transparent bits set in both halves and the planes clear under
 * it, as a sprite's (planes8.c). A tile draws at a group boundary, and
 * a group the clip rectangle cuts is dropped, as on 4 planes. Its own
 * object, so planes8.c's code stays as it was; every program asking
 * for the colour links it, through the 8-plane table, so it is built
 * for size (the Makefile's -Os): 1,524 bytes where -O2 made 1,808, the
 * rows' merges the same instructions, the count a subtract and compare
 * where -O2 had a dbf.
 */

#include <stdlib.h>
#include "responsive.h"

STDL_Tileset *p8_tileset_from(const STDL_Surface *s, int tw, int th)
{
    const int groups = tw >> 4, masked = s->mask != NULL;
    STDL_Tileset *ts;
    uint32_t *out;
    int cols, rows, ty, tx, y, y0, gx, g, k;

    /* tileset.c has checked the tile's size */
    cols = STDL_DivU16((uint16_t)s->w, (unsigned)tw);
    rows = STDL_DivU16((uint16_t)s->h, (unsigned)th);
    if (cols < 1 || rows < 1) {
        STDL_SetError("surface smaller than one tile");
        return NULL;
    }
    ts = calloc(1, sizeof(*ts));
    if (ts == NULL) {
        STDL_SetError("out of memory");
        return NULL;
    }
    ts->tw = (int16_t)tw;
    ts->th = (int16_t)th;
    ts->ntiles = (uint16_t)STDL_MulU16((unsigned)cols, (unsigned)rows);
    ts->groups = (uint16_t)groups;
    ts->masked = (uint8_t)masked;
    ts->planes = 8;
    /* in words, as a 4-plane tileset's */
    ts->tilesize = STDL_MulU16(STDL_MulU16((unsigned)groups,
                                           masked ? 10 : 8), (unsigned)th);
    ts->data = malloc(stdl_row_off(ts->ntiles, (uint16_t)ts->tilesize) << 1);
    if (ts->data == NULL) {
        free(ts);
        STDL_SetError("out of memory");
        return NULL;
    }

    /* the tiles across the surface, then down; load time, so each row
     * found by its offset */
    out = (uint32_t *)ts->data;
    for (ty = 0, y0 = 0; ty < rows; ty++, y0 += th) {
        for (tx = 0, gx = 0; tx < cols; tx++, gx += groups) {
            for (y = y0; y < y0 + th; y++) {
                const stdl_wlong *grp = (const stdl_wlong *)(s->pixels
                    + stdl_row_off(y, s->stride)) + (gx << 2);
                const uint16_t *m = masked
                    ? (const uint16_t *)(s->mask
                                         + stdl_row_off(y, s->maskstride)) + gx
                    : NULL;

                for (g = 0; g < groups; g++, grp += 4) {
                    uint32_t nm = 0;

                    if (masked) {
                        nm = STDL_PACK2(m[g], m[g]);
                        *out++ = nm;
                    }
                    for (k = 0; k < 4; k++) {
                        *out++ = grp[k] & ~nm;
                    }
                }
            }
        }
    }
    return ts;
}

void p8_free_tileset(STDL_Tileset *ts)
{
    free(ts->data);
    free(ts);
}

/* rows of a 4-plane tile onto 8 planes, n groups each: its colours
 * 0-15, the upper planes cleared where it draws; a group [mask][p0-p3]
 * with the planes clear under the mask, or [p0-p3] where it has none,
 * drawn as one whose mask is clear */
static void p8_tile_rows4(const uint16_t *src, int masked, int rowwords,
                          uint8_t *drow, int n, int rows, int stride)
{
    const int sw = 4 + masked;
    int k;

    for (; --rows >= 0; src += rowwords, drow += stride) {
        const uint16_t *s = src;
        stdl_wlong *dg = (stdl_wlong *)drow;

        for (k = n; --k >= 0; s += sw, dg += 4) {
            const unsigned m = masked ? s[0] : 0;
            const uint16_t *pl = s + masked;
            const uint32_t nm = STDL_PACK2(m, m);

            if (m != 0xFFFFu) {
                dg[0] = (dg[0] & nm) | STDL_PACK2(pl[0], pl[1]);
                dg[1] = (dg[1] & nm) | STDL_PACK2(pl[2], pl[3]);
                dg[2] &= nm;
                dg[3] &= nm;
            }
        }
    }
}

/* rows of a masked 8-plane tile, n groups each */
static void p8_tile_rows8(const uint32_t *src, uint8_t *drow, int n,
                          int rows, int rowlongs, int stride)
{
    int k;

    for (; --rows >= 0; src += rowlongs, drow += stride) {
        const uint32_t *s = src;
        stdl_wlong *dg = (stdl_wlong *)drow;

        for (k = n; --k >= 0; s += 5, dg += 4) {
            const uint32_t nm = s[0];

            if (nm == 0) {
                dg[0] = s[1]; dg[1] = s[2]; dg[2] = s[3]; dg[3] = s[4];
            } else if (nm != 0xFFFFFFFFUL) {
                dg[0] = (dg[0] & nm) | s[1];
                dg[1] = (dg[1] & nm) | s[2];
                dg[2] = (dg[2] & nm) | s[3];
                dg[3] = (dg[3] & nm) | s[4];
            }
        }
    }
}

void p8_blit_tile(STDL_Tileset *ts, int index, STDL_Surface *d, int x,
                  int y)
{
    const int gx0 = x >> 4, cx1 = d->clip.x, cx2 = d->clip.x + d->clip.w;
    const int masked = ts->masked != 0;
    /* words a group: a 4-plane tile's 4 and a mask word, an 8-plane
     * one's 8 and a mask long */
    const unsigned sw = ts->planes == 8 ? 8 + (masked << 1) : 4 + masked;
    const unsigned rowwords = STDL_MulU16(ts->groups, sw);
    int row0 = 0, row1 = ts->th, g0 = 0, g1 = ts->groups, n;
    const uint16_t *src;
    uint8_t *drow;

    if (!STDL_IS_RESP(d)) {
        STDL_SetError("an 8-plane tile onto a 4-plane surface");
        return;
    }
    if (y < d->clip.y) row0 = d->clip.y - y;
    if (y + row1 > d->clip.y + d->clip.h) row1 = d->clip.y + d->clip.h - y;
    while (g0 < g1 && (gx0 + g0) * 16 < cx1) g0++;
    while (g1 > g0 && (gx0 + g1 - 1) * 16 + 15 >= cx2) g1--;
    if (row0 >= row1 || g0 >= g1) {
        return;
    }
    n = g1 - g0;
    src = ts->data + stdl_row_off(index, (uint16_t)ts->tilesize)
        + stdl_row_off(row0, (uint16_t)rowwords) + STDL_MulU16(g0, sw);
    drow = d->pixels + stdl_row_off(y + row0, d->stride) + (gx0 + g0) * 16;
    if (ts->planes != 8) {
        p8_tile_rows4(src, masked, rowwords, drow, n, row1 - row0,
                      d->stride);
    } else if (!masked) {
        /* a 16-byte group is two of the row copy's 8-byte ones */
        stdl_copy_rows_groups(drow, (const uint8_t *)src, n << 1,
                              row1 - row0,
                              (int32_t)((rowwords << 1) - (n << 4)),
                              (int32_t)d->stride - (n << 4));
    } else {
        p8_tile_rows8((const uint32_t *)src, drow, n, row1 - row0,
                      rowwords >> 1, d->stride);
    }
    resp_touch(d, (gx0 + g0) * 16, y + row0, (gx0 + g1) * 16 - 1,
               y + row1 - 1);
}
