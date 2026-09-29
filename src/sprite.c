/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Sprites. Tilesets are in tileset.c and text in text.c, so each is
 * linked only by a program that uses it.
 *
 * Sprite data layout, per 16-pixel group: [mask][p0][p1][p2][p3].
 * Mask bit set = destination preserved; plane bits are zero wherever
 * the mask is set, so the draw loop is dst = (dst & mask) | src.
 * Variants (pre-shifted copies) are stored variant-major:
 *   variant -> frame -> row -> group -> 5 words.
 */

#include <stdlib.h>
#include <string.h>
#include "stdl_internal.h"

#define SPR_WORDS 5     /* words per group: mask + 4 planes */

/* ---------------------------------------------------------------- */
/* building                                                         */

/*
 * Fill one frame of variant 0 (unshifted) from surface pixels.
 *
 * The row bases walk rather than being recomputed: `y * stride` is a
 * 32-bit multiply, which gcc 4.6 turns into a __mulsi3 call (~270
 * cycles on an 8MHz 68000). Two per row, and a pre-shifted sprite
 * builds sixteen variants, so this is the one load-time site where
 * they were worth removing.
 */
static void build_frame(const STDL_Surface *s, int fx, int frame_w,
                        int groups, uint16_t *out)
{
    const uint8_t *row = s->pixels;
    const uint8_t *mbase = s->mask;
    int y, g, p;

    for (y = 0; y < s->h; y++) {
        const uint16_t *mrow = (const uint16_t *)mbase;

        for (g = 0; g < groups; g++) {
            /* source pixels [fx + g*16, fx + g*16 + 15]; the frame
             * offset fx is a multiple of 16 (enforced by caller) */
            int sg = (fx >> 4) + g;
            int in_range = (sg * 16 < s->w);
            const uint16_t *grp =
                (const uint16_t *)(row + sg * 8);
            uint16_t mask;
            uint16_t visible;

            /* pixels beyond the frame width are always masked out */
            int first_px = g * 16;
            uint16_t pad = 0;
            if (first_px + 15 >= frame_w) {
                if (first_px >= frame_w) {
                    pad = 0xFFFFu;
                } else {
                    pad = (uint16_t)(0xFFFFu >> (frame_w - first_px));
                }
            }

            if (!in_range) {
                mask = 0xFFFFu;
            } else {
                mask = (mrow != NULL) ? mrow[sg] : 0;
                mask |= pad;
            }
            visible = (uint16_t)~mask;

            out[0] = mask;
            for (p = 0; p < 4; p++) {
                out[1 + p] = in_range
                    ? (uint16_t)(grp[p] & visible) : 0;
            }
            out += SPR_WORDS;
        }
        row += s->stride;
        if (mbase != NULL) {
            mbase += s->maskstride;
        }
    }
}

/* Expand variant 0 into 16 pre-shifted variants (variant v shifted
 * right v pixels, one pad group wider). Returns new data block. */
static uint16_t *preshift_expand(const uint16_t *v0, int groups,
                                 int rows_total)
{
    int pg = groups + 1;                 /* padded groups per row */
    int srcrow = groups * SPR_WORDS;     /* words per input row      */
    uint32_t rowsz = (uint32_t)pg * SPR_WORDS;
    uint32_t vsize = stdl_row_off(rows_total, (uint16_t)rowsz);
    /* one group of slack past the last variant: see SPR_SLACK */
    uint16_t *data = malloc((vsize << 5) + SPR_SLACK);
    uint16_t *dst = data;
    int v, y, g, p;

    if (data == NULL) {
        return NULL;
    }
    /* dst walks the whole block: one variant is exactly vsize words,
     * which is what the group loop below advances it by per variant */
    for (v = 0; v < 16; v++) {
        const uint16_t *src = v0;
        for (y = 0; y < rows_total; y++) {
            for (g = 0; g < pg; g++) {
                /* source groups g-1 and g feed output group g */
                const uint16_t *a =
                    (g > 0) ? src + (g - 1) * SPR_WORDS : NULL;
                const uint16_t *b =
                    (g < groups) ? src + g * SPR_WORDS : NULL;
                uint16_t am, bm, aw, bw;

                am = a ? a[0] : 0xFFFFu;     /* mask 1-fills edges */
                bm = b ? b[0] : 0xFFFFu;
                if (v == 0) {
                    dst[0] = bm;
                } else {
                    dst[0] = (uint16_t)((am << (16 - v)) | (bm >> v));
                }
                for (p = 1; p <= 4; p++) {
                    aw = a ? a[p] : 0;       /* planes 0-fill */
                    bw = b ? b[p] : 0;
                    if (v == 0) {
                        dst[p] = bw;
                    } else {
                        dst[p] = (uint16_t)
                            ((aw << (16 - v)) | (bw >> v));
                    }
                }
                dst += SPR_WORDS;
            }
            src += srcrow;
        }
    }
    return data;
}

