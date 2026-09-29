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

/*
 * A masked tile's rows: stored like a sprite's, [mask][p0..p3] per
 * group with the planes clear under the mask. The mask is read first:
 * a transparent group's planes are never fetched, and an opaque one -
 * the inside of most tiles - is plain stores with no read of the
 * destination. The rest merge a plane pair at a time as
 * (d & mask) | s, the mask in both halves of a register.
 *
 * Hand-written for the 68000: gcc 4.6 kept the mask and the loop
 * counts on the stack and merged a word at a time, 737us for a 16x16
 * tile on a plain ST. n >= 1 groups a row, rows >= 1; the skips are
 * the source and destination strides less what a row walked. Budget
 * 2 merges and copies planes 0 and 1 only. Out of line so that the
 * unmasked tile's path through STDL_BlitTile keeps its registers.
 * The C twin below is what tests/host exercises; PIXCHK runs the
 * same tests on the target.
 */
#ifdef __m68k__
#define TILE_PAIR \
    "move.l (%[d]),%%d2\n\t" \
    "and.l  %%d1,%%d2\n\t" \
    "or.l   (%[s])+,%%d2\n\t" \
    "move.l %%d2,(%[d])+\n\t"
#define TILE_SKIP2 \
    "addq.l #4,%[s]\n\t" \
    "addq.l #4,%[d]\n\t"
#define TILE_ROWS(MERGE, COPY) \
    "1:\n\t" \
    "move.w %[n],%%d3\n" \
    "2:\n\t" \
    "move.w (%[s])+,%%d0\n\t" \
    "beq.s  5f\n\t" \
    "cmp.w  #-1,%%d0\n\t" \
    "beq.s  6f\n\t" \
    "move.w %%d0,%%d1\n\t" \
    "swap   %%d1\n\t" \
    "move.w %%d0,%%d1\n\t" \
    MERGE \
    "bra.s  7f\n" \
    "5:\n\t" \
    COPY \
    "bra.s  7f\n" \
    "6:\n\t" \
    "addq.l #8,%[s]\n\t" \
    "addq.l #8,%[d]\n" \
    "7:\n\t" \
    "dbf    %%d3,2b\n\t" \
    "adda.l %[sk],%[s]\n\t" \
    "adda.l %[dk],%[d]\n\t" \
    "subq.w #1,%[h]\n\t" \
    "bne.s  1b"
#endif

static __attribute__((noinline)) void tile_rows_masked(
        const uint16_t *src, uint8_t *drow, int dstride, int rows, int n,
        uint32_t srcadv, const int np)
{
#ifdef __m68k__
    int16_t h = (int16_t)rows;
    const int16_t cnt = (int16_t)(n - 1);
    const int32_t sskip = (int32_t)(srcadv * 2) - n * 10;
    const int32_t dskip = (int32_t)dstride - n * 8;

    if (np > 2) {
        __asm__ volatile(
            TILE_ROWS(TILE_PAIR TILE_PAIR,
                      "move.l (%[s])+,(%[d])+\n\t"
                      "move.l (%[s])+,(%[d])+\n\t")
            : [s] "+a"(src), [d] "+a"(drow), [h] "+d"(h)
            : [n] "d"(cnt), [sk] "a"(sskip), [dk] "a"(dskip)
            : "d0", "d1", "d2", "d3", "cc", "memory");
    } else {
        __asm__ volatile(
            TILE_ROWS(TILE_PAIR TILE_SKIP2,
                      "move.l (%[s])+,(%[d])+\n\t"
                      TILE_SKIP2)
            : [s] "+a"(src), [d] "+a"(drow), [h] "+d"(h)
            : [n] "d"(cnt), [sk] "a"(sskip), [dk] "a"(dskip)
            : "d0", "d1", "d2", "d3", "cc", "memory");
    }
#else
    /* C twin - what tests/host exercises and the asm must match */
    int yy, k;

    for (yy = 0; yy < rows; yy++) {
        const uint16_t *sg = src;
        uint16_t *d = (uint16_t *)drow;

        for (k = n; k > 0; k--) {
            const uint16_t m = sg[0];

            if (m == 0) {
                d[0] = sg[1];
                d[1] = sg[2];
                if (np > 2) d[2] = sg[3];
                if (np > 2) d[3] = sg[4];
            } else if (m != 0xFFFFu) {
                d[0] = (uint16_t)((d[0] & m) | sg[1]);
                d[1] = (uint16_t)((d[1] & m) | sg[2]);
                if (np > 2) d[2] = (uint16_t)((d[2] & m) | sg[3]);
                if (np > 2) d[3] = (uint16_t)((d[3] & m) | sg[4]);
            }
            sg += 5;
            d += 4;
        }
        src += srcadv;
        drow += dstride;
    }
#endif
}

