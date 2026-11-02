/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * 8-plane surfaces (STDL_RESPONSIVE_COLOUR): eight interleaved planes,
 * the display's own layout - 16 pixels a group, a word a plane, 16
 * bytes - so the screen is the display page itself: on a Falcon there
 * is nothing to present, on a TT each row is copied to the line under
 * it. It beat a surface of two 4-plane halves drawn by the planar
 * routines twice (AGENTS.md, which has the numbers); tests/host/p8ref.c
 * keeps that model as the reference these routines are tested against.
 * A group is four longs, each two planes' words, and a colour is four
 * longs of 0 and 0xFFFF halves (p8_pat), so every write is a long merge
 * under a mask in both halves.
 *
 * A mask (a colour key) is a word a group as a planar surface's is,
 * bit set where the destination shows through; drawing keeps it as the
 * planar routines do (fills, lines, pixels, points and blits clear the
 * bits they draw; text and sprites leave it). STDL_TRANSPARENT keeps
 * its planar meaning on a masked surface - a SET in it sets the bits
 * over colour 0, a hole, and a key of it keeps the mask - so a
 * program's 4-plane composition draws the same on 8 planes, for the
 * one colour of 256 that a masked surface cannot draw. A sprite group
 * is five longs: the transparent pixels in both halves, then the four
 * plane pairs, clear under them.
 */

#include <stdlib.h>
#include <string.h>
#include "responsive.h"

#define P8(s) ((p8_surface *)(s))
#define SPR_LONGS 5

/* plane 2k is a pair's first word, 2k+1 its second */
static const uint32_t p8_pair[4] = {
    STDL_PACK2(0, 0), STDL_PACK2(0xFFFFu, 0),
    STDL_PACK2(0, 0xFFFFu), STDL_PACK2(0xFFFFu, 0xFFFFu)
};

static __inline__ void p8_pat(unsigned c, uint32_t pat[4])
{
    pat[0] = p8_pair[c & 3];
    pat[1] = p8_pair[(c >> 2) & 3];
    pat[2] = p8_pair[(c >> 4) & 3];
    pat[3] = p8_pair[(c >> 6) & 3];
}

/* the colour in pat into group g where m (both halves) is set */
static __inline__ void p8_merge(uint32_t *g, uint32_t m,
                                const uint32_t pat[4])
{
    g[0] ^= (g[0] ^ pat[0]) & m;
    g[1] ^= (g[1] ^ pat[1]) & m;
    g[2] ^= (g[2] ^ pat[2]) & m;
    g[3] ^= (g[3] ^ pat[3]) & m;
}

static __inline__ uint32_t p8_m32(unsigned m16)
{
    return (uint32_t)m16 | ((uint32_t)m16 << 16);
}

static __inline__ uint32_t *p8_group(const STDL_Surface *s, int x, int y)
{
    return (uint32_t *)(s->pixels + stdl_row_off(y, s->stride)
                        + ((x >> 4) << 4));
}

static __inline__ void p8_mclear(STDL_Surface *s, int y, int g,
                                 unsigned m16)
{
    if (s->mask != NULL) {
        ((uint16_t *)(s->mask + stdl_row_off(y, s->maskstride)))[g]
            &= (uint16_t)~m16;
    }
}

/* a SET in STDL_TRANSPARENT on a masked surface: a hole, as the planar
 * fills punch (draw.c). The colour first: one compare for the rest */
static __inline__ int p8_hole(const STDL_Surface *s, unsigned c, int op)
{
    return c == STDL_TRANSPARENT && op == STDL_DRAW_SET && s->mask != NULL;
}

static unsigned p8_get(const STDL_Surface *s, int x, int y)
{
    const uint16_t *g = (const uint16_t *)p8_group(s, x, y);
    const unsigned bit = 0x8000u >> (x & 15);
    unsigned c = 0, p;

    for (p = 0; p < 8; p++) {
        if (g[p] & bit) {
            c |= 1u << p;
        }
    }
    return c;
}

static void p8_set(STDL_Surface *s, int x, int y, unsigned c)
{
    uint32_t pat[4];

    p8_pat(c, pat);
    p8_merge(p8_group(s, x, y), p8_m32(0x8000u >> (x & 15)), pat);
}

/* ---------------------------------------------------------------- */
/* surfaces                                                         */

static STDL_Surface *p8_create(int w, int h)
{
    p8_surface *d;
    uint16_t stride;

    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        STDL_SetError("bad surface size");
        return NULL;
    }
    stride = (uint16_t)(((w + 15) >> 4) << 4);
    d = calloc(1, sizeof(*d));
    if (d != NULL) {
        /* laundered, so the rounding below survives (stdl_launder) */
        d->block = stdl_launder(calloc(1, stdl_row_off(h, stride) + 3));
    }
    if (d == NULL || d->block == NULL) {
        free(d);
        STDL_SetError("out of memory");
        return NULL;
    }
    p8_init_surface(&d->s, w, h);
    d->s.pixels = (uint8_t *)(((uintptr_t)d->block + 3) & ~(uintptr_t)3);
    d->s.stride = stride;
    return &d->s;
}

static void p8_free_surface(STDL_Surface *s)
{
    free(P8(s)->block);
    free(P8(s)->mblock);
    free(s);
}

static int p8_mask_alloc(STDL_Surface *s, int fill)
{
    if (s->mask == NULL) {
        uint16_t ms = (uint16_t)(s->stride >> 3);

        P8(s)->mblock = malloc(stdl_row_off(s->h, ms));
        if (P8(s)->mblock == NULL) {
            STDL_SetError("out of memory for mask");
            return -1;
        }
        s->mask = P8(s)->mblock;
        s->maskstride = ms;
    }
    memset(s->mask, fill, stdl_row_off(s->h, s->maskstride));
    s->flags |= STDL_SRCKEY;
    s->opaque_state = 0;
    return 0;
}

static int p8_set_key(STDL_Surface *s, int enable, uint8_t key)
{
    int y, g, p;
    const int groups = s->stride >> 4;

    if (!enable) {
        free(P8(s)->mblock);
        P8(s)->mblock = NULL;
        s->mask = NULL;
        s->flags &= ~STDL_SRCKEY;
        return 0;
    }
    /* as STDL_SetColourKey: the mask the surface has (holes, PutGroup,
     * a decoder's), or an opaque one, and no scan */
    if (key == STDL_TRANSPARENT) {
        if (s->mask == NULL && p8_mask_alloc(s, 0) < 0) {
            return -1;
        }
        s->colourkey = STDL_TRANSPARENT;
        s->flags |= STDL_SRCKEY;
        return 0;
    }
    if (p8_mask_alloc(s, 0) < 0) {
        return -1;
    }
    s->colourkey = key;
    for (y = 0; y < s->h; y++) {
        const uint16_t *grp =
            (const uint16_t *)(s->pixels + stdl_row_off(y, s->stride));
        uint16_t *mrow =
            (uint16_t *)(s->mask + stdl_row_off(y, s->maskstride));

        for (g = 0; g < groups; g++, grp += 8) {
            uint16_t m = 0xFFFFu;

            for (p = 0; p < 8; p++) {
                m &= (key & (1u << p)) ? grp[p] : (uint16_t)~grp[p];
            }
            mrow[g] = m;
        }
    }
    return 0;
}

/* ---------------------------------------------------------------- */
/* fills, lines and points                                          */

#define P8_SET STDL_DRAW_SET
#define P8_XOR STDL_DRAW_XOR

/* the colour in pat into group g where m is set, by op: CLEAR clears
 * the planes the colour has */
