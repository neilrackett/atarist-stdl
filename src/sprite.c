/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Sprites, tilesets and bitmap fonts.
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

/* Fill one frame of variant 0 (unshifted) from surface pixels. */
static void build_frame(const STDL_Surface *s, int fx, int frame_w,
                        int groups, uint16_t *out)
{
    int y, g, p;

    for (y = 0; y < s->h; y++) {
        const uint8_t *row = s->pixels + (uint32_t)y * s->stride;
        const uint16_t *mrow = (s->mask != NULL)
            ? (const uint16_t *)(s->mask + (uint32_t)y * s->maskstride)
            : NULL;

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
    }
}

/* Expand variant 0 into 16 pre-shifted variants (variant v shifted
 * right v pixels, one pad group wider). Returns new data block. */
static uint16_t *preshift_expand(const uint16_t *v0, int groups,
                                 int rows_total)
{
    int pg = groups + 1;                 /* padded groups per row */
    uint32_t rowsz = (uint32_t)pg * SPR_WORDS;
    uint32_t vsize = rowsz * (uint32_t)rows_total;
    uint16_t *data = malloc(vsize * 16 * 2);
    int v, y, g, p;

    if (data == NULL) {
        return NULL;
    }
    for (v = 0; v < 16; v++) {
        uint16_t *dst = data + (uint32_t)v * vsize;
        for (y = 0; y < rows_total; y++) {
            const uint16_t *src = v0 + (uint32_t)y * groups * SPR_WORDS;
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

            w[1] = 0;
            w[2] = 0;
            w[3] = 0;
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

void STDL_BlitSprite(STDL_Sprite *spr, int frame, STDL_Surface *dst,
                     int x, int y)
{
    int phase, ng, row0, row1, g0, g1, gx0, cy1, cy2, cx1, cx2;
    int runtime_shift, r, np;
    const uint16_t *fdata;
    uint32_t rowwords;

    if (spr == NULL || dst == NULL || frame < 0
        || frame >= spr->nframes) {
        return;
    }
    phase = x & 15;
    runtime_shift = 0;
    r = 0;

    if (spr->nvariants == 16) {
        fdata = spr->data
              + (uint32_t)phase * spr->framesize * spr->nframes
              + (uint32_t)frame * spr->framesize;
        ng = spr->groups;
    } else if (phase == 0) {
        fdata = spr->data + (uint32_t)frame * spr->framesize;
        ng = spr->groups;
    } else {
        fdata = spr->data + (uint32_t)frame * spr->framesize;
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
        const uint16_t *srow = fdata + (uint32_t)row0 * rowwords;
        uint8_t *drow = dst->pixels
            + (uint32_t)(y + row0) * dst->stride + (gx0 + g0) * 8;

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

        np = stdl_planes;
#define SPRITE_ROWS(np) \
        blit_sprite_rows(srow, drow, rowwords, dst->stride, \
                         row1 - row0, g0, g1, spr->groups, \
                         cover0, cover1, runtime_shift, r, (np))
        STDL_PLANE_DISPATCH(np, SPRITE_ROWS);
#undef SPRITE_ROWS
    }
}

/* ---------------------------------------------------------------- */
/* tilesets                                                         */

STDL_Tileset *STDL_TilesetFromSurface(const STDL_Surface *s, int tw,
                                      int th)
{
    STDL_Tileset *ts;
    int groups, cols, rows, tx, ty, t, y, g, p;
    int masked;

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

    for (t = 0; t < ts->ntiles; t++) {
        uint16_t *out = ts->data + ts->tilesize * (uint32_t)t;
        tx = (t % cols) * tw;
        ty = (t / cols) * th;
        for (y = 0; y < th; y++) {
            const uint8_t *row =
                s->pixels + (uint32_t)(ty + y) * s->stride;
            const uint16_t *mrow = masked
                ? (const uint16_t *)(s->mask
                    + (uint32_t)(ty + y) * s->maskstride)
                : NULL;
            for (g = 0; g < groups; g++) {
                const uint16_t *grp =
                    (const uint16_t *)(row + ((tx >> 4) + g) * 8);
                uint16_t mask = masked ? mrow[(tx >> 4) + g] : 0;
                if (masked) {
                    *out++ = mask;
                }
                for (p = 0; p < 4; p++) {
                    *out++ = (uint16_t)(grp[p] & ~mask);
                }
            }
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
    int yy, g;

    for (yy = 0; yy < rows; yy++) {
        for (g = g0; g < g1; g++) {
            uint16_t *dgrp = (uint16_t *)(drow + (gx0 + g) * 8);
            const uint16_t *sg = src + g * words;
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
        src += tsgroups * words;
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

    tdata = ts->data + ts->tilesize * (uint32_t)index;
    {
    const uint16_t *src =
        tdata + (uint32_t)row0 * ts->groups * words;
    uint8_t *drow =
        dst->pixels + (uint32_t)(y + row0) * dst->stride;

    np = stdl_planes;
#define TILE_ROWS(np) \
    blit_tile_rows(src, drow, dst->stride, row1 - row0, gx0, g0, g1, \
                   ts->groups, words, ts->masked, (np))
    STDL_PLANE_DISPATCH(np, TILE_ROWS);
#undef TILE_ROWS
    }
}

/* ---------------------------------------------------------------- */
/* fonts                                                            */

void STDL_FreeFont(STDL_Font *font)
{
    if (font != NULL) {
        free(font->bits);
        free(font);
    }
}

/*
 * Draw text with a 1bpp cell font (cw <= 16). Set bits get colour
 * col; clear bits leave the destination untouched.
 *
 * Everything that does not vary per glyph row - the clip masks, the
 * group shift, the plane fill words and the row pointers - is
 * hoisted out of the row loop by hand: gcc 4.6 will not unswitch it,
 * and games that render a status bar a character at a time run this
 * inner loop thousands of times a frame. The plane budget is
 * hoisted the same way - the glyph loop is instantiated once per
 * budget, so a 4-colour game moves half the memory per glyph.
 */
STDL_PLANE_INLINE void draw_text_glyphs(uint8_t *pixels, int stride,
                   const STDL_Font *font, int x, int y,
                   const char *text,
                   uint16_t pw0, uint16_t pw1, uint16_t pw2,
                   uint16_t pw3, int row0, int row1,
                   int cx1, int cx2, const int np)
{
    int i, cw, ch, bpr, wstride;
    uint16_t widthmask, glyphsize;
    const uint8_t *bits0;
    uint8_t *rowbase;

    wstride = stride >> 1;
    cw = font->cw;
    ch = font->ch;
    bpr = font->bytes_per_row;
    widthmask = (uint16_t)(0xFFFFu << (16 - cw));

    /*
     * Everything that does not depend on which glyph this is comes
     * out of the loop. That matters more than it looks: gcc 4.6 turns
     * a 32-bit multiply into a __mulsi3 call (~270 cycles measured on
     * an 8MHz 68000), and the obvious form of this loop makes four of
     * them per character - `(c - first) * bpr * ch` is two on its own.
     * What is left is one mulu.w: glyphsize is at most 16*32/8 and
     * c - first at most 255, so both operands fit 16 bits.
     */
    glyphsize = (uint16_t)(bpr * ch);
    bits0 = font->bits + (uint32_t)row0 * bpr;
    rowbase = pixels + (uint32_t)(y + row0) * stride;

    for (i = 0; text[i] != '\0'; i++, x += cw) {
        uint8_t c = (uint8_t)text[i];
        const uint8_t *glyph;
        uint16_t clipmask;
        int row, shift, gx, cl, cr;
        uint16_t *g1w, *g2w;

        if (c < font->first || c > font->last) {
            continue;
        }
        cl = cx1 - x;
        cr = (x + cw) - cx2;
        if (cl >= cw || cr >= cw) {
            continue;                       /* fully clipped away */
        }
        clipmask = widthmask;
        if (cl > 0) clipmask &= (uint16_t)(0xFFFFu >> cl);
        if (cr > 0) clipmask &= (uint16_t)(0xFFFFu << (16 - cw + cr));
        if (clipmask == 0) {
            continue;
        }

        shift = x & 15;
        gx = x >> 4;
        glyph = bits0 + (uint16_t)(c - font->first) * glyphsize;
        g1w = (uint16_t *)(rowbase + gx * 8);
        g2w = g1w + 4;

        for (row = row0; row < row1;
             row++, glyph += bpr, g1w += wstride, g2w += wstride) {
            uint16_t bits = (uint16_t)(glyph[0] << 8);
            uint16_t hi, lo;

            if (bpr > 1) {
                bits |= glyph[1];
            }
            bits &= clipmask;
            if (bits == 0) {
                continue;
            }

            /* place the 16-bit strip across up to two groups */
            hi = (uint16_t)(bits >> shift);
            lo = (shift != 0) ? (uint16_t)(bits << (16 - shift)) : 0;

            if (hi != 0) {
                stdl_merge_planes(g1w, hi, pw0, pw1, pw2, pw3, np);
            }
            if (lo != 0) {
                stdl_merge_planes(g2w, lo, pw0, pw1, pw2, pw3, np);
            }
        }
    }
}

void STDL_DrawText(STDL_Surface *dst, const STDL_Font *font,
                   int x, int y, const char *text, uint8_t col)
{
    int row0, row1, cx1, cx2, np;
    uint16_t pw0, pw1, pw2, pw3;

    if (dst == NULL || font == NULL || text == NULL
        || font->cw > 16 || font->cw <= 0) {
        return;
    }
    col &= 15;
    pw0 = (col & 1) ? 0xFFFFu : 0;
    pw1 = (col & 2) ? 0xFFFFu : 0;
    pw2 = (col & 4) ? 0xFFFFu : 0;
    pw3 = (col & 8) ? 0xFFFFu : 0;

    /* vertical clip is the same for every glyph on the line */
    row0 = 0;
    row1 = font->ch;
    if (y < dst->clip.y) row0 = dst->clip.y - y;
    if (y + row1 > dst->clip.y + dst->clip.h)
        row1 = dst->clip.y + dst->clip.h - y;
    if (row0 >= row1) {
        return;
    }
    cx1 = dst->clip.x;
    cx2 = dst->clip.x + dst->clip.w;

    np = stdl_planes;
#define TEXT_GLYPHS(np) \
    draw_text_glyphs(dst->pixels, dst->stride, font, x, y, text, \
                     pw0, pw1, pw2, pw3, row0, row1, cx1, cx2, (np))
    STDL_PLANE_DISPATCH(np, TEXT_GLYPHS);
#undef TEXT_GLYPHS
}