STDL_Sprite *STDL_SpriteFromSurface(const STDL_Surface *s,
                                    int frame_w, uint32_t flags)
{
    STDL_Sprite *spr;
    int groups, nframes, f;
    uint16_t *v0;
    uint32_t framesize;

    /*
     * A frame starts at f * frame_w in the source, and build_frame
     * addresses the source by group, so every frame after the first
     * has to begin on a 16-pixel boundary. That constrains strips of
     * several frames, not the width of a single sprite: a lone frame
     * starts at zero whatever it is wide, and the pixels past
     * frame_w in its last group are masked transparent below.
     */
    if (s == NULL || frame_w <= 0 || frame_w > s->w) {
        STDL_SetError("bad sprite frame width");
        return NULL;
    }
    if ((frame_w & 15) != 0 && frame_w != s->w) {
        STDL_SetError("multi-frame sprites must be a multiple of "
                      "16 wide");
        return NULL;
    }
    nframes = s->w / frame_w;
    groups = (frame_w + 15) >> 4;
    framesize = (uint32_t)groups * SPR_WORDS * s->h;

    v0 = malloc(framesize * (uint32_t)nframes * 2 + SPR_SLACK);
    if (v0 == NULL) {
        STDL_SetError("out of memory");
        return NULL;
    }
    for (f = 0; f < nframes; f++) {
        build_frame(s, f * frame_w, frame_w, groups,
                    v0 + framesize * (uint32_t)f);
    }

    spr = calloc(1, sizeof(STDL_Sprite));
    if (spr == NULL) {
        free(v0);
        STDL_SetError("out of memory");
        return NULL;
    }
    spr->w = (int16_t)frame_w;
    spr->h = s->h;
    spr->nframes = (uint16_t)nframes;
    spr->planes = 4;

    spr->data = v0;
    spr->nvariants = 1;
    spr->groups = (uint16_t)groups;
    spr->framesize = framesize;

    if (flags & STDL_PRESHIFT) {
        STDL_Sprite *ps = stdl_sprite_preshift(spr);
        if (ps == NULL) {
            STDL_FreeSprite(spr);
            return NULL;
        }
        spr = ps;
    }
    return spr;
}

void STDL_FreeSprite(STDL_Sprite *spr)
{
    if (spr != NULL) {
        free(spr->data);
        free(spr);
    }
}

/* Expand an unshifted sprite into a 16-variant one. Consumes the
 * input sprite on success. Used by the bank loader. */
STDL_Sprite *stdl_sprite_preshift(STDL_Sprite *spr)
{
    uint16_t *data;

    if (spr == NULL || spr->nvariants != 1) {
        return spr;
    }
    data = preshift_expand(spr->data, spr->groups,
                           spr->h * spr->nframes);
    if (data == NULL) {
        STDL_SetError("out of memory for pre-shift variants");
        return NULL;
    }
    free(spr->data);
    spr->data = data;
    spr->nvariants = 16;
    spr->groups = (uint16_t)(spr->groups + 1);
    spr->framesize = (uint32_t)spr->groups * SPR_WORDS * spr->h;
    return spr;
}