/* budget 1 or 2: the low plane pair of each group, a long each */
static void tile_rows_low(uint8_t *drow, const uint8_t *srow, int n,
                          int rows, int sstride, int dstride)
{
    do {
        uint8_t *d = drow;
        const uint8_t *sp = srow;
        int k = n;

        do {
            *(stdl_wlong *)d = *(const stdl_wlong *)sp;
            d += 8;
            sp += 8;
        } while (--k != 0);
        drow += dstride;
        srow += sstride;
    } while (--rows != 0);
}

/*
 * x is rounded down to a group boundary: tiles are the aligned fast
 * path by definition. Use sprites for free positioning.
 *
 * An unmasked tile is rows of whole groups, and they go through the
 * library's register-only row copy (stdl_copy_rows_groups). It
 * replaced a loop that stored the planes a word at a time and asked,
 * for every group of every row, whether the tile was masked: 654us
 * for a 16x16 tile on a plain ST.
 *
 * At budget 1 or 2 only the low plane pair is copied, as before that
 * change. Tile art is not normalised to the budget when a tileset is
 * built or loaded, so a tileset made with colours 4-15 still has
 * planes 2 and 3 set, and copying them would draw colours the budget
 * masks off everywhere else and leave bits in the destination that no
 * later drawing at that budget clears. Budget 3 copies all four, as it
 * always did.
 */
void STDL_BlitTile(STDL_Tileset *ts, int index, STDL_Surface *dst,
                   int x, int y)
{
    int row0, row1, g0, g1, gx0, n;
    const uint16_t *src;
    uint8_t *drow;

    if (ts == NULL || dst == NULL || index < 0
        || index >= ts->ntiles) {
        return;
    }
    gx0 = x >> 4;

    row0 = 0;
    row1 = ts->th;
    if (y < dst->clip.y) row0 = dst->clip.y - y;
    if (y + row1 > dst->clip.y + dst->clip.h)
        row1 = dst->clip.y + dst->clip.h - y;
    if (row0 >= row1) {
        return;
    }
    /* whole groups only: one cut by the clip rectangle is dropped */
    g0 = 0;
    g1 = ts->groups;
    while (g0 < g1 && (gx0 + g0) * 16 < dst->clip.x) g0++;
    while (g1 > g0
           && (gx0 + g1 - 1) * 16 + 15
              >= dst->clip.x + dst->clip.w) g1--;
    if (g0 >= g1) {
        return;
    }
    n = g1 - g0;
    drow = dst->pixels + stdl_row_off(y + row0, dst->stride)
         + (gx0 + g0) * 8;

    if (!ts->masked) {
        /* a tile row is groups * 4 words, rows back to back */
        const int rowbytes = ts->groups * 8;

        src = ts->data + stdl_row_off(index, (uint16_t)ts->tilesize)
            + stdl_row_off(row0, (uint16_t)(ts->groups * 4)) + g0 * 4;
        if (stdl_planes > 2) {
            stdl_copy_rows_groups(drow, (const uint8_t *)src, n,
                                  row1 - row0,
                                  (int32_t)(rowbytes - n * 8),
                                  (int32_t)dst->stride - n * 8);
        } else {
            tile_rows_low(drow, (const uint8_t *)src, n, row1 - row0,
                          rowbytes, dst->stride);
        }
    } else {
        const uint32_t srcadv = (uint32_t)ts->groups * 5;

        src = ts->data + stdl_row_off(index, (uint16_t)ts->tilesize)
            + stdl_row_off(row0, (uint16_t)srcadv) + g0 * 5;
        tile_rows_masked(src, drow, dst->stride, row1 - row0, n,
                         srcadv, stdl_planes);
    }
}
