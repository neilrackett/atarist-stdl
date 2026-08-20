/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_BlitIndexed8: draw byte-per-pixel (chunky) frames at frame
 * rate, through a per-call colour map.
 *
 * 90s engines RLE-decode their sprite frames into chunky scratch at
 * draw time; converting each frame to a surface first
 * (STDL_SurfaceFromIndexed8) allocates and converts twice per draw.
 * This is the direct path: one pass from the chunky bytes into the
 * destination's planes, with transparency on value 0, optional
 * horizontal flip and column-major source walks (rotated storage),
 * and the destination mask either maintained, marked, or consulted
 * as per-pixel protection (STDL_I8_UNDER - the "sprites pass behind
 * marked foreground" idiom).
 *
 * Ported from (and first proven in) the REminiscence conversion.
 */

#include "stdl_internal.h"

/*
 * Positioned-pattern table: for a pixel at position i of a group
 * with mapped colour v, cell[i][v] holds the two plane-pair longs
 * and the drawn bit, all pre-positioned. The inner loop then only
 * ORs three memory operands into three accumulators - measured at
 * ~2.5x the branch-per-plane version on a 68000, where gcc 4.6
 * spills half of five separate accumulators to the stack. `dr` is
 * the position bit for every v (value 0 through the map still
 * draws; only source byte 0 is transparent).
 *
 * Cells are 16 bytes so the value index is a shift, and rows are
 * 256 bytes so the position walk is one add.
 */
typedef struct {
    uint32_t d01, d23;
    uint16_t dr;
    uint16_t pad[3];
} b8cell;

static b8cell b8tab[16][16];
static int b8tab_ready;

static void b8tab_build(void)
{
    int i, v;

    for (i = 0; i < 16; i++) {
        uint16_t bit = (uint16_t)(0x8000u >> i);
        for (v = 0; v < 16; v++) {
            b8cell *e = &b8tab[i][v];
            e->d01 = ((v & 1) ? (uint32_t)bit << 16 : 0)
                   | ((v & 2) ? bit : 0);
            e->d23 = ((v & 4) ? (uint32_t)bit << 16 : 0)
                   | ((v & 8) ? bit : 0);
            e->dr = bit;
        }
    }
    b8tab_ready = 1;
}

void STDL_BlitIndexed8(STDL_Surface *dst, const uint8_t *src,
                       int pitch, int x, int y, int w, int h,
                       const uint8_t *map, unsigned flags)
{
    uint16_t smap[16];
    int step, rowadv, cx1, cy1, cx2, cy2, gx0, gx1, i;
    uint8_t *drow;
    uint8_t *mrow;
    int mstride;

    if (dst == NULL || src == NULL || map == NULL) {
        return;
    }
    if (!b8tab_ready) {
        b8tab_build();
    }

    /* per-variant source walk: `step` from pixel to pixel inside a
     * row, `rowadv` from row to row */
    if (flags & STDL_I8_COLMAJOR) {
        step = (flags & STDL_I8_XFLIP) ? -pitch : pitch;
        rowadv = 1;
    } else {
        step = (flags & STDL_I8_XFLIP) ? -1 : 1;
        rowadv = pitch;
    }

    x -= dst->org_x;
    y -= dst->org_y;

    /* clip against the destination clip rectangle, bias the source
     * pointer for the pixels and rows skipped */
    cx1 = dst->clip.x;
    cy1 = dst->clip.y;
    cx2 = dst->clip.x + dst->clip.w;
    cy2 = dst->clip.y + dst->clip.h;
    if (x < cx1) {
        src += (cx1 - x) * step;
        w -= cx1 - x;
        x = cx1;
    }
    if (y < cy1) {
        src += (cy1 - y) * rowadv;
        h -= cy1 - y;
        y = cy1;
    }
    if (x + w > cx2) {
        w = cx2 - x;
    }
    if (y + h > cy2) {
        h = cy2 - y;
    }
    if (w <= 0 || h <= 0) {
        return;
    }

    /* colours above the plane budget are truncated (docs/format.md);
     * the map is folded into byte offsets of a table row here, so
     * the loop pays one lookup, not lookup-then-scale */
    for (i = 0; i < 16; i++) {
        smap[i] = (uint16_t)((map[i] & STDL_COL_MASK) << 4);
    }

    gx0 = x >> 4;
    gx1 = (x + w - 1) >> 4;
    drow = dst->pixels + stdl_row_off(y, dst->stride) + gx0 * 8;
    if (dst->mask != NULL) {
        mrow = dst->mask + stdl_row_off(y, dst->maskstride) + gx0 * 2;
        mstride = dst->maskstride;
        dst->opaque_state = 0;
    } else {
        mrow = NULL;
        mstride = 0;
        flags &= ~(STDL_I8_UNDER | STDL_I8_MARK);
    }

    for (; h > 0; h--) {
        const uint8_t *p = src;
        uint16_t *grp = (uint16_t *)drow;
        uint16_t *mw = (uint16_t *)mrow;
        int g;

        for (g = gx0; g <= gx1; g++) {
            /* pixel range of this group overlapping the blit */
            int base = (g << 4) - x;
            int i0 = base < 0 ? -base : 0;
            int i1 = w - base;
            uint32_t a01 = 0, a23 = 0;
            uint16_t drawn = 0;
            const uint8_t *cell;
            int n;

            if (i1 > 16) {
                i1 = 16;
            }
            cell = (const uint8_t *)&b8tab[i0][0];
            n = i1 - i0;
#ifdef __m68k__
            /*
             * The pixel gather, by hand: gcc 4.6 spills the
             * accumulators, keeps smap on the stack and re-derives
             * the source pointer with a __mulsi3 call per group -
             * measured at ~210 cycles per pixel against ~100 for
             * this. The C build below is the reference the host
             * tests exercise; BENCH8-style on-target runs compare
             * the two.
             */
            if (n > 0) {
                int cnt = n - 1;
                uint32_t vdr = 0;
                __asm__ volatile(
                    "1:\n\t"
                    "moveq #0,%%d0\n\t"
                    "move.b (%0),%%d0\n\t"
                    "adda.l %6,%0\n\t"
                    "jeq 2f\n\t"
                    "and.w #15,%%d0\n\t"
                    "add.w %%d0,%%d0\n\t"
                    "move.w (%7,%%d0.w),%%d0\n\t"
                    "or.l (%1,%%d0.w),%2\n\t"
                    "or.l 4(%1,%%d0.w),%3\n\t"
                    "or.w 8(%1,%%d0.w),%4\n\t"
                    "2:\n\t"
                    "lea 256(%1),%1\n\t"
                    "dbra %5,1b"
                    : "+a"(p), "+a"(cell), "+d"(a01), "+d"(a23),
                      "+d"(vdr), "+d"(cnt)
                    : "r"((long)step), "a"(smap)
                    : "d0", "cc", "memory");
                drawn = (uint16_t)vdr;
            }
#else
            for (; --n >= 0; cell += sizeof(b8tab[0])) {
                uint8_t c = *p;
                p += step;
                if (c) {
                    const b8cell *e =
                        (const b8cell *)(cell + smap[c & 15]);
                    a01 |= e->d01;
                    a23 |= e->d23;
                    drawn |= e->dr;
                }
            }
#endif
            if (drawn) {
                uint16_t d0 = (uint16_t)(a01 >> 16);
                uint16_t d1 = (uint16_t)a01;
                uint16_t d2 = (uint16_t)(a23 >> 16);
                uint16_t d3 = (uint16_t)a23;
                if (flags & STDL_I8_UNDER) {
                    drawn &= (uint16_t)~*mw;
                }
                if (drawn) {
                    uint16_t keep = (uint16_t)~drawn;
                    grp[0] = (uint16_t)((grp[0] & keep) | (d0 & drawn));
                    grp[1] = (uint16_t)((grp[1] & keep) | (d1 & drawn));
                    grp[2] = (uint16_t)((grp[2] & keep) | (d2 & drawn));
                    grp[3] = (uint16_t)((grp[3] & keep) | (d3 & drawn));
                    if (mw != NULL) {
                        if (flags & STDL_I8_MARK) {
                            *mw |= drawn;
                        } else {
                            *mw &= keep;
                        }
                    }
                }
            }
            grp += 4;
            if (mw != NULL) {
                mw++;
            }
        }
        src += rowadv;
        drow += dst->stride;
        if (mrow != NULL) {
            mrow += mstride;
        }
    }
}