/* ---------------------------------------------------------------- */
/* drawing                                                          */

/*
 * Draw one frame with its top-left at (x, y), clipped to dst->clip.
 * Pre-shifted sprites select the x & 15 variant and run the aligned
 * loop; unshifted sprites at odd phases go through the runtime
 * shift (the documented slow path).
 *
 * Both row loops are instantiated once per plane budget: with np a
 * compile-time constant the per-group plane merges unroll and the
 * out-of-budget words are never fetched from the sprite either. The
 * choice between them is made once per sprite, outside the loops -
 * gcc 4.6 does not unswitch a loop-invariant test, and the single
 * loop this replaced asked "shifted?" for every group of every row.
 */

/*
 * Merge one group: mask bit set = destination kept, and the plane
 * words are already clear under the mask, so the draw is
 * d = (d & m) | w. A transparent group writes nothing; an opaque
 * one (a mask of zero, the inside of most sprites) is plain stores
 * with no read of the destination - the CPU's version of the
 * BLiTTER never reading a destination word its endmask replaces.
 */
STDL_PLANE_INLINE void spr_merge(uint16_t *d, uint16_t m, uint16_t w0,
                                 uint16_t w1, uint16_t w2, uint16_t w3,
                                 const int np)
{
    if (m == 0) {
        stdl_put_planes(d, w0, w1, w2, w3, np);
    } else if (m != 0xFFFFu) {
        d[0] = (uint16_t)((d[0] & m) | w0);
        if (np > 1) d[1] = (uint16_t)((d[1] & m) | w1);
        if (np > 2) d[2] = (uint16_t)((d[2] & m) | w2);
        if (np > 3) d[3] = (uint16_t)((d[3] & m) | w3);
    }
}

/* one stored group, cut by a clip cover (bits outside it are kept) */
STDL_PLANE_INLINE void spr_group_cover(uint16_t *d, const uint16_t *sg,
                                       uint16_t cover, const int np)
{
    uint16_t m = (uint16_t)(sg[0] | (uint16_t)~cover);

    if (m != 0xFFFFu) {
        spr_merge(d, m, (uint16_t)(sg[1] & cover),
                  np > 1 ? (uint16_t)(sg[2] & cover) : 0,
                  np > 2 ? (uint16_t)(sg[3] & cover) : 0,
                  np > 3 ? (uint16_t)(sg[4] & cover) : 0, np);
    }
}

/*
 * Stored groups straight onto the destination: a pre-shifted variant,
 * or an unshifted sprite at phase 0. The first and last groups carry
 * the clip covers and are peeled out, so the middle runs with no
 * edge tests; unclipped, the covers are all ones and cost a couple
 * of instructions a row. The mask is read first, and a transparent
 * group's plane words are never fetched.
 */
STDL_PLANE_INLINE void spr_rows_aligned(const uint16_t *srow,
                              uint8_t *drow, uint32_t rowwords,
                              int dstride, int rows, int g0, int g1,
                              uint16_t cover0, uint16_t cover1,
                              const int np)
{
    const int mid = g1 - g0 - 2;        /* groups between the edges */
    int yy;

    srow += g0 * SPR_WORDS;
    for (yy = 0; yy < rows; yy++) {
        const uint16_t *sg = srow;
        uint16_t *d = (uint16_t *)drow;

        spr_group_cover(d, sg, cover0, np);
        if (mid >= 0) {
            int n = mid;

            sg += SPR_WORDS;
            d += 4;
            while (n-- > 0) {
                uint16_t m = sg[0];

                if (m != 0xFFFFu) {
                    spr_merge(d, m, sg[1], np > 1 ? sg[2] : 0,
                              np > 2 ? sg[3] : 0, np > 3 ? sg[4] : 0,
                              np);
                }
                sg += SPR_WORDS;
                d += 4;
            }
            spr_group_cover(d, sg, cover1, np);
        }
        srow += rowwords;
        drow += dstride;
    }
}

