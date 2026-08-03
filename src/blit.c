/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_Blit: surface copying.
 *
 * Terms: a "group" is 16 pixels = 4 consecutive plane words (8
 * bytes). "Phase" is x & 15. Same-phase blits walk words with edge
 * masks; different phases go through the shift chain, which reads a
 * 32-bit window per group (and may read one group beyond either end
 * of the source rectangle - surfaces carry guard bytes for this).
 *
 * Masks follow the format contract: bit set = destination preserved.
 */

#include <string.h>
#include "stdl_internal.h"

/* --- same-phase copy ------------------------------------------- */

/* one group: dst = (dst & ~vis) | (src & vis), mask upkeep. np is
 * the plane budget and is a compile-time constant in every
 * instantiation, so the guarded plane writes vanish. */
STDL_PLANE_INLINE void copy_group(const uint16_t *sg, uint16_t *dg,
                                  uint16_t vis, uint16_t *dm, int g,
                                  const int np)
{
    if (vis == 0xFFFFu) {
        dg[0] = sg[0];
        if (np > 1) dg[1] = sg[1];
        if (np > 2) dg[2] = sg[2];
        if (np > 3) dg[3] = sg[3];
    } else if (vis != 0) {
        uint16_t keep = (uint16_t)~vis;
        dg[0] = (uint16_t)((dg[0] & keep) | (sg[0] & vis));
        if (np > 1) dg[1] = (uint16_t)((dg[1] & keep) | (sg[1] & vis));
        if (np > 2) dg[2] = (uint16_t)((dg[2] & keep) | (sg[2] & vis));
        if (np > 3) dg[3] = (uint16_t)((dg[3] & keep) | (sg[3] & vis));
    }
    if (dm != NULL && vis != 0) {
        dm[g] &= (uint16_t)~vis;   /* blitted pixels become opaque */
    }
}

/*
 * Edge groups are peeled out of the row loop so the middle groups
 * run without per-group edge tests; the unmasked middle collapses
 * to a straight byte copy when the whole group is in budget, and to
 * a strided plane copy when it is not.
 */
STDL_PLANE_INLINE void blit_rows_aligned(const uint8_t *srow,
                              uint8_t *drow,
                              const uint8_t *smrow, uint8_t *dmrow,
                              int sstride, int dstride,
                              int smstride, int dmstride,
                              int ng, int h,
                              uint16_t lm, uint16_t rm, int masked,
                              const int np)
{
    int y, g;

    if (ng == 1) {
        lm &= rm;
    }
    for (y = 0; y < h; y++) {
        const uint16_t *sg = (const uint16_t *)srow;
        uint16_t *dg = (uint16_t *)drow;
        const uint16_t *sm = (const uint16_t *)smrow;
        uint16_t *dm = (uint16_t *)dmrow;

        copy_group(sg, dg,
                   masked ? (uint16_t)(lm & ~sm[0]) : lm, dm, 0, np);
        if (ng > 1) {
            if (masked) {
                for (g = 1; g < ng - 1; g++) {
                    copy_group(sg + g * 4, dg + g * 4,
                               (uint16_t)~sm[g], dm, g, np);
                }
            } else if (ng > 2) {
                if (np == 4) {
                    memcpy(dg + 4, sg + 4, (size_t)(ng - 2) * 8);
                } else {
                    const uint16_t *sp = sg + 4;
                    uint16_t *dp = dg + 4;
                    for (g = 1; g < ng - 1; g++) {
                        copy_group(sp, dp, 0xFFFFu, NULL, 0, np);
                        sp += 4;
                        dp += 4;
                    }
                }
                if (dm != NULL) {
                    memset(dm + 1, 0, (size_t)(ng - 2) * 2);
                }
            }
            copy_group(sg + (ng - 1) * 4, dg + (ng - 1) * 4,
                       masked ? (uint16_t)(rm & ~sm[ng - 1]) : rm,
                       dm, ng - 1, np);
        }

        srow += sstride;
        drow += dstride;
        smrow += smstride;
        if (dmrow != NULL) {
            dmrow += dmstride;
        }
    }
}