static __inline__ void p8_op(uint32_t *g, uint32_t m, const uint32_t pat[4],
                             int op)
{
    if (op == P8_SET) {
        p8_merge(g, m, pat);
    } else if (op == P8_XOR) {
        g[0] ^= pat[0] & m; g[1] ^= pat[1] & m;
        g[2] ^= pat[2] & m; g[3] ^= pat[3] & m;
    } else if (op == STDL_DRAW_OR) {
        g[0] |= pat[0] & m; g[1] |= pat[1] & m;
        g[2] |= pat[2] & m; g[3] |= pat[3] & m;
    } else {
        g[0] &= ~(pat[0] & m); g[1] &= ~(pat[1] & m);
        g[2] &= ~(pat[2] & m); g[3] &= ~(pat[3] & m);
    }
}

/* the mask's bits under what was drawn cleared, as the planar fills
 * do: pixels drawn are opaque; set for a hole. Inline, and the clear
 * written as it was before holes, so a blit's constant 0 leaves its
 * code as it was: a called helper changed the registers of the whole
 * blit, and a TT's restores took 3% longer */
static __inline__ void p8_mask_row(uint16_t *m, int g0, int g1,
                                   unsigned lm, unsigned rm, int hole)
{
    int g;

    m += g0;
    if (hole) {
        if (g0 == g1) {
            *m |= (uint16_t)(lm & rm);
            return;
        }
        *m++ |= (uint16_t)lm;
        for (g = g1 - g0 - 1; --g >= 0; ) {
            *m++ = 0xFFFFu;
        }
        *m |= (uint16_t)rm;
        return;
    }
    if (g0 == g1) {
        *m &= (uint16_t)~(lm & rm);
        return;
    }
    *m++ &= (uint16_t)~lm;
    for (g = g1 - g0 - 1; --g >= 0; ) {
        *m++ = 0;
    }
    *m &= (uint16_t)~rm;
}

/* rows y1..y2, pixels x1..x2, already clipped */
static void p8_rows(STDL_Surface *s, int x1, int x2, int y1, int y2,
                    unsigned c, int op)
{
    const int g0 = x1 >> 4, g1 = x2 >> 4;
    const int hole = p8_hole(s, c, op);
    unsigned lm = 0xFFFFu >> (x1 & 15);
    unsigned rm = (0xFFFFu << (15 - (x2 & 15))) & 0xFFFFu;
    uint32_t pat[4], ml, mr;
    uint8_t *row = s->pixels + stdl_row_off(y1, s->stride) + (g0 << 4);
    int y, g;

    if (g0 == g1) {
        lm &= rm;
        rm = lm;
    }
    ml = p8_m32(lm);
    mr = p8_m32(rm);
    /* a hole is colour 0: 16 is planes 4-5's pair alone. Choosing the
     * colour before p8_pat cost every call 16 bytes of set-up */
    p8_pat(c, pat);
    if (hole) {
        pat[2] = 0;
    }
    for (y = y1; y <= y2; y++, row += s->stride) {
        uint32_t *d = (uint32_t *)row;

        p8_op(d, ml, pat, op);
        if (g1 > g0) {
            if (op == P8_SET) {
                for (g = g1 - g0 - 1, d += 4; --g >= 0; d += 4) {
                    d[0] = pat[0];
                    d[1] = pat[1];
                    d[2] = pat[2];
                    d[3] = pat[3];
                }
            } else {
                for (g = g1 - g0 - 1, d += 4; --g >= 0; d += 4) {
                    p8_op(d, 0xFFFFFFFFUL, pat, op);
                }
            }
            p8_op(d, mr, pat, op);
        }
        if (s->mask != NULL) {
            p8_mask_row((uint16_t *)(s->mask
                                     + stdl_row_off(y, s->maskstride)),
                        g0, g1, lm, rm, hole);
            s->opaque_state = 0;
        }
    }
    resp_touch(s, x1, y1, x2, y2);
}

/* r clipped, and set to what was drawn, as the planar fills do; an XOR
 * of colour 0 draws nothing */
static void p8_rect(STDL_Surface *d, STDL_Rect *r, unsigned c, int op)
{
    int x1, y1, x2, y2;

    if (r == NULL) {
        x1 = d->clip.x;
        y1 = d->clip.y;
        x2 = d->clip.x + d->clip.w - 1;
        y2 = d->clip.y + d->clip.h - 1;
    } else {
        x1 = r->x - d->org_x;
        y1 = r->y - d->org_y;
        x2 = x1 + r->w - 1;
        y2 = y1 + r->h - 1;
        if (x1 < d->clip.x) x1 = d->clip.x;
        if (y1 < d->clip.y) y1 = d->clip.y;
        if (x2 > d->clip.x + d->clip.w - 1)
            x2 = d->clip.x + d->clip.w - 1;
        if (y2 > d->clip.y + d->clip.h - 1)
            y2 = d->clip.y + d->clip.h - 1;
        if (x1 > x2 || y1 > y2) {
            r->w = 0;
            r->h = 0;
            return;
        }
        r->x = (int16_t)(x1 + d->org_x);
        r->y = (int16_t)(y1 + d->org_y);
        r->w = (uint16_t)(x2 - x1 + 1);
        r->h = (uint16_t)(y2 - y1 + 1);
    }
    if (x1 <= x2 && y1 <= y2 && (op == P8_SET || c != 0)) {
        p8_rows(d, x1, x2, y1, y2, c, op);
    }
}

static void p8_fill_rect(STDL_Surface *d, STDL_Rect *r, uint8_t c)
{
    p8_rect(d, r, c, P8_SET);
}

static void p8_line(STDL_Surface *d, int x1, int x2, int y, unsigned c,
                    int op)
{
    int t;

    if (x1 > x2) { t = x1; x1 = x2; x2 = t; }
    if (y < d->clip.y || y >= d->clip.y + d->clip.h) {
        return;
    }
    if (x1 < d->clip.x) x1 = d->clip.x;
    if (x2 >= d->clip.x + d->clip.w)
        x2 = d->clip.x + d->clip.w - 1;
    if (x1 <= x2 && (op == P8_SET || c != 0)) {
        p8_rows(d, x1, x2, y, y, c, op);
    }
}

static void p8_hline(STDL_Surface *d, int x1, int x2, int y, uint8_t c)
{
    p8_line(d, x1, x2, y, c, P8_SET);
}

static void p8_vline(STDL_Surface *d, int x, int y1, int y2, uint8_t c)
{
    int t;

    if (y1 > y2) { t = y1; y1 = y2; y2 = t; }
    if (x < d->clip.x || x >= d->clip.x + d->clip.w) {
        return;
    }
    if (y1 < d->clip.y) y1 = d->clip.y;
    if (y2 >= d->clip.y + d->clip.h)
        y2 = d->clip.y + d->clip.h - 1;
    if (y1 > y2) {
        return;
    }
    p8_rows(d, x, x, y1, y2, c, P8_SET);
}

/* spans as the planar calls clip them: a row each (or a column) */
static void p8_spans(STDL_Surface *d, const STDL_Span *sp, int n,
                     unsigned c, int op, int vertical)
{
    const int cx0 = d->clip.x, cy0 = d->clip.y;
    const int cx1 = cx0 + d->clip.w - 1, cy1 = cy0 + d->clip.h - 1;
    int i;

    for (i = 0; i < n; i++, sp++) {
        int a, b;

        if (sp->len <= 0) {
            continue;
        }
        if (vertical) {
            if (sp->x < cx0 || sp->x > cx1) {
                continue;
            }
            a = sp->y < cy0 ? cy0 : sp->y;
            b = sp->y + sp->len - 1;
            if (b > cy1) b = cy1;
            if (a <= b) {
                p8_rows(d, sp->x, sp->x, a, b, c, op);
            }
        } else {
            if (sp->y < cy0 || sp->y > cy1) {
                continue;
            }
            a = sp->x < cx0 ? cx0 : sp->x;
            b = sp->x + sp->len - 1;
            if (b > cx1) b = cx1;
            if (a <= b) {
                p8_rows(d, a, b, sp->y, sp->y, c, op);
            }
        }
    }
}