/* the group beyond either end of an unshifted sprite: transparent */
static const uint16_t spr_edge[SPR_WORDS] = { 0xFFFFu, 0, 0, 0, 0 };

/*
 * One output group of the runtime shift: source groups a (left) and
 * b (right) through a 32-bit window each, one variable shift per
 * word - the form blit.c's shift chain uses. The two-shift form
 * this replaced, (a << (16 - r)) | (b >> r), paid the shift's fixed
 * cost twice per word, ten times per four-plane group.
 */
STDL_PLANE_INLINE void spr_group_shift(uint16_t *d, const uint16_t *a,
                                       const uint16_t *b, int r,
                                       uint16_t cover, const int np)
{
    uint16_t m = (uint16_t)((((uint32_t)a[0] << 16) | b[0]) >> r);

    m |= (uint16_t)~cover;
    if (m != 0xFFFFu) {
        spr_merge(d, m,
            (uint16_t)(((((uint32_t)a[1] << 16) | b[1]) >> r) & cover),
            np > 1 ? (uint16_t)(((((uint32_t)a[2] << 16) | b[2]) >> r)
                                & cover) : 0,
            np > 2 ? (uint16_t)(((((uint32_t)a[3] << 16) | b[3]) >> r)
                                & cover) : 0,
            np > 3 ? (uint16_t)(((((uint32_t)a[4] << 16) | b[4]) >> r)
                                & cover) : 0,
            np);
    }
}

/*
 * An unshifted sprite at phase r: output group g takes source groups
 * g-1 and g, so the output is one group wider than the sprite. Only
 * the first and last output groups can reach past the sprite's ends
 * (they take spr_edge there) or carry a clip cover, so they are
 * peeled out; every group between them has a real source on both
 * sides, walked with a pointer.
 */
STDL_PLANE_INLINE void spr_rows_shift(const uint16_t *srow,
                              uint8_t *drow, uint32_t rowwords,
                              int dstride, int rows, int g0, int g1,
                              int sprgroups, uint16_t cover0,
                              uint16_t cover1, int r, const int np)
{
    const int mid = g1 - g0 - 2;
    const int first_a = g0 - 1, last_b = g1 - 1;
    int yy;

    for (yy = 0; yy < rows; yy++) {
        uint16_t *d = (uint16_t *)drow;
        const uint16_t *a = first_a >= 0
            ? srow + first_a * SPR_WORDS : spr_edge;
        const uint16_t *b = g0 < sprgroups
            ? srow + g0 * SPR_WORDS : spr_edge;

        spr_group_shift(d, a, b, r, cover0, np);
        if (mid >= 0) {
            const uint16_t *sg = srow + g0 * SPR_WORDS;
            int n = mid;

            d += 4;
            while (n-- > 0) {
                spr_group_shift(d, sg, sg + SPR_WORDS, r, 0xFFFFu, np);
                sg += SPR_WORDS;
                d += 4;
            }
            spr_group_shift(d, sg,
                            last_b < sprgroups ? sg + SPR_WORDS
                                               : spr_edge,
                            r, cover1, np);
        }
        srow += rowwords;
        drow += dstride;
    }
}

#ifdef STDL_BLIT_STATS
/* see stdl_types.h; a normal build has none of these */
unsigned long stdl_spr_calls;       /* entries                    */
unsigned long stdl_spr_rows;        /* rows drawn, any path       */
unsigned long stdl_spr_shift;       /* rows through runtime shift */
#endif