/* --- shift chain ------------------------------------------------ */
/*
 * off = sx - dx is the source-pixel index that lands on destination
 * pixel 0. For destination group g the needed source window starts
 * at pixel dgbase + 16*g + off, i.e. source word sw with an in-word
 * offset r in 1..15 (r == 0 is the aligned case above). Each output
 * word is (s[sw] << r) | (s[sw+1] >> (16 - r)) per plane, via
 * walking pointers; the single mask word is carried across groups.
 * (Carrying the four plane words was tried and measured slower:
 * gcc 4.6 spills the array to the stack, trading RAM reads for
 * RAM reads plus copies.)
 */

STDL_PLANE_INLINE void blit_rows_shift(const uint8_t *srow,
                            uint8_t *drow,
                            const uint8_t *smrow, uint8_t *dmrow,
                            int sstride, int dstride,
                            int smstride, int dmstride,
                            int ng, int h,
                            uint16_t lm, uint16_t rm, int masked,
                            int sw0, int r, const int np)
{
    int y, g, p;
    int rr = 16 - r;

    if (ng == 1) {
        lm &= rm;
    }
    for (y = 0; y < h; y++) {
        const uint16_t *sp = (const uint16_t *)(srow + sw0 * 8);
        const uint16_t *mp = masked
            ? (const uint16_t *)(smrow + sw0 * 2) : NULL;
        uint16_t *dg = (uint16_t *)drow;
        uint16_t *dm = (uint16_t *)dmrow;
        uint16_t bm = 0;

        if (masked) {
            bm = *mp++;
        }

        for (g = 0; g < ng; g++) {
            uint16_t vis = 0xFFFFu;

            if (g == 0) vis &= lm;
            if (g == ng - 1) vis &= rm;
            if (masked) {
                uint16_t am = bm;
                bm = *mp++;
                vis &= (uint16_t)~((uint16_t)((am << r) | (bm >> rr)));
            }
            if (vis != 0) {
                uint16_t keep = (uint16_t)~vis;
                const uint16_t *s2 = sp + 4;
                for (p = 0; p < np; p++) {
                    uint16_t v =
                        (uint16_t)((sp[p] << r) | (s2[p] >> rr));
                    dg[p] = (uint16_t)((dg[p] & keep) | (v & vis));
                }
                if (dm != NULL) {
                    dm[g] &= (uint16_t)~vis;
                }
            }
            sp += 4;
            dg += 4;
        }
        srow += sstride;
        drow += dstride;
        smrow += smstride;
        if (dmrow != NULL) {
            dmrow += dmstride;
        }
    }
}

/* --------------------------------------------------------------- */