static void p8_hspans(STDL_Surface *d, const STDL_Span *sp, int n, uint8_t c)
{
    p8_spans(d, sp, n, c, P8_SET, 0);
}

static void p8_vspans(STDL_Surface *d, const STDL_Span *sp, int n, uint8_t c)
{
    p8_spans(d, sp, n, c, P8_SET, 1);
}

static void p8_xor_hspans(STDL_Surface *d, const STDL_Span *sp, int n,
                          uint8_t c)
{
    if (c != 0) {
        p8_spans(d, sp, n, c, P8_XOR, 0);
    }
}

static void p8_xor_vspans(STDL_Surface *d, const STDL_Span *sp, int n,
                          uint8_t c)
{
    if (c != 0) {
        p8_spans(d, sp, n, c, P8_XOR, 1);
    }
}

static void p8_xor_rect(STDL_Surface *d, STDL_Rect *r, uint8_t c)
{
    p8_rect(d, r, c, P8_XOR);
}

static void p8_xor_hline(STDL_Surface *d, int x1, int x2, int y, uint8_t c)
{
    p8_line(d, x1, x2, y, c, P8_XOR);
}

static void p8_xor_pixel(STDL_Surface *d, int x, int y, uint8_t c)
{
    if (x < d->clip.x || x >= d->clip.x + d->clip.w
        || y < d->clip.y || y >= d->clip.y + d->clip.h || c == 0) {
        return;
    }
    p8_rows(d, x, x, y, y, c, P8_XOR);
}

static void p8_put_pixel(STDL_Surface *d, int x, int y, uint8_t c)
{
    if (d == NULL
        || x < d->clip.x || x >= d->clip.x + d->clip.w
        || y < d->clip.y || y >= d->clip.y + d->clip.h) {
        return;
    }
    {
        const int hole = p8_hole(d, c, STDL_DRAW_SET);

        p8_set(d, x, y, hole ? 0 : c);
        if (d->mask != NULL) {
            uint16_t *m = (uint16_t *)(d->mask
                                       + stdl_row_off(y, d->maskstride))
                        + (x >> 4);
            const unsigned bit = 0x8000u >> (x & 15);

            *m = (uint16_t)(hole ? *m | bit : *m & ~bit);
            d->opaque_state = 0;
        }
    }
    resp_touch(d, x, y, x, y);
}

static uint8_t p8_get_pixel(const STDL_Surface *s, int x, int y)
{
    if (x < 0 || x >= s->w || y < 0 || y >= s->h) {
        return 0;
    }
    return (uint8_t)p8_get(s, x, y);
}

/* a pixel's bit in both halves of a pair long */
static const uint32_t p8_bit32[16] = {
    0x80008000UL, 0x40004000UL, 0x20002000UL, 0x10001000UL,
    0x08000800UL, 0x04000400UL, 0x02000200UL, 0x01000100UL,
    0x00800080UL, 0x00400040UL, 0x00200020UL, 0x00100010UL,
    0x00080008UL, 0x00040004UL, 0x00020002UL, 0x00010001UL
};

/*
 * Points: an unsigned compare an axis for the clip, a mulu.w for the
 * row and a long merge a plane pair. A masked surface goes a point at a
 * time through PutPixel's own path, which keeps the mask.
 */
static void p8_points(STDL_Surface *d, const STDL_Point *p, int n,
                      uint8_t c)
{
    const int cx0 = d->clip.x, cy0 = d->clip.y;
    const unsigned cw = d->clip.w, ch = d->clip.h;
    uint8_t *const base = d->pixels;
    const uint16_t stride = d->stride;
    uint32_t pat[4];
    int i;

    if (d->mask != NULL) {
        for (i = 0; i < n; i++) {
            p8_put_pixel(d, p[i].x, p[i].y, c);
        }
        return;
    }
    p8_pat(c, pat);
    for (i = 0; i < n; i++, p++) {
        const int x = p->x, y = p->y;

        if ((unsigned)(x - cx0) < cw && (unsigned)(y - cy0) < ch) {
            p8_merge((uint32_t *)(base + stdl_row_off(y, stride)
                                  + ((x >> 4) << 4)),
                     p8_bit32[x & 15], pat);
        }
    }
    resp_touch_points(d, p - n, n);
}

static void p8_points_c(STDL_Surface *d, const STDL_Point *p,
                        const uint8_t *c, int n)
{
    const int cx0 = d->clip.x, cy0 = d->clip.y;
    const unsigned cw = d->clip.w, ch = d->clip.h;
    uint8_t *const base = d->pixels;
    const uint16_t stride = d->stride;
    uint32_t pat[4] = { 0, 0, 0, 0 };
    unsigned last = 256;
    int i;

    if (d->mask != NULL) {
        for (i = 0; i < n; i++) {
            p8_put_pixel(d, p[i].x, p[i].y, c[i]);
        }
        return;
    }
    for (i = 0; i < n; i++, p++) {
        const int x = p->x, y = p->y;

        if ((unsigned)(x - cx0) < cw && (unsigned)(y - cy0) < ch) {
            if (c[i] != last) {
                last = c[i];
                p8_pat(last, pat);
            }
            p8_merge((uint32_t *)(base + stdl_row_off(y, stride)
                                  + ((x >> 4) << 4)),
                     p8_bit32[x & 15], pat);
        }
    }
    resp_touch_points(d, p - n, n);
}

/* ---------------------------------------------------------------- */
/* blits                                                            */

/* the 16 bits of word wi + sh/16 of a row of `ng` words a stride
 * `step` apart, zero (or `out`) past either end */
static __inline__ unsigned p8_fetch(const uint16_t *w, int step, int ng,
                                    int wi, int sh, unsigned out)
{
    unsigned a = (wi >= 0 && wi < ng) ? w[wi * step] : out;
    unsigned b;

    if (sh == 0) {
        return a;
    }
    b = (wi + 1 >= 0 && wi + 1 < ng) ? w[(wi + 1) * step] : out;
    return ((a << sh) | (b >> (16 - sh))) & 0xFFFFu;
}

/* the same phase and no key: the edges merged, the groups between
 * copied - a restore, every frame */
static void p8_copy_row(const uint8_t *srow, uint8_t *drow,
                        uint16_t *dmrow, int sx, int dx, int w)
{
    const int g0 = dx >> 4, g1 = (dx + w - 1) >> 4;
    const uint32_t *s = (const uint32_t *)(srow + ((sx >> 4) << 4));
    uint32_t *d = (uint32_t *)(drow + (g0 << 4));
    unsigned lm = 0xFFFFu >> (dx & 15);
    unsigned rm = (0xFFFFu << (15 - ((dx + w - 1) & 15))) & 0xFFFFu;
    int g;

    if (g0 == g1) {
        lm &= rm;
    }
    p8_merge(d, p8_m32(lm), s);
    if (g1 > g0) {
        /* the whole groups between, a long at a time: a memcpy call a
         * row made a game's restores, a sprite or two wide, 12% slower
         * on a TT and a Falcon */
        for (g = g1 - g0 - 1, s += 4, d += 4; --g >= 0; s += 4, d += 4) {
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = s[3];
        }
        p8_merge(d, p8_m32(rm), s);
    }
    if (dmrow != NULL) {
        p8_mask_row(dmrow, g0, g1, lm, rm, 0);
    }
}

