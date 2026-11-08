/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_BLIT_UNDER and STDL_BLIT_MARK onto an 8-plane surface with a
 * mask (planes8.c's blit hands them here), for a layer whose mask is a
 * priority plane. An object of its own, built for size (the Makefile's
 * -Os): 1.3K, where -O2 made it 1.7K, in every program asking for the
 * colour. gcc 4.6's optimize attribute did the same inside planes8.c,
 * but it changed the code of every function in that file: the sprite
 * blit's, its source untouched, merged two tails and took a branch more
 * a row.
 */

#include "responsive.h"

const uint16_t p8_zero[8];          /* responsive.h: P8_WIN's zero group */

/*
 * As the 4-plane CPU blit draws one (blit.c's copy_group): a group at a
 * time from a source of 4 planes or 8, shifted or not, keyed or not;
 * under UNDER the destination's marked pixels keep themselves, and the
 * mask is set under what was drawn for MARK, cleared there with
 * neither. The source's group index steps by one a group, so the two
 * groups it straddles are found once, and a plane's word is one shift
 * of a 32-bit window. Kept out of p8_blit: a change in its rows
 * reshaped its registers, which a TT's restores pay for every frame,
 * and so did a call after its clip, so this clips for itself.
 */
int p8_blit_ex(const STDL_Surface *src, const STDL_Rect *srcrect,
               STDL_Surface *dst, STDL_Rect *dstrect, unsigned flags)
{
    int sx = 0, sy = 0, w = src->w, h = src->h, dx = 0, dy = 0;
    int g0, g1, off, sh, ss, np, masked, y, g, k;
    unsigned sng;

    if (srcrect != NULL) {
        sx = srcrect->x;
        sy = srcrect->y;
        w = srcrect->w;
        h = srcrect->h;
    }
    if (dstrect != NULL) {
        dx = dstrect->x;
        dy = dstrect->y;
    }
    sx -= src->org_x;
    sy -= src->org_y;
    dx -= dst->org_x;
    dy -= dst->org_y;
    if (sx < 0) { w += sx; dx -= sx; sx = 0; }
    if (sy < 0) { h += sy; dy -= sy; sy = 0; }
    if (sx + w > src->w) w = src->w - sx;
    if (sy + h > src->h) h = src->h - sy;
    if (dx < dst->clip.x) {
        w -= dst->clip.x - dx; sx += dst->clip.x - dx; dx = dst->clip.x;
    }
    if (dy < dst->clip.y) {
        h -= dst->clip.y - dy; sy += dst->clip.y - dy; dy = dst->clip.y;
    }
    if (dx + w > dst->clip.x + dst->clip.w) w = dst->clip.x + dst->clip.w - dx;
    if (dy + h > dst->clip.y + dst->clip.h) h = dst->clip.y + dst->clip.h - dy;
    if (w <= 0 || h <= 0) {
        if (dstrect != NULL) {
            dstrect->w = 0;
            dstrect->h = 0;
        }
        return 0;
    }
    if (dstrect != NULL) {
        dstrect->x = (int16_t)(dx + dst->org_x);
        dstrect->y = (int16_t)(dy + dst->org_y);
        dstrect->w = (uint16_t)w;
        dstrect->h = (uint16_t)h;
    }
    g0 = dx >> 4;
    g1 = (dx + w - 1) >> 4;
    off = sx - dx;
    sh = off & 15;
    ss = STDL_IS_RESP(src) ? 3 : 2;             /* a source group's words */
    np = 1 << ss;
    sng = (unsigned)src->stride >> (ss + 1);
    masked = (src->flags & STDL_SRCKEY) && src->mask != NULL;
    /* onto its own memory with the destination at the higher address,
     * done already (overlap.c) */
    y = (src == dst || ((src->flags | dst->flags) & STDL_PREALLOC))
        && stdl_blit_overlap8(src, dst, sx, sy, dx, dy, w, h, flags) == 0
        ? h : 0;
    for (; y < h; y++) {
        const uint16_t *sw = (const uint16_t *)(src->pixels
                                 + stdl_row_off(sy + y, src->stride));
        const uint16_t *sm = masked
            ? (const uint16_t *)(src->mask
                                 + stdl_row_off(sy + y, src->maskstride))
            : NULL;
        uint8_t *drow = dst->pixels + stdl_row_off(dy + y, dst->stride);
        uint16_t *dm = (uint16_t *)(dst->mask
                                    + stdl_row_off(dy + y, dst->maskstride));
        unsigned wi = (unsigned)(g0 + (off >> 4));

        for (g = g0; g <= g1; g++, wi++) {
            const int in0 = wi < sng, in1 = wi + 1 < sng;
            const uint16_t *a = in0 ? sw + (wi << ss) : p8_zero;
            const uint16_t *b = in1 ? sw + ((wi + 1) << ss) : p8_zero;
            uint32_t *d = (uint32_t *)(drow + (g << 4));
            uint32_t em = 0xFFFFu, m;

            if (g == g0) em &= 0xFFFFu >> (dx & 15);
            if (g == g1) em &= 0xFFFFu << (15 - ((dx + w - 1) & 15));
            if (flags & STDL_BLIT_UNDER) {
                em &= ~(uint32_t)dm[g];     /* marked pixels keep themselves */
            }
            if (sm != NULL && em != 0) {   /* past an edge is transparent */
                em &= ~P8_WIN(in0 ? sm[wi] : 0xFFFFu,
                              in1 ? sm[wi + 1] : 0xFFFFu, sh);
            }
            if (em == 0) {
                continue;
            }
            /* a long is two planes' words, under the mask in both halves;
             * a 4-plane source's upper four planes are zero */
            m = em | (em << 16);
            for (k = 0; k < 4; k++) {
                const int p = 2 * k;
                const uint32_t s = p < np
                    ? STDL_PACK2(P8_WIN(a[p], b[p], sh),
                                 P8_WIN(a[p + 1], b[p + 1], sh))
                    : 0;

                d[k] ^= (d[k] ^ s) & m;
            }
            if (flags & STDL_BLIT_MARK) {
                dm[g] |= (uint16_t)em;
            } else if (!(flags & STDL_BLIT_UNDER)) {
                dm[g] &= (uint16_t)~em;
            }
        }
    }
    dst->opaque_state = 0;
    resp_touch(dst, dx, dy, dx + w - 1, dy + h - 1);
    return 0;
}