void STDL_BlitSprite(STDL_Sprite *spr, int frame, STDL_Surface *dst,
                     int x, int y)
{
    int phase, ng, row0, row1, g0, g1, gx0, cy1, cy2, cx1, cx2;
    int runtime_shift, r, np;
    const uint16_t *fdata;
    uint32_t rowwords;

#ifdef STDL_BLIT_STATS
    stdl_spr_calls++;
#endif
    if (spr == NULL || dst == NULL || frame < 0
        || frame >= spr->nframes) {
        return;
    }
    phase = x & 15;
    runtime_shift = 0;
    r = 0;

    /*
     * The frame's offset: variant v's frame f is frame v * nframes + f
     * of one long run, so one product of the frame size does it. Both
     * factors fit sixteen bits for any sprite a game draws often, and
     * then it is a single mulu.w; the 32-bit form is kept for sheets
     * larger than that.
     */
    {
        uint32_t idx = (uint32_t)frame;

        ng = spr->groups;
        if (spr->nvariants == 16) {
            idx += stdl_row_off(phase, spr->nframes);
        } else if (phase != 0) {
            ng = spr->groups + 1;   /* output covers one extra group */
            runtime_shift = 1;
            r = phase;
        }
        fdata = spr->data
              + ((spr->framesize | idx) <= 0xFFFFu
                 ? stdl_row_off((int)idx, (uint16_t)spr->framesize)
                 : stdl_mul32x16(spr->framesize, (uint16_t)idx));
    }
    rowwords = (uint32_t)spr->groups * SPR_WORDS;

    /* clip */
    cx1 = dst->clip.x;
    cy1 = dst->clip.y;
    cx2 = dst->clip.x + dst->clip.w;
    cy2 = dst->clip.y + dst->clip.h;

    row0 = 0;
    row1 = spr->h;
    if (y < cy1) row0 = cy1 - y;
    if (y + row1 > cy2) row1 = cy2 - y;
    if (row0 >= row1) {
        return;
    }

    /* output group range [g0, g1) relative to sprite group 0 at
     * pixel xbase = x - phase */
    {
        int xbase = x - phase;
        g0 = 0;
        g1 = ng;
        while (g0 < g1 && xbase + g0 * 16 + 15 < cx1) g0++;
        while (g1 > g0 && xbase + (g1 - 1) * 16 >= cx2) g1--;
        if (g0 >= g1) {
            return;
        }
        gx0 = (xbase >> 4);
    }

    /* clip coverage is per-group and row-invariant: only the edge
     * groups can be partial, so precompute their masks once */
    {
        int xb0 = (x - phase) + g0 * 16;
        int xb1 = (x - phase) + (g1 - 1) * 16;
        uint16_t cover0 = 0xFFFFu, cover1 = 0xFFFFu;
        const uint16_t *srow = fdata + stdl_row_off(row0, (uint16_t)rowwords);
        uint8_t *drow = dst->pixels
            + stdl_row_off(y + row0, dst->stride) + (gx0 + g0) * 8;

        if (xb0 < cx1) {
            cover0 &= (uint16_t)(0xFFFFu >> (cx1 - xb0));
        }
        if (xb1 + 15 >= cx2) {
            cover1 &= (uint16_t)(0xFFFFu << (xb1 + 16 - cx2));
        }
        if (g0 == g1 - 1) {
            cover0 &= cover1;
            cover1 = cover0;
        }

#ifdef STDL_BLIT_STATS
        stdl_spr_rows += (unsigned long)(row1 - row0);
        if (runtime_shift) {
            stdl_spr_shift += (unsigned long)(row1 - row0);
        }
#endif
        np = stdl_planes;
        if (!runtime_shift) {
#define SPRITE_ALIGNED(np) \
            spr_rows_aligned(srow, drow, rowwords, dst->stride, \
                             row1 - row0, g0, g1, cover0, cover1, (np))
            STDL_PLANE_DISPATCH(np, SPRITE_ALIGNED);
#undef SPRITE_ALIGNED
        } else {
#define SPRITE_SHIFT(np) \
            spr_rows_shift(srow, drow, rowwords, dst->stride, \
                           row1 - row0, g0, g1, spr->groups, \
                           cover0, cover1, r, (np))
            STDL_PLANE_DISPATCH(np, SPRITE_SHIFT);
#undef SPRITE_SHIFT
        }
    }
}
