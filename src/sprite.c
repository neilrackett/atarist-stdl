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

    if (s == NULL || frame_w <= 0 || (frame_w & 15) != 0) {
        STDL_SetError("sprite frames must be a multiple of 16 wide");
        return NULL;
    }
    nframes = s->w / frame_w;
    if (nframes < 1) {
        STDL_SetError("surface narrower than one frame");
        return NULL;
    }
    groups = frame_w >> 4;
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
void STDL_BlitSprite(STDL_Sprite *spr, int frame, STDL_Surface *dst,
                     int x, int y)
{
    int phase, ng, row0, row1, g0, g1, gx0, cy1, cy2, cx1, cx2;
    int runtime_shift, r, yy, g, p;
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

        for (yy = row0; yy < row1; yy++) {
            uint16_t *dgrp = (uint16_t *)drow;
            const uint16_t *src = srow;

            for (g = g0; g < g1; g++) {
                uint16_t mask, w[4];
                uint16_t cover = (g == g0) ? cover0
                               : (g == g1 - 1) ? cover1 : 0xFFFFu;

                if (!runtime_shift) {
                    const uint16_t *sg = src + g * SPR_WORDS;
                    mask = sg[0];
                    w[0] = sg[1];
                    w[1] = sg[2];
                    w[2] = sg[3];
                    w[3] = sg[4];
                } else {
                    const uint16_t *a =
                        (g > 0) ? src + (g - 1) * SPR_WORDS : NULL;
                    const uint16_t *b =
                        (g < spr->groups) ? src + g * SPR_WORDS : NULL;
                    uint16_t am = a ? a[0] : 0xFFFFu;
                    uint16_t bm = b ? b[0] : 0xFFFFu;
                    mask = (uint16_t)((am << (16 - r)) | (bm >> r));
                    for (p = 0; p < 4; p++) {
                        uint16_t aw = a ? a[1 + p] : 0;
                        uint16_t bw = b ? b[1 + p] : 0;
                        w[p] = (uint16_t)((aw << (16 - r)) | (bw >> r));
                    }
                }

                if (cover != 0xFFFFu) {
                    mask |= (uint16_t)~cover;
                    w[0] &= cover;
                    w[1] &= cover;
                    w[2] &= cover;
                    w[3] &= cover;
                }
                if (mask != 0xFFFFu) {
                    dgrp[0] = (uint16_t)((dgrp[0] & mask) | w[0]);
                    dgrp[1] = (uint16_t)((dgrp[1] & mask) | w[1]);
                    dgrp[2] = (uint16_t)((dgrp[2] & mask) | w[2]);
                    dgrp[3] = (uint16_t)((dgrp[3] & mask) | w[3]);
                }
                dgrp += 4;
            }
            srow += rowwords;
            drow += dst->stride;
        }
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
void STDL_BlitTile(STDL_Tileset *ts, int index, STDL_Surface *dst,
                   int x, int y)
{
    int row0, row1, g, g0, g1, yy, gx0, words;
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
    for (yy = row0; yy < row1; yy++) {
        for (g = g0; g < g1; g++) {
            uint16_t *dgrp = (uint16_t *)(drow + (gx0 + g) * 8);
            const uint16_t *sg = src + g * words;
            if (ts->masked) {
                uint16_t m = sg[0];
                dgrp[0] = (uint16_t)((dgrp[0] & m) | sg[1]);
                dgrp[1] = (uint16_t)((dgrp[1] & m) | sg[2]);
                dgrp[2] = (uint16_t)((dgrp[2] & m) | sg[3]);
                dgrp[3] = (uint16_t)((dgrp[3] & m) | sg[4]);
            } else {
                dgrp[0] = sg[0];
                dgrp[1] = sg[1];
                dgrp[2] = sg[2];
                dgrp[3] = sg[3];
            }
        }
        src += ts->groups * words;
        drow += dst->stride;
    }
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

/* Draw text with a 1bpp cell font (cw <= 16). Set bits get colour
 * col; clear bits leave the destination untouched. */
void STDL_DrawText(STDL_Surface *dst, const STDL_Font *font,
                   int x, int y, const char *text, uint8_t col)
{
    int i;

    if (dst == NULL || font == NULL || text == NULL
        || font->cw > 16) {
        return;
    }
    col &= 15;
    for (i = 0; text[i] != '\0'; i++, x += font->cw) {
        uint8_t c = (uint8_t)text[i];
        const uint8_t *glyph;
        int row;

        if (c < font->first || c > font->last) {
            continue;
        }
        glyph = font->bits + (uint32_t)(c - font->first)
              * font->bytes_per_row * font->ch;

        for (row = 0; row < font->ch;
             row++, glyph += font->bytes_per_row) {
            int py = y + row;
            uint32_t bits;
            int shift, gx;
            uint32_t win;
            int p;

            if (py < dst->clip.y
                || py >= dst->clip.y + dst->clip.h) {
                continue;
            }
            bits = (uint32_t)glyph[0] << 8;
            if (font->bytes_per_row > 1) {
                bits |= glyph[1];
            }
            bits &= 0xFFFFu << (16 - font->cw);
            if (bits == 0) {
                continue;
            }

            /* clip horizontally */
            {
                int cl = dst->clip.x - x;
                int cr = (x + font->cw) - (dst->clip.x + dst->clip.w);
                if (cl >= font->cw || cr >= font->cw) {
                    continue;
                }
                if (cl > 0) bits &= 0xFFFFu >> cl;
                if (cr > 0) bits &= 0xFFFFu << (16 - font->cw + cr);
                if (bits == 0) {
                    continue;
                }
            }

            /* place the 16-bit strip across up to two groups */
            shift = x & 15;
            gx = x >> 4;
            win = (uint32_t)bits << (16 - shift);  /* 32-bit window */
            if (shift == 0) {
                win = (uint32_t)bits << 16;
            }
            {
                uint8_t *drow = dst->pixels
                    + (uint32_t)py * dst->stride;
                uint16_t hi = (uint16_t)(win >> 16);
                uint16_t lo = (uint16_t)win;
                uint16_t *g1w = (uint16_t *)(drow + gx * 8);
                uint16_t *g2w = (uint16_t *)(drow + (gx + 1) * 8);

                for (p = 0; p < 4; p++) {
                    if (hi != 0 && gx >= 0) {
                        if (col & (1 << p)) g1w[p] |= hi;
                        else                g1w[p] &= (uint16_t)~hi;
                    }
                    if (lo != 0) {
                        if (col & (1 << p)) g2w[p] |= lo;
                        else                g2w[p] &= (uint16_t)~lo;
                    }
                }
            }
        }
    }
}
