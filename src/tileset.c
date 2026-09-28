/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Tilesets: whole-group tiles cut from a surface, and the aligned
 * tile blit.
 *
 * Its own translation unit, out of sprite.c, because the linker's
 * granularity on this toolchain is the object file: a port that
 * draws sprites but no tiles no longer carries the tile code.
 */

#include <stdlib.h>
#include "stdl_internal.h"

STDL_Tileset *STDL_TilesetFromSurface(const STDL_Surface *s, int tw,
                                      int th)
{
    STDL_Tileset *ts;
    int groups, cols, rows, tx, ty, y, g, p;
    int masked;
    uint16_t *out;
    const uint8_t *rowbase, *mrowbase;

    if (s == NULL || tw <= 0 || th <= 0 || (tw & 15) != 0) {
        STDL_SetError("tiles must be a multiple of 16 wide");
        return NULL;
    }
    cols = s->w / tw;
    rows = s->h / th;
    if (cols < 1 || rows < 1) {
        STDL_SetError("surface smaller than one tile");
        return NULL;
    }
    masked = (s->mask != NULL) ? 1 : 0;
    groups = tw >> 4;

    ts = calloc(1, sizeof(STDL_Tileset));
    if (ts == NULL) {
        STDL_SetError("out of memory");
        return NULL;
    }
    ts->tw = (int16_t)tw;
    ts->th = (int16_t)th;
    ts->ntiles = (uint16_t)(cols * rows);
    ts->groups = (uint16_t)groups;
    ts->masked = (uint8_t)masked;
    ts->planes = 4;
    ts->tilesize = (uint32_t)groups * (masked ? 5 : 4) * th;
    ts->data = malloc(ts->tilesize * ts->ntiles * 2);
    if (ts->data == NULL) {
        free(ts);
        STDL_SetError("out of memory");
        return NULL;
    }

    /*
     * Walk the source rather than addressing it: the obvious form
     * costs two __mulsi3 calls per tile row (~270 cycles each on an
     * 8MHz 68000) plus a division per tile for the tile's column.
     * Nesting the tile loops removes the division; carrying the row
     * bases removes the multiplies.
     */
    out = ts->data;
    rowbase = s->pixels;
    mrowbase = s->mask;
    for (ty = 0; ty < rows; ty++) {
        for (tx = 0; tx < cols; tx++) {
            const uint8_t *row = rowbase + tx * groups * 8;
            const uint8_t *mbase = masked
                ? mrowbase + tx * groups * 2 : NULL;

            for (y = 0; y < th; y++) {
                const uint16_t *mrow = (const uint16_t *)mbase;
                for (g = 0; g < groups; g++) {
                    const uint16_t *grp =
                        (const uint16_t *)(row + g * 8);
                    uint16_t mask = masked ? mrow[g] : 0;
                    if (masked) {
                        *out++ = mask;
                    }
                    for (p = 0; p < 4; p++) {
                        *out++ = (uint16_t)(grp[p] & ~mask);
                    }
                }
                row += s->stride;
                if (masked) {
                    mbase += s->maskstride;
                }
            }
        }
        rowbase += (uint32_t)(uint16_t)th * s->stride;
        if (masked) {
            mrowbase += (uint32_t)(uint16_t)th * s->maskstride;
        }
    }
    return ts;
}

void STDL_FreeTileset(STDL_Tileset *ts)
{
    if (ts != NULL) {
        free(ts->data);
        free(ts);
    }
}

/* x is rounded down to a group boundary: tiles are the aligned fast
 * path by definition. Use sprites for free positioning. */
STDL_PLANE_INLINE void blit_tile_rows(const uint16_t *src,
                              uint8_t *drow, int dstride, int rows,
                              int gx0, int g0, int g1, int tsgroups,
                              int words, int masked, const int np)
{
    /* every product here is 16x16 (groups, words per group): as
     * plain int arithmetic gcc 4.6 makes each a __mulsi3 call, per
     * group per row */
    const uint32_t srcadv = stdl_row_off(tsgroups, (uint16_t)words);
    const uint16_t *sg0 = src + stdl_row_off(g0, (uint16_t)words);
    int yy, g;

    for (yy = 0; yy < rows; yy++) {
        const uint16_t *sg = sg0;
        uint16_t *dgrp = (uint16_t *)(drow + (gx0 + g0) * 8);
        for (g = g0; g < g1; g++, sg += words, dgrp += 4) {
            if (masked) {
                uint16_t m = sg[0];
                dgrp[0] = (uint16_t)((dgrp[0] & m) | sg[1]);
                if (np > 1) dgrp[1] = (uint16_t)((dgrp[1] & m) | sg[2]);
                if (np > 2) dgrp[2] = (uint16_t)((dgrp[2] & m) | sg[3]);
                if (np > 3) dgrp[3] = (uint16_t)((dgrp[3] & m) | sg[4]);
            } else {
                dgrp[0] = sg[0];
                if (np > 1) dgrp[1] = sg[1];
                if (np > 2) dgrp[2] = sg[2];
                if (np > 3) dgrp[3] = sg[3];
            }
        }
        sg0 += srcadv;
        drow += dstride;
    }
}

void STDL_BlitTile(STDL_Tileset *ts, int index, STDL_Surface *dst,
                   int x, int y)
{
    int row0, row1, g0, g1, gx0, words, np;
    const uint16_t *tdata;

    if (ts == NULL || dst == NULL || index < 0
        || index >= ts->ntiles) {
        return;
    }
    x &= ~15;
    gx0 = x >> 4;
    words = ts->masked ? 5 : 4;

    row0 = 0;
    row1 = ts->th;
    if (y < dst->clip.y) row0 = dst->clip.y - y;
    if (y + row1 > dst->clip.y + dst->clip.h)
        row1 = dst->clip.y + dst->clip.h - y;
    if (row0 >= row1) {
        return;
    }
    g0 = 0;
    g1 = ts->groups;
    while (g0 < g1 && (gx0 + g0) * 16 < dst->clip.x) g0++;
    while (g1 > g0
           && (gx0 + g1 - 1) * 16 + 15
              >= dst->clip.x + dst->clip.w) g1--;
    if (g0 >= g1) {
        return;
    }

    tdata = ts->data + stdl_row_off(index, (uint16_t)ts->tilesize);
    {
    const uint16_t *src =
        tdata + stdl_row_off(row0, (uint16_t)stdl_row_off(ts->groups, (uint16_t)words));
    uint8_t *drow =
        dst->pixels + stdl_row_off(y + row0, dst->stride);

    np = stdl_planes;
#define TILE_ROWS(np) \
    blit_tile_rows(src, drow, dst->stride, row1 - row0, gx0, g0, g1, \
                   ts->groups, words, ts->masked, (np))
    STDL_PLANE_DISPATCH(np, TILE_ROWS);
#undef TILE_ROWS
    }
}
