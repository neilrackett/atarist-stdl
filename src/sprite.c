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
    uint16_t *data = malloc(vsize << 5);
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

    v0 = malloc(framesize * (uint32_t)nframes * 2);
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
 * shift chain (the documented slow path).
 */
/*
 * Sprite row loop, instantiated once per plane budget: with np a
 * compile-time constant the per-group plane merges unroll and the
 * out-of-budget words are never fetched from the sprite either.
 */
STDL_PLANE_INLINE void blit_sprite_rows(const uint16_t *srow,
                              uint8_t *drow, uint32_t rowwords,
                              int dstride, int rows,
                              int g0, int g1, int sprgroups,
                              uint16_t cover0, uint16_t cover1,
                              int runtime_shift, int r, const int np)
{
    int yy, g, p;

    for (yy = 0; yy < rows; yy++) {
        uint16_t *dgrp = (uint16_t *)drow;
        const uint16_t *src = srow;

        for (g = g0; g < g1; g++) {
            uint16_t mask, w[4];
            uint16_t cover = (g == g0) ? cover0
                           : (g == g1 - 1) ? cover1 : 0xFFFFu;

            /* w[p] above the plane budget is never read: every use
             * below is guarded by the same np test that fills it,
             * so the three zero stores this loop used to make were
             * dead - three stores a group, a row, a sprite. */
            if (!runtime_shift) {
                const uint16_t *sg = src + g * SPR_WORDS;
                mask = sg[0];
                w[0] = sg[1];
                if (np > 1) w[1] = sg[2];
                if (np > 2) w[2] = sg[3];
                if (np > 3) w[3] = sg[4];
            } else {
                const uint16_t *a =
                    (g > 0) ? src + (g - 1) * SPR_WORDS : NULL;
                const uint16_t *b =
                    (g < sprgroups) ? src + g * SPR_WORDS : NULL;
                uint16_t am = a ? a[0] : 0xFFFFu;
                uint16_t bm = b ? b[0] : 0xFFFFu;
                mask = (uint16_t)((am << (16 - r)) | (bm >> r));
                for (p = 0; p < np; p++) {
                    uint16_t aw = a ? a[1 + p] : 0;
                    uint16_t bw = b ? b[1 + p] : 0;
                    w[p] = (uint16_t)((aw << (16 - r)) | (bw >> r));
                }
            }

            if (cover != 0xFFFFu) {
                mask |= (uint16_t)~cover;
                w[0] &= cover;
                if (np > 1) w[1] &= cover;
                if (np > 2) w[2] &= cover;
                if (np > 3) w[3] &= cover;
            }
            if (mask != 0xFFFFu) {
                dgrp[0] = (uint16_t)((dgrp[0] & mask) | w[0]);
                if (np > 1)
                    dgrp[1] = (uint16_t)((dgrp[1] & mask) | w[1]);
                if (np > 2)
                    dgrp[2] = (uint16_t)((dgrp[2] & mask) | w[2]);
                if (np > 3)
                    dgrp[3] = (uint16_t)((dgrp[3] & mask) | w[3]);
            }
            dgrp += 4;
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

    if (spr->nvariants == 16) {
        fdata = spr->data
              + stdl_mul32x16(stdl_mul32x16(spr->framesize, spr->nframes),
                              (uint16_t)phase)
              + stdl_mul32x16(spr->framesize, (uint16_t)frame);
        ng = spr->groups;
    } else if (phase == 0) {
        fdata = spr->data + stdl_mul32x16(spr->framesize, (uint16_t)frame);
        ng = spr->groups;
    } else {
        fdata = spr->data + stdl_mul32x16(spr->framesize, (uint16_t)frame);
        ng = spr->groups + 1;       /* output covers one extra group */
        runtime_shift = 1;
        r = phase;
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
#define SPRITE_ROWS(np) \
        blit_sprite_rows(srow, drow, rowwords, dst->stride, \
                         row1 - row0, g0, g1, spr->groups, \
                         cover0, cover1, runtime_shift, r, (np))
        STDL_PLANE_DISPATCH(np, SPRITE_ROWS);
#undef SPRITE_ROWS
    }
}