int STDL_BlitSurface(STDL_Surface *src, const STDL_Rect *srcrect,
                     STDL_Surface *dst, STDL_Rect *dstrect)
{
    int sx, sy, w, h, dx, dy;
    int cx1, cy1, cx2, cy2;
    int masked, np = stdl_planes;

    if (src == NULL || dst == NULL || src->pixels == NULL
        || dst->pixels == NULL) {
        STDL_SetError("null surface in blit");
        return -1;
    }

    if (srcrect != NULL) {
        sx = srcrect->x;
        sy = srcrect->y;
        w = srcrect->w;
        h = srcrect->h;
    } else {
        sx = 0;
        sy = 0;
        w = src->w;
        h = src->h;
    }
    if (dstrect != NULL) {
        dx = dstrect->x;
        dy = dstrect->y;
    } else {
        dx = 0;
        dy = 0;
    }

    /* translate logical coordinates into storage space */
    sx -= src->org_x;
    sy -= src->org_y;
    dx -= dst->org_x;
    dy -= dst->org_y;

    /* clip source rectangle against the source surface */
    if (sx < 0) { w += sx; dx -= sx; sx = 0; }
    if (sy < 0) { h += sy; dy -= sy; sy = 0; }
    if (sx + w > src->w) w = src->w - sx;
    if (sy + h > src->h) h = src->h - sy;

    /* clip destination against the destination clip rect */
    cx1 = dst->clip.x;
    cy1 = dst->clip.y;
    cx2 = dst->clip.x + dst->clip.w;
    cy2 = dst->clip.y + dst->clip.h;
    if (dx < cx1) { w -= cx1 - dx; sx += cx1 - dx; dx = cx1; }
    if (dy < cy1) { h -= cy1 - dy; sy += cy1 - dy; dy = cy1; }
    if (dx + w > cx2) w = cx2 - dx;
    if (dy + h > cy2) h = cy2 - dy;

    if (w <= 0 || h <= 0) {
        if (dstrect != NULL) {
            dstrect->w = 0;
            dstrect->h = 0;
        }
        return 0;
    }
    /* SDL 1.2 semantics: store the final clipped rectangle back,
     * in the destination's logical coordinates */
    if (dstrect != NULL) {
        dstrect->x = (int16_t)(dx + dst->org_x);
        dstrect->y = (int16_t)(dy + dst->org_y);
        dstrect->w = (uint16_t)w;
        dstrect->h = (uint16_t)h;
    }

    masked = (src->flags & STDL_SRCKEY) && src->mask != NULL;

    {
        int sphase = sx & 15;
        int dphase = dx & 15;
        int sg0 = sx >> 4;
        int dg0 = dx >> 4;
        int ng = ((dphase + w + 15) >> 4);
        uint16_t lm = (uint16_t)(0xFFFFu >> dphase);
        uint16_t rm =
            (uint16_t)(0xFFFFu << (15 - ((dphase + w - 1) & 15)));
        const uint8_t *srow =
            src->pixels + (uint32_t)sy * src->stride;
        uint8_t *drow =
            dst->pixels + (uint32_t)dy * dst->stride + dg0 * 8;
        const uint8_t *smrow = masked
            ? src->mask + (uint32_t)sy * src->maskstride
            : NULL;
        uint8_t *dmrow = (dst->mask != NULL)
            ? dst->mask + (uint32_t)dy * dst->maskstride + dg0 * 2
            : NULL;
        int smstride = masked ? src->maskstride : 0;
        int dmstride = (dst->mask != NULL) ? dst->maskstride : 0;

        if (dst->mask != NULL) {
            dst->opaque_state = 0;
        }

        if (sphase == dphase
            && stdl_blitter_active()
            && ng * h >= (masked ? STDL_BLIT_MASKED_MIN_CELLS
                                 : STDL_BLIT_COPY_MIN_CELLS)) {
            /*
             * BLiTTER path, one plane rectangle per pass. Masked
             * blits use XOR-AND-XOR: d ^= s; d &= mask; d ^= s
             * computes (d & mask) | (s & ~mask) exactly, without
             * needing zeroed source pixels under the mask.
             */
            uintptr_t sbase = (uintptr_t)(srow + sg0 * 8);
            uintptr_t dbase = (uintptr_t)drow;
            uintptr_t smbase =
                masked ? (uintptr_t)(smrow + sg0 * 2) : 0;
            int16_t s_yinc = (int16_t)(src->stride - (ng - 1) * 8);
            int16_t d_yinc = (int16_t)(dst->stride - (ng - 1) * 8);
            int16_t sm_yinc = masked
                ? (int16_t)(src->maskstride - (ng - 1) * 2) : 0;
            int p;

            for (p = 0; p < np; p++) {
                uintptr_t sp = sbase + (uintptr_t)(p * 2);
                uintptr_t dp = dbase + (uintptr_t)(p * 2);

                if (!masked) {
                    stdl_blitter_go(sp, 8, s_yinc, dp, 8, d_yinc,
                                    lm, rm, (uint16_t)ng, (uint16_t)h,
                                    STDL_BLIT_HOP_SRC,
                                    STDL_BLIT_OP_SRC);
                } else {
                    stdl_blitter_go(sp, 8, s_yinc, dp, 8, d_yinc,
                                    lm, rm, (uint16_t)ng, (uint16_t)h,
                                    STDL_BLIT_HOP_SRC,
                                    STDL_BLIT_OP_XOR);
                    stdl_blitter_go(smbase, 2, sm_yinc, dp, 8, d_yinc,
                                    lm, rm, (uint16_t)ng, (uint16_t)h,
                                    STDL_BLIT_HOP_SRC,
                                    STDL_BLIT_OP_AND);
                    stdl_blitter_go(sp, 8, s_yinc, dp, 8, d_yinc,
                                    lm, rm, (uint16_t)ng, (uint16_t)h,
                                    STDL_BLIT_HOP_SRC,
                                    STDL_BLIT_OP_XOR);
                }
            }
            if (dmrow != NULL) {
                int16_t dm_yinc =
                    (int16_t)(dst->maskstride - (ng - 1) * 2);
                if (masked) {
                    /* dstmask &= srcmask inside the span */
                    stdl_blitter_go(smbase, 2, sm_yinc,
                                    (uintptr_t)dmrow, 2, dm_yinc,
                                    lm, rm, (uint16_t)ng, (uint16_t)h,
                                    STDL_BLIT_HOP_SRC,
                                    STDL_BLIT_OP_AND);
                } else {
                    stdl_blitter_go(0, 0, 0,
                                    (uintptr_t)dmrow, 2, dm_yinc,
                                    lm, rm, (uint16_t)ng, (uint16_t)h,
                                    STDL_BLIT_HOP_ONES,
                                    STDL_BLIT_OP_ZERO);
                }
            }
            return 0;
        }

        if (sphase == dphase) {
            /* fully aligned, unmasked, whole groups: straight rows,
             * but only while every plane of the group is in budget */
            if (!masked && lm == 0xFFFFu && rm == 0xFFFFu
                && np == 4) {
                const uint8_t *sp = srow + sg0 * 8;
                uint8_t *dp = drow;
                int bytes = ng * 8;
                int y;
                for (y = 0; y < h; y++) {
                    memcpy(dp, sp, (size_t)bytes);
                    if (dmrow != NULL) {
                        memset(dmrow, 0, (size_t)(ng * 2));
                        dmrow += dmstride;
                    }
                    sp += src->stride;
                    dp += dst->stride;
                }
            } else {
#define BLIT_ALIGNED(np) \
                blit_rows_aligned(srow + sg0 * 8, drow, \
                                  masked ? smrow + sg0 * 2 : NULL, \
                                  dmrow, \
                                  src->stride, dst->stride, \
                                  smstride, dmstride, \
                                  ng, h, lm, rm, masked, (np))
                STDL_PLANE_DISPATCH(np, BLIT_ALIGNED);
#undef BLIT_ALIGNED
            }
        } else {
            /*
             * Shift chain. Source pixel landing on destination
             * group base: sp0 = (dg0*16) + off with off = sx - dx.
             * Split into word index and residue r in 1..15.
             */
            int off = sx - dx;
            int sp0 = dg0 * 16 + off;
            int sw0, r;

            /* floor division / positive modulo for negative sp0 */
            sw0 = sp0 >> 4;
            r = sp0 & 15;
            if (sp0 < 0) {
                sw0 = -((-sp0 + 15) >> 4);
                r = sp0 - sw0 * 16;
            }
#define BLIT_SHIFT(np) \
            blit_rows_shift(srow, drow, smrow, dmrow, \
                            src->stride, dst->stride, \
                            smstride, dmstride, \
                            ng, h, lm, rm, masked, sw0, r, (np))
            STDL_PLANE_DISPATCH(np, BLIT_SHIFT);
#undef BLIT_SHIFT
        }
    }
    return 0;
}