static void p8_blit_row(const uint8_t *srow, int sng, const uint16_t *smrow,
                        uint8_t *drow, uint16_t *dmrow, int sx, int dx,
                        int w)
{
    const int g0 = dx >> 4, g1 = (dx + w - 1) >> 4;
    const int off = sx - dx, sh = off & 15;
    int g, k;

    if (sh == 0 && smrow == NULL) {
        p8_copy_row(srow, drow, dmrow, sx, dx, w);
        return;
    }

    for (g = g0; g <= g1; g++) {
        const int wi = (g * 16 + off) >> 4;
        unsigned em = 0xFFFFu, om;
        uint32_t s[4], *d;

        if (g == g0) em &= 0xFFFFu >> (dx & 15);
        if (g == g1) em &= 0xFFFFu << (15 - ((dx + w - 1) & 15));
        em &= 0xFFFFu;
        if (sh == 0) {
            const uint32_t *sg = (const uint32_t *)(srow + (wi << 4));

            s[0] = sg[0]; s[1] = sg[1]; s[2] = sg[2]; s[3] = sg[3];
            om = smrow != NULL ? (uint16_t)~smrow[wi] : 0xFFFFu;
        } else {
            const uint16_t *sw = (const uint16_t *)srow;

            for (k = 0; k < 4; k++) {
                s[k] = STDL_PACK2(p8_fetch(sw + 2 * k, 8, sng, wi, sh, 0),
                                  p8_fetch(sw + 2 * k + 1, 8, sng, wi, sh,
                                           0));
            }
            om = smrow != NULL
                 ? (~p8_fetch(smrow, 1, sng, wi, sh, 0xFFFFu)) & 0xFFFFu
                 : 0xFFFFu;
        }
        em &= om;
        if (em == 0) {
            continue;
        }
        d = (uint32_t *)(drow + (g << 4));
        p8_merge(d, p8_m32(em), s);
        if (dmrow != NULL) {
            dmrow[g] &= (uint16_t)~em;
        }
    }
}

/* a 4-plane source's row onto 8 planes: its colours 0-15, the upper
 * planes cleared where it draws */
static void p8_blit_row4(const uint8_t *srow, int sng,
                         const uint16_t *smrow, uint8_t *drow,
                         uint16_t *dmrow, int sx, int dx, int w)
{
    const int g0 = dx >> 4, g1 = (dx + w - 1) >> 4;
    const int off = sx - dx, sh = off & 15;
    const uint16_t *sw = (const uint16_t *)srow;
    int g;

    for (g = g0; g <= g1; g++) {
        const int wi = (g * 16 + off) >> 4;
        unsigned em = 0xFFFFu;
        uint32_t s0, s1, m, *d;

        if (g == g0) em &= 0xFFFFu >> (dx & 15);
        if (g == g1) em &= 0xFFFFu << (15 - ((dx + w - 1) & 15));
        em &= 0xFFFFu;
        if (smrow != NULL) {
            em &= ~p8_fetch(smrow, 1, sng, wi, sh, 0xFFFFu);
        }
        if (em == 0) {
            continue;
        }
        s0 = STDL_PACK2(p8_fetch(sw, 4, sng, wi, sh, 0),
                        p8_fetch(sw + 1, 4, sng, wi, sh, 0));
        s1 = STDL_PACK2(p8_fetch(sw + 2, 4, sng, wi, sh, 0),
                        p8_fetch(sw + 3, 4, sng, wi, sh, 0));
        m = p8_m32(em);
        d = (uint32_t *)(drow + (g << 4));
        d[0] ^= (d[0] ^ s0) & m;
        d[1] ^= (d[1] ^ s1) & m;
        d[2] &= ~m;
        d[3] &= ~m;
        if (dmrow != NULL) {
            dmrow[g] &= (uint16_t)~em;
        }
    }
}

