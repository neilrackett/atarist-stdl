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

void STDL_BlitIndexed8(STDL_Surface *dst, const uint8_t *src,
                       int pitch, int x, int y, int w, int h,
                       const uint8_t *map, unsigned flags)
{
    uint8_t m16[16];
    int step, rowadv, cx1, cy1, cx2, cy2, gx0, gx1, i;
    uint8_t *drow;
    uint8_t *mrow;
    int mstride;

    if (dst == NULL || src == NULL || map == NULL) {
        return;
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
     * masking the map once keeps the high-plane invariant without
     * per-plane branches in the loop */
    for (i = 0; i < 16; i++) {
        m16[i] = (uint8_t)(map[i] & STDL_COL_MASK);
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
            uint16_t drawn = 0, d0 = 0, d1 = 0, d2 = 0, d3 = 0;
            uint16_t bit = (uint16_t)(0x8000u >> i0);
            int n;

            if (i1 > 16) {
                i1 = 16;
            }
            for (n = i1 - i0; --n >= 0; bit >>= 1) {
                uint8_t c = *p;
                p += step;
                if (c) {
                    uint8_t v = m16[c & 15];
                    drawn |= bit;
                    if (v & 1) d0 |= bit;
                    if (v & 2) d1 |= bit;
                    if (v & 4) d2 |= bit;
                    if (v & 8) d3 |= bit;
                }
            }
            if (drawn) {
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