static int p8_blit(STDL_Surface *src, const STDL_Rect *srcrect,
                   STDL_Surface *dst, STDL_Rect *dstrect, unsigned flags)
{
    int sx, sy, w, h, dx, dy, cx1, cy1, cx2, cy2, y, masked;

    if (!STDL_IS_RESP(dst)) {
        STDL_SetError("blit from an 8-plane to a 4-plane surface");
        return -1;
    }
    if (flags != 0) {
        STDL_SetError("blit flags on an 8-plane surface");
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
    sx -= src->org_x;
    sy -= src->org_y;
    dx -= dst->org_x;
    dy -= dst->org_y;
    if (sx < 0) { w += sx; dx -= sx; sx = 0; }
    if (sy < 0) { h += sy; dy -= sy; sy = 0; }
    if (sx + w > src->w) w = src->w - sx;
    if (sy + h > src->h) h = src->h - sy;
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
    if (dstrect != NULL) {
        dstrect->x = (int16_t)(dx + dst->org_x);
        dstrect->y = (int16_t)(dy + dst->org_y);
        dstrect->w = (uint16_t)w;
        dstrect->h = (uint16_t)h;
    }
    masked = (src->flags & STDL_SRCKEY) && src->mask != NULL;
    for (y = 0; y < h; y++) {
        if (!STDL_IS_RESP(src)) {
            p8_blit_row4(src->pixels + stdl_row_off(sy + y, src->stride),
                         src->stride >> 3,
                         masked ? (const uint16_t *)(src->mask
                                  + stdl_row_off(sy + y, src->maskstride))
                                : NULL,
                         dst->pixels + stdl_row_off(dy + y, dst->stride),
                         dst->mask != NULL ? (uint16_t *)(dst->mask
                                  + stdl_row_off(dy + y, dst->maskstride))
                                : NULL,
                         sx, dx, w);
            continue;
        }
        p8_blit_row(src->pixels + stdl_row_off(sy + y, src->stride),
                    src->stride >> 4,
                    masked ? (const uint16_t *)(src->mask
                             + stdl_row_off(sy + y, src->maskstride)) : NULL,
                    dst->pixels + stdl_row_off(dy + y, dst->stride),
                    dst->mask != NULL ? (uint16_t *)(dst->mask
                             + stdl_row_off(dy + y, dst->maskstride)) : NULL,
                    sx, dx, w);
    }
    /* whether or not there is a mask (SurfaceIsOpaque reads it only
     * with one): testing for one kept a value on the stack through the
     * whole blit */
    dst->opaque_state = 0;
    resp_touch(dst, dx, dy, dx + w - 1, dy + h - 1);
    return 0;
}

/* ---------------------------------------------------------------- */
/* sprites                                                          */

static STDL_Sprite *p8_sprite_from(const STDL_Surface *s, int fw,
                                   uint32_t flags)
{
    STDL_Sprite *spr;
    uint32_t *out;
    int nframes, nvar, gw, groups, v, f, y, g, j;

    if (s == NULL || fw <= 0 || fw > s->w) {
        STDL_SetError("bad sprite frame width");
        return NULL;
    }
    nframes = s->w / fw;
    gw = (fw + 15) >> 4;
    nvar = (flags & STDL_PRESHIFT) ? 16 : 1;
    groups = nvar == 16 ? gw + 1 : gw;
    spr = calloc(1, sizeof(*spr));
    if (spr == NULL) {
        STDL_SetError("out of memory");
        return NULL;
    }
    spr->w = (int16_t)fw;
    spr->h = s->h;
    spr->groups = (uint16_t)groups;
    spr->nframes = (uint16_t)nframes;
    spr->nvariants = (uint8_t)nvar;
    spr->planes = 8;
    spr->framesize = stdl_row_off(s->h, (uint16_t)(groups * SPR_LONGS));
    out = malloc(spr->framesize * 4 * (uint32_t)(nframes * nvar));
    if (out == NULL) {
        free(spr);
        STDL_SetError("out of memory");
        return NULL;
    }
    spr->data = (uint16_t *)out;

    /* variant v holds every frame v pixels right; load time, so a
     * pixel at a time */
    for (v = 0; v < nvar; v++) {
        for (f = 0; f < nframes; f++) {
            for (y = 0; y < s->h; y++) {
                const uint16_t *mrow = s->mask != NULL
                    ? (const uint16_t *)(s->mask
                                         + stdl_row_off(y, s->maskstride))
                    : NULL;

                for (g = 0; g < groups; g++, out += SPR_LONGS) {
                    unsigned om = 0;
                    uint32_t pl[4] = { 0, 0, 0, 0 };

                    for (j = 0; j < 16; j++) {
                        const int u = g * 16 + j - v, x = f * fw + u;
                        uint32_t pat[4], m;

                        if (u < 0 || u >= fw
                            || (mrow != NULL
                                && (mrow[x >> 4] & (0x8000u >> (x & 15))))) {
                            continue;
                        }
                        om |= 0x8000u >> j;
                        m = p8_m32(0x8000u >> j);
                        p8_pat(p8_get(s, x, y), pat);
                        pl[0] |= pat[0] & m;
                        pl[1] |= pat[1] & m;
                        pl[2] |= pat[2] & m;
                        pl[3] |= pat[3] & m;
                    }
                    out[0] = ~p8_m32(om);
                    out[1] = pl[0];
                    out[2] = pl[1];
                    out[3] = pl[2];
                    out[4] = pl[3];
                }
            }
        }
    }
    return spr;
}

static void p8_free_sprite(STDL_Sprite *spr)
{
    free(spr->data);
    free(spr);
}

/* a sprite group onto a whole destination group: the planes kept
 * where the sprite is transparent, its own where it is not */
static __inline__ void p8_spr_whole(uint32_t *dg, const uint32_t *g)
{
    const uint32_t nm = g[0];

    dg[0] = (dg[0] & nm) | g[1];
    dg[1] = (dg[1] & nm) | g[2];
    dg[2] = (dg[2] & nm) | g[3];
    dg[3] = (dg[3] & nm) | g[4];
}

/* the same, where the clip lets through only the pixels in m */
static __inline__ void p8_spr_masked(uint32_t *dg, const uint32_t *g,
                                     uint32_t m)
{
    m &= ~g[0];
    dg[0] ^= (dg[0] ^ g[1]) & m;
    dg[1] ^= (dg[1] ^ g[2]) & m;
    dg[2] ^= (dg[2] ^ g[3]) & m;
    dg[3] ^= (dg[3] ^ g[4]) & m;
}

/* group o of a row shifted r right: from stored groups o-1 and o */
static void p8_shifted(const uint32_t *row, int groups, int o, int r,
                       uint32_t g[SPR_LONGS])
{
    const uint32_t *a = o > 0 ? row + (o - 1) * SPR_LONGS : NULL;
    const uint32_t *b = o < groups ? row + o * SPR_LONGS : NULL;
    int k;

    for (k = 0; k < SPR_LONGS; k++) {
        /* the mask's halves are transparency: outside the stored
         * groups everything is */
        uint32_t fa = a != NULL ? a[k] : (k == 0 ? 0xFFFFFFFFUL : 0);
        uint32_t fb = b != NULL ? b[k] : (k == 0 ? 0xFFFFFFFFUL : 0);
        uint32_t hi = (((fa >> 16) << (16 - r)) | ((fb >> 16) >> r))
                      & 0xFFFFu;
        uint32_t lo = (((fa & 0xFFFFu) << (16 - r)) | ((fb & 0xFFFFu) >> r))
                      & 0xFFFFu;

        g[k] = (hi << 16) | lo;
    }
}

/* where frame `frame` at phase `phase` starts in a sprite's data, in
 * the units its framesize counts (words for a 4-plane sprite, longs for
 * an 8-plane one), and the shift still to do, without a variant for
 * the phase */
static __inline__ uint32_t p8_frame_at(const STDL_Sprite *spr, int frame,
                                       int phase, int *shift)
{
    uint32_t idx = (uint32_t)frame;

    *shift = 0;
    if (spr->nvariants == 16) {
        idx += stdl_row_off(phase, spr->nframes);
    } else {
        *shift = phase;
    }
    /* a mulu.w while both fit sixteen bits, as the planar sprites do */
    return (spr->framesize | idx) <= 0xFFFFu
           ? stdl_row_off((int)idx, (uint16_t)spr->framesize)
           : spr->framesize * idx;
}

/*
 * A 4-plane sprite (made before the screen, or from a 4-plane surface)
 * onto 8 planes: its groups are [mask][p0-p3], the mask transparent and
 * the planes clear under it; its colours 0-15, the upper planes cleared
 * where it draws. Not the fast path: a group at a time, clipped each.
 */
static void p8_blit_sprite4(STDL_Sprite *spr, int frame, STDL_Surface *d,
                            int x, int y)
{
    const int phase = x & 15, xbase = x - phase;
    const int cx1 = d->clip.x, cx2 = d->clip.x + d->clip.w;
    const int cy1 = d->clip.y, cy2 = d->clip.y + d->clip.h;
    const uint16_t *fd = spr->data;
    const int groups = spr->groups, rowwords = groups * 5;
    int ng = groups, shift, row0, row1, r, o;

    fd += p8_frame_at(spr, frame, phase, &shift);
    if (shift != 0) {
        ng++;
    }
    row0 = y < cy1 ? cy1 - y : 0;
    row1 = y + spr->h > cy2 ? cy2 - y : spr->h;
    fd += stdl_row_off(row0, (uint16_t)rowwords);
    for (r = row0; r < row1; r++, fd += rowwords) {
        uint32_t *dg = (uint32_t *)(d->pixels
                                    + stdl_row_off(y + r, d->stride))
                     + ((xbase >> 4) << 2);

        for (o = 0; o < ng; o++, dg += 4) {
            const int px = xbase + (o << 4);
            unsigned w[5], cm = 0xFFFFu;
            uint32_t m;
            int k;

            if (px + 15 < cx1 || px >= cx2) {
                continue;
            }
            if (px < cx1) cm &= 0xFFFFu >> (cx1 - px);
            if (px + 16 > cx2) cm &= (0xFFFFu << (px + 16 - cx2)) & 0xFFFFu;
            for (k = 0; k < 5; k++) {
                const int at = (o << 2) + o + k;   /* o * 5, no __mulsi3 */

                if (shift == 0) {
                    w[k] = fd[at];
                } else {
                    /* outside the stored groups everything is clear */
                    unsigned a = o > 0 ? fd[at - 5]
                                       : (k == 0 ? 0xFFFFu : 0);
                    unsigned b = o < groups ? fd[at]
                                            : (k == 0 ? 0xFFFFu : 0);

                    w[k] = ((a << (16 - shift)) | (b >> shift)) & 0xFFFFu;
                }
            }
            cm &= ~w[0];
            if (cm == 0) {
                continue;
            }
            m = p8_m32(cm);
            dg[0] ^= (dg[0] ^ STDL_PACK2(w[1], w[2])) & m;
            dg[1] ^= (dg[1] ^ STDL_PACK2(w[3], w[4])) & m;
            dg[2] &= ~m;
            dg[3] &= ~m;
        }
    }
    resp_touch(d, x, y, x + spr->w - 1, y + spr->h - 1);
}

static void p8_blit_sprite(STDL_Sprite *spr, int frame, STDL_Surface *d,
                           int x, int y)
{
    const int phase = x & 15, xbase = x - phase;
    const int cx1 = d->clip.x, cx2 = d->clip.x + d->clip.w;
    const int cy1 = d->clip.y, cy2 = d->clip.y + d->clip.h;
    const uint32_t *fd = (const uint32_t *)spr->data;
    const int rowlongs = spr->groups * SPR_LONGS;
    int ng = spr->groups, shift, row0, row1, o0, o1, r, o;

    if (!STDL_IS_RESP(d)) {
        STDL_SetError("an 8-plane sprite onto a 4-plane surface");
        return;
    }
    if (!STDL_IS_RESP(spr)) {
        p8_blit_sprite4(spr, frame, d, x, y);
        return;
    }
    fd += p8_frame_at(spr, frame, phase, &shift);
    if (shift != 0) {
        ng++;
    }
    row0 = 0;
    row1 = spr->h;
    if (y < cy1) row0 = cy1 - y;
    if (y + row1 > cy2) row1 = cy2 - y;
    if (row0 >= row1) {
        return;
    }
    o0 = 0;
    o1 = ng;
    while (o0 < o1 && xbase + o0 * 16 + 15 < cx1) o0++;
    while (o1 > o0 && xbase + (o1 - 1) * 16 >= cx2) o1--;
    if (o0 >= o1) {
        return;
    }
    {
        /* the edge groups' clip, the same on every row; the groups
         * between them are whole */
        const int n = o1 - o0, stride = d->stride;
        const int px0 = xbase + (o0 << 4), px1 = xbase + ((o1 - 1) << 4);
        unsigned cm0 = 0xFFFFu, cm1 = 0xFFFFu;
        uint8_t *drow = d->pixels + stdl_row_off(y + row0, (uint16_t)stride)
                      + (((xbase >> 4) + o0) << 4);

        if (px0 < cx1) cm0 &= 0xFFFFu >> (cx1 - px0);
        if (px0 + 16 > cx2) cm0 &= (0xFFFFu << (px0 + 16 - cx2)) & 0xFFFFu;
        if (px1 < cx1) cm1 &= 0xFFFFu >> (cx1 - px1);
        if (px1 + 16 > cx2) cm1 &= (0xFFFFu << (px1 + 16 - cx2)) & 0xFFFFu;
        fd += stdl_row_off(row0, (uint16_t)rowlongs);
        if (shift != 0) {
            for (r = row1 - row0; --r >= 0; fd += rowlongs, drow += stride) {
                uint32_t *dg = (uint32_t *)drow;

                for (o = o0; o < o1; o++, dg += 4) {
                    uint32_t sg[SPR_LONGS];

                    p8_shifted(fd, spr->groups, o, shift, sg);
                    p8_spr_masked(dg, sg, p8_m32(o == o0 ? cm0
                                                 : o == o1 - 1 ? cm1
                                                 : 0xFFFFu));
                }
            }
        } else {
            const uint32_t m0 = p8_m32(cm0), m1 = p8_m32(cm1);

            fd += stdl_row_off(o0, SPR_LONGS);
            for (r = row1 - row0; --r >= 0; fd += rowlongs, drow += stride) {
                const uint32_t *g = fd;
                uint32_t *dg = (uint32_t *)drow;
                int k;

                if (cm0 == 0xFFFFu) {
                    p8_spr_whole(dg, g);
                } else {
                    p8_spr_masked(dg, g, m0);
                }
                if (n > 1) {
                    for (k = n - 2, g += SPR_LONGS, dg += 4; --k >= 0;
                         g += SPR_LONGS, dg += 4) {
                        p8_spr_whole(dg, g);
                    }
                    if (cm1 == 0xFFFFu) {
                        p8_spr_whole(dg, g);
                    } else {
                        p8_spr_masked(dg, g, m1);
                    }
                }
            }
        }
    }
    resp_touch(d, x, y, x + spr->w - 1, y + spr->h - 1);
}

/* ---------------------------------------------------------------- */
/* text and the converters                                          */

/* one glyph's rows row0..row1, clipped to cx1..cx2 across; text and
 * STDL_DrawChar clip as each other */
static void p8_glyph(STDL_Surface *d, const STDL_Font *font, int x, int y,
                     unsigned c, const uint32_t pat[4], int row0, int row1)
{
    const int cw = font->cw, bpr = font->bytes_per_row;
    const int cx1 = d->clip.x, cx2 = d->clip.x + d->clip.w;
    const uint8_t *glyph;
    unsigned clipmask = (0xFFFFu << (16 - cw)) & 0xFFFFu;
    int cl, cr, shift, r;

    if (c < font->first || c > font->last) {
        return;
    }
    cl = cx1 - x;
    cr = (x + cw) - cx2;
    if (cl >= cw || cr >= cw) {
        return;
    }
    if (cl > 0) clipmask &= 0xFFFFu >> cl;
    if (cr > 0) clipmask &= (0xFFFFu << (16 - cw + cr)) & 0xFFFFu;
    if (clipmask == 0) {
        return;
    }
    shift = x & 15;
    glyph = font->bits
          + stdl_row_off((int)(c - font->first), (uint16_t)(bpr * font->ch))
          + stdl_row_off(row0, (uint16_t)bpr);
    for (r = row0; r < row1; r++, glyph += bpr) {
        unsigned bits = ((unsigned)glyph[0] << 8) | (bpr > 1 ? glyph[1] : 0);
        uint32_t *g = (uint32_t *)(d->pixels + stdl_row_off(y + r, d->stride))
                    + ((x >> 4) << 2);
        unsigned hi, lo;

        bits &= clipmask;
        hi = bits >> shift;
        lo = shift ? (bits << (16 - shift)) & 0xFFFFu : 0;
        if (hi != 0) {
            p8_merge(g, p8_m32(hi), pat);
        }
        if (lo != 0) {
            p8_merge(g + 4, p8_m32(lo), pat);
        }
    }
}

/* the rows of a font's cells inside the clip, or 0 */
static int p8_text_rows(const STDL_Surface *d, const STDL_Font *font, int y,
                        int *row0, int *row1)
{
    *row0 = 0;
    *row1 = font->ch;
    if (y < d->clip.y) *row0 = d->clip.y - y;
    if (y + *row1 > d->clip.y + d->clip.h) *row1 = d->clip.y + d->clip.h - y;
    return *row0 < *row1;
}

static void p8_draw_text(STDL_Surface *d, const STDL_Font *font, int x,
                         int y, const char *text, uint8_t col)
{
    const int x0 = x;
    uint32_t pat[4];
    int row0, row1, i;

    if (!p8_text_rows(d, font, y, &row0, &row1)) {
        return;
    }
    p8_pat(col, pat);
    for (i = 0; text[i] != '\0'; i++, x += font->cw) {
        p8_glyph(d, font, x, y, (uint8_t)text[i], pat, row0, row1);
    }
    resp_touch(d, x0, y,
               x0 + (int)stdl_row_off(i, (uint16_t)font->cw) - 1,
               y + font->ch - 1);
}

static void p8_draw_char(STDL_Surface *d, const STDL_Font *font, int x,
                         int y, int ch, uint8_t col)
{
    uint32_t pat[4];
    int row0, row1;

    if (font->cw <= 0 || font->cw > 16 || ch < 0 || ch > 255
        || !p8_text_rows(d, font, y, &row0, &row1)) {
        return;
    }
    p8_pat(col, pat);
    p8_glyph(d, font, x, y, (unsigned)ch, pat, row0, row1);
    resp_touch(d, x, y, x + font->cw - 1, y + font->ch - 1);
}

static STDL_Surface *p8_from_1bpp(const uint8_t *bits, int w, int h,
                                  uint8_t fg, uint8_t bg)
{
    STDL_Surface *s = p8_create(w, h);
    const int bpr = (w + 7) >> 3;
    int x, y;

    if (s == NULL) {
        return NULL;
    }
    for (y = 0; y < h; y++, bits += bpr) {
        for (x = 0; x < w; x++) {
            p8_set(s, x, y, (bits[x >> 3] & (0x80u >> (x & 7))) ? fg : bg);
        }
    }
    return s;
}

static STDL_Surface *p8_from_indexed8(const uint8_t *bytes, int w, int h,
                                      int stride, int key)
{
    STDL_Surface *s;
    int x, y;

    if (bytes == NULL) {
        STDL_SetError("no indexed pixel data");
        return NULL;
    }
    if (stride <= 0) {
        stride = w;
    }
    s = p8_create(w, h);
    if (s == NULL) {
        return NULL;
    }
    if (key >= 0 && key <= 255) {
        if (p8_mask_alloc(s, 0xFF) < 0) {
            p8_free_surface(s);
            return NULL;
        }
        s->colourkey = (uint8_t)key;
    }
    for (y = 0; y < h; y++, bytes += stride) {
        for (x = 0; x < w; x++) {
            p8_set(s, x, y, bytes[x]);
            if (s->mask != NULL && bytes[x] != key) {
                p8_mclear(s, y, x >> 4, 0x8000u >> (x & 15));
            }
        }
    }
    return s;
}

static void p8_remap(STDL_Surface *s, const uint8_t map[16])
{
    int x, y;

    for (y = 0; y < s->h; y++) {
        for (x = 0; x < s->w; x++) {
            const unsigned v = p8_get(s, x, y);

            if (v < 16) {
                p8_set(s, x, y, map[v]);
            }
        }
    }
}

/*
 * A polygon's coverage (p8_fill_polygon, below: plane 0 of a 4-plane
 * scratch whose groups are d's from group g0): the colour by op
 * wherever it is set, rows y0 on, and plane 0 left clear again.
 */
static void p8_cover(STDL_Surface *d, STDL_Surface *scratch, int g0,
                     int ng, int y0, int h, uint8_t c, int op)
{
    const int hole = p8_hole(d, c, op);
    uint32_t pat[4];
    int y, g;

    p8_pat(c, pat);
    if (hole) {
        pat[2] = 0;             /* colour 0, as p8_rows */
    }
    for (y = 0; y < h; y++) {
        uint16_t *sw = (uint16_t *)(scratch->pixels
                                    + stdl_row_off(y, scratch->stride));
        uint32_t *dg = (uint32_t *)(d->pixels
                                    + stdl_row_off(y0 + y, d->stride))
                     + (g0 << 2);
        uint16_t *mw = d->mask != NULL
            ? (uint16_t *)(d->mask + stdl_row_off(y0 + y, d->maskstride)) + g0
            : NULL;

        for (g = 0; g < ng; g++, sw += 4, dg += 4) {
            const unsigned m = sw[0];

            if (m != 0) {
                sw[0] = 0;
                p8_op(dg, p8_m32(m), pat, op);
                if (mw != NULL) {
                    mw[g] = (uint16_t)(hole ? mw[g] | m : mw[g] & ~m);
                }
            }
        }
    }
    d->opaque_state = 0;
    resp_touch(d, g0 << 4, y0, ((g0 + ng) << 4) - 1, y0 + h - 1);
}

/*
 * A polygon: drawn by the planar rasteriser, in colour 1, into a
 * 4-plane scratch over its clipped box (its left edge on a group, so
 * the scratch's groups are the surface's), and the colour put wherever
 * plane 0 is set. So every edge rule is the planar one, for the cost
 * of the scratch. The rasteriser XORs plane 0 alone, each row's span
 * once, into a scratch that is clear, and the cover clears what it
 * read: nothing to clear between polygons. polygon.c hands the
 * rasteriser over (`raster`): named here, it would link 7K into every
 * program that asks for the colour, polygons or not.
 */
static STDL_Surface *p8_poly_scratch;
static STDL_Point *p8_poly_pts;
static int p8_poly_npts;

static void p8_fill_polygon(STDL_Surface *dst, const STDL_Point *pts,
                            int n, uint8_t col, int op, stdl_poly_fn raster)
{
    int x0, y0, x1, y1, bx, w, h, i;
    STDL_Rect clip;

    x0 = x1 = pts[0].x;
    y0 = y1 = pts[0].y;
    for (i = 1; i < n; i++) {
        if (pts[i].x < x0) x0 = pts[i].x;
        if (pts[i].x > x1) x1 = pts[i].x;
        if (pts[i].y < y0) y0 = pts[i].y;
        if (pts[i].y > y1) y1 = pts[i].y;
    }
    if (x0 < dst->clip.x) x0 = dst->clip.x;
    if (y0 < dst->clip.y) y0 = dst->clip.y;
    if (x1 > dst->clip.x + dst->clip.w - 1) x1 = dst->clip.x + dst->clip.w - 1;
    if (y1 > dst->clip.y + dst->clip.h - 1) y1 = dst->clip.y + dst->clip.h - 1;
    if (x0 > x1 || y0 > y1) {
        return;
    }
    bx = x0 & ~15;
    w = x1 - bx + 1;
    h = y1 - y0 + 1;
    if (p8_poly_scratch == NULL || p8_poly_scratch->w < w
        || p8_poly_scratch->h < h) {
        const int sw = p8_poly_scratch != NULL && p8_poly_scratch->w > w
                       ? p8_poly_scratch->w : w;
        const int sh = p8_poly_scratch != NULL && p8_poly_scratch->h > h
                       ? p8_poly_scratch->h : h;

        STDL_FreeSurface(p8_poly_scratch);
        p8_poly_scratch = stdl_surface_create(sw, sh);
        if (p8_poly_scratch == NULL) {
            return;
        }
    }
    if (n > p8_poly_npts) {
        STDL_Point *np = realloc(p8_poly_pts, (size_t)n * sizeof(STDL_Point));

        if (np == NULL) {
            return;
        }
        p8_poly_pts = np;
        p8_poly_npts = n;
    }
    for (i = 0; i < n; i++) {
        p8_poly_pts[i].x = (int16_t)(pts[i].x - bx);
        p8_poly_pts[i].y = (int16_t)(pts[i].y - y0);
    }
    clip.x = (int16_t)(x0 - bx);
    clip.y = 0;
    clip.w = (uint16_t)(x1 - x0 + 1);
    clip.h = (uint16_t)h;
    STDL_SetClipRect(p8_poly_scratch, &clip);
    raster(p8_poly_scratch, p8_poly_pts, n, 1, STDL_DRAW_XOR);
    p8_cover(dst, p8_poly_scratch, bx >> 4, (x1 >> 4) - (bx >> 4) + 1,
             y0, h, col, op);
}

/* chunky through a 16-entry map, as STDL_BlitIndexed8 (blit8.c) */
static void p8_blit_indexed8(STDL_Surface *d, const uint8_t *src, int pitch,
                             int x, int y, int w, int h,
                             const uint8_t *map, unsigned flags)
{
    const int cx1 = d->clip.x, cy1 = d->clip.y;
    const int cx2 = d->clip.x + d->clip.w, cy2 = d->clip.y + d->clip.h;
    int step, rowadv, i, j;
    uint8_t cm[16];

    if (flags & STDL_I8_COLMAJOR) {
        step = (flags & STDL_I8_XFLIP) ? -pitch : pitch;
        rowadv = 1;
    } else {
        step = (flags & STDL_I8_XFLIP) ? -1 : 1;
        rowadv = pitch;
    }
    x -= d->org_x;
    y -= d->org_y;
    if (x < cx1) {
        src += stdl_mul16(cx1 - x, step);
        w -= cx1 - x;
        x = cx1;
    }
    if (y < cy1) {
        src += stdl_mul16(cy1 - y, rowadv);
        h -= cy1 - y;
        y = cy1;
    }
    if (x + w > cx2) w = cx2 - x;
    if (y + h > cy2) h = cy2 - y;
    if (w <= 0 || h <= 0) {
        return;
    }
    if (d->mask == NULL) {
        flags &= ~(STDL_I8_UNDER | STDL_I8_MARK);
    }
    for (i = 0; i < 16; i++) {
        cm[i] = map[i];
    }
    for (j = 0; j < h; j++, src += rowadv) {
        const uint8_t *p = src;
        uint16_t *mrow = d->mask != NULL
            ? (uint16_t *)(d->mask + stdl_row_off(y + j, d->maskstride))
            : NULL;

        for (i = 0; i < w; i++, p += step) {
            const int px = x + i;
            const unsigned bit = 0x8000u >> (px & 15);

            if (*p == 0
                || ((flags & STDL_I8_UNDER) && (mrow[px >> 4] & bit))) {
                continue;
            }
            p8_set(d, px, y + j, cm[*p & 15]);
            if (mrow != NULL) {
                if (flags & STDL_I8_MARK) {
                    mrow[px >> 4] |= (uint16_t)bit;
                } else {
                    mrow[px >> 4] &= (uint16_t)~bit;
                }
            }
        }
    }
    d->opaque_state = 0;
    resp_touch(d, x, y, x + w - 1, y + h - 1);
}

static STDL_Surface *p8_dup(STDL_Surface *s)
{
    STDL_Surface *d = p8_create(s->w, s->h);
    const size_t bytes = (size_t)(((s->w + 15) >> 4) << 4);
    int y;

    if (d == NULL) {
        return NULL;
    }
    for (y = 0; y < s->h; y++) {
        memcpy(d->pixels + stdl_row_off(y, d->stride),
               s->pixels + stdl_row_off(y, s->stride), bytes);
    }
    /* as STDL_DuplicateSurface: the mask itself, not one scanned from
     * the key */
    if (s->mask != NULL) {
        if (p8_mask_alloc(d, 0) < 0) {
            p8_free_surface(d);
            return NULL;
        }
        for (y = 0; y < s->h; y++) {
            memcpy(d->mask + stdl_row_off(y, d->maskstride),
                   s->mask + stdl_row_off(y, s->maskstride), d->maskstride);
        }
        d->flags = (d->flags & ~STDL_SRCKEY) | (s->flags & STDL_SRCKEY);
        d->colourkey = s->colourkey;
    }
    return d;
}

static int p8_create_mask(STDL_Surface *s, int transparent)
{
    return p8_mask_alloc(s, transparent ? 0xFF : 0);
}

/* 4-plane data a group (or half of one) at a time: colours 0-15, the
 * upper planes cleared */
static void p8_put_group(STDL_Surface *s, int x, int y,
                         const uint16_t planes[4], uint16_t mask)
{
    const int g = x >> 4;
    uint16_t *grp;

    /* the surface's width, not its stride: a Falcon's overscan screen
     * has groups beyond the picture's either side */
    if (g < 0 || g >= ((s->w + 15) >> 4)) {
        return;
    }
    grp = (uint16_t *)(s->pixels + stdl_row_off(y, s->stride)) + (g << 3);
    grp[0] = planes[0]; grp[1] = planes[1];
    grp[2] = planes[2]; grp[3] = planes[3];
    grp[4] = grp[5] = grp[6] = grp[7] = 0;
    if (s->mask != NULL) {
        ((uint16_t *)(s->mask + stdl_row_off(y, s->maskstride)))[g] = mask;
        s->opaque_state = 0;
    }
    resp_touch(s, g << 4, y, (g << 4) + 15, y);
}

static void p8_put_group8(STDL_Surface *s, int x, int y,
                          const uint8_t planes[4], uint8_t transmask)
{
    const int g = x >> 4, half = STDL_WORD_BYTE((x >> 3) & 1);
    uint8_t *grp;
    int p;

    if (g >= ((s->w + 15) >> 4)) {
        return;
    }
    grp = s->pixels + stdl_row_off(y, s->stride) + (g << 4) + half;
    for (p = 0; p < 8; p++) {
        grp[p * 2] = p < 4 ? planes[p] : 0;
    }
    if (s->mask != NULL) {
        s->mask[stdl_row_off(y, s->maskstride) + g * 2 + half] = transmask;
        s->opaque_state = 0;
    }
    resp_touch(s, (x >> 3) << 3, y, ((x >> 3) << 3) + 7, y);
}

/* ---------------------------------------------------------------- */
/* the screen: the display page                                     */

static int p8_screen_init(void)
{
    /* a Falcon shows the page drawn into as it is */
    if (stdl_rs.falcon) {
        stdl_rs.track = NULL;
    }
    return 0;
}

static void p8_screen_page(int p)
{
    stdl_rs.screen.s.pixels = stdl_rs.page[p] + stdl_rs.left
        + stdl_row_off(stdl_rs.top, (uint16_t)stdl_rs.line);
    stdl_rs.screen.s.stride = (uint16_t)(stdl_rs.line << stdl_rs.dbl);
}

static void p8_screen_free(void)
{
}

/* a TT: each row of the box onto the line under it; a Falcon shows
 * the page as it is */
static void p8_present(int p, const resp_box *b)
{
    const int g0 = b->x0 >> 4, ng = (b->x1 >> 4) - g0 + 1;
    const int line = stdl_rs.line;
    uint8_t *row;
    int y, g;

    if (!stdl_rs.dbl || b->x0 > b->x1) {
        return;
    }
    stdl_rs.stats.groups += stdl_row_off(b->y1 - b->y0 + 1, (uint16_t)ng);
    row = stdl_rs.page[p] + stdl_row_off(stdl_rs.top + (b->y0 << 1),
                                         (uint16_t)line) + (g0 << 4);
    for (y = b->y0; y <= b->y1; y++, row += 2 * line) {
        const uint32_t *s = (const uint32_t *)row;
        uint32_t *d = (uint32_t *)(row + line);

        for (g = ng; --g >= 0; ) {
            *d++ = *s++; *d++ = *s++; *d++ = *s++; *d++ = *s++;
        }
    }
}

static const stdl_resp_ops_t p8_ops = {
    .create = p8_create,
    .free_surface = p8_free_surface,
    .set_key = p8_set_key,
    .fill_rect = p8_fill_rect,
    .hline = p8_hline,
    .vline = p8_vline,
    .put_pixel = p8_put_pixel,
    .get_pixel = p8_get_pixel,
    .points = p8_points,
    .points_c = p8_points_c,
    .blit = p8_blit,
    .sprite_from = p8_sprite_from,
    .blit_sprite = p8_blit_sprite,
    .free_sprite = p8_free_sprite,
    .draw_text = p8_draw_text,
    .from_1bpp = p8_from_1bpp,
    .from_indexed8 = p8_from_indexed8,
    .remap = p8_remap,
    .set_colours = resp_set_colours,
    .hspans = p8_hspans,
    .vspans = p8_vspans,
    .xor_rect = p8_xor_rect,
    .xor_hline = p8_xor_hline,
    .xor_pixel = p8_xor_pixel,
    .xor_hspans = p8_xor_hspans,
    .xor_vspans = p8_xor_vspans,
    .draw_char = p8_draw_char,
    .fill_polygon = p8_fill_polygon,
    .blit_indexed8 = p8_blit_indexed8,
    .dup = p8_dup,
    .create_mask = p8_create_mask,
    .put_group = p8_put_group,
    .put_group8 = p8_put_group8,
};

const resp_model stdl_resp_colour = {
    .ops = &p8_ops, .planes = 8,
    .screen_init = p8_screen_init, .screen_page = p8_screen_page,
    .screen_free = p8_screen_free, .present = p8_present
};
