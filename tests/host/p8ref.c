/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/*
 * The reference test_responsive.c holds 8-plane surfaces to: the split
 * model from the bake-off between the two (AGENTS.md), kept as the
 * tests' oracle once the library took the other. A surface here is two
 * 4-plane surfaces, its colours' low nibbles and high nibbles, and
 * every call is the library's own planar routine on each, colour & 15
 * on one and colour >> 4 on the other - so it is as right as those
 * routines are, which their own suites check, and shares no code with
 * planes8.c. A colour key's mask is the two halves' masks ANDed (a
 * pixel is the key when both nibbles are), held by both. A sprite is
 * two planar sprites. Flip interleaves the halves' plane words into the
 * display's 16-byte groups: the four low planes, then the four high.
 * It plugs into responsive.c through the same seam (resp_model) as
 * planes8.c.
 */

#include <stdlib.h>
#include <string.h>
#include "responsive.h"

typedef struct {
    p8_surface d;
    STDL_Surface *half[2];
} ref_surface;

typedef struct {
    STDL_Sprite s;
    STDL_Sprite *half[2];
} ref_sprite;

static STDL_Surface *sp_page[2][2];     /* the screen's, page by page */

/* the halves of a surface; the screen's are the page drawn into */
static STDL_Surface *ref_half(const STDL_Surface *s, int i)
{
    if (s == &stdl_rs.screen.s) {
        return sp_page[stdl_rs.back][i];
    }
    return ((const ref_surface *)s)->half[i];
}

#define HALF(s, i) ref_half((s), (i))
#define SHALF(s, i) (((ref_sprite *)(s))->half[i])

/* an 8-plane surface over two halves the caller made */
static STDL_Surface *sp_wrap(STDL_Surface *lo, STDL_Surface *hi)
{
    ref_surface *d;

    if (lo == NULL || hi == NULL) {
        STDL_FreeSurface(lo);
        STDL_FreeSurface(hi);
        return NULL;
    }
    d = calloc(1, sizeof(*d));
    if (d == NULL) {
        STDL_FreeSurface(lo);
        STDL_FreeSurface(hi);
        STDL_SetError("out of memory");
        return NULL;
    }
    p8_init_surface(&d->d.s, lo->w, lo->h);
    d->half[0] = lo;
    d->half[1] = hi;
    /* pixels for the blit's null test; no stride, so the whole-surface
     * copy (blit.c) declines it and the 8-plane blit is reached */
    d->d.s.pixels = lo->pixels;
    return &d->d.s;
}

static STDL_Surface *sp_create(int w, int h)
{
    return sp_wrap(stdl_surface_create(w, h), stdl_surface_create(w, h));
}

static void sp_free_surface(STDL_Surface *s)
{
    STDL_FreeSurface(HALF(s, 0));
    STDL_FreeSurface(HALF(s, 1));
    free(s);
}

/* the surface's view of its halves after they changed */
static void sp_sync(STDL_Surface *s)
{
    const STDL_Surface *lo = HALF(s, 0);

    s->mask = lo->mask;
    s->maskstride = lo->maskstride;
    s->flags = (s->flags & ~STDL_SRCKEY) | (lo->flags & STDL_SRCKEY);
    s->clip = lo->clip;
}

/* one mask for both halves: a pixel is transparent when both are */
static void sp_and_masks(STDL_Surface *s)
{
    STDL_Surface *lo = HALF(s, 0), *hi = HALF(s, 1);
    uint32_t n, i;

    if (lo->mask == NULL || hi->mask == NULL) {
        return;
    }
    n = stdl_row_off(lo->h, lo->maskstride);
    for (i = 0; i < n; i++) {
        lo->mask[i] &= hi->mask[i];
        hi->mask[i] = lo->mask[i];
    }
}

static int sp_set_key(STDL_Surface *s, int enable, uint8_t key)
{
    if (STDL_SetColourKey(HALF(s, 0), enable, (uint8_t)(key & 15)) < 0
        || STDL_SetColourKey(HALF(s, 1), enable, (uint8_t)(key >> 4)) < 0) {
        return -1;
    }
    if (enable) {
        sp_and_masks(s);
        s->colourkey = key;
    }
    sp_sync(s);
    return 0;
}

static void sp_set_clip(STDL_Surface *s, const STDL_Rect *r)
{
    STDL_SetClipRect(HALF(s, 0), r);
    STDL_SetClipRect(HALF(s, 1), r);
    s->clip = HALF(s, 0)->clip;
}

static void sp_fill_rect(STDL_Surface *d, STDL_Rect *r, uint8_t c)
{
    STDL_Rect t;

    if (r == NULL) {
        STDL_FillRect(HALF(d, 0), NULL, (uint8_t)(c & 15));
        STDL_FillRect(HALF(d, 1), NULL, (uint8_t)(c >> 4));
        resp_touch(d, d->clip.x, d->clip.y, d->clip.x + d->clip.w - 1,
                   d->clip.y + d->clip.h - 1);
        return;
    }
    t = *r;
    STDL_FillRect(HALF(d, 0), r, (uint8_t)(c & 15));
    STDL_FillRect(HALF(d, 1), &t, (uint8_t)(c >> 4));
    resp_touch(d, r->x, r->y, r->x + r->w - 1, r->y + r->h - 1);
}

static void sp_hline(STDL_Surface *d, int x1, int x2, int y, uint8_t c)
{
    STDL_HLine(HALF(d, 0), x1, x2, y, (uint8_t)(c & 15));
    STDL_HLine(HALF(d, 1), x1, x2, y, (uint8_t)(c >> 4));
    if (x1 > x2) {
        int t = x1; x1 = x2; x2 = t;
    }
    resp_touch(d, x1, y, x2, y);
}

static void sp_vline(STDL_Surface *d, int x, int y1, int y2, uint8_t c)
{
    STDL_VLine(HALF(d, 0), x, y1, y2, (uint8_t)(c & 15));
    STDL_VLine(HALF(d, 1), x, y1, y2, (uint8_t)(c >> 4));
    if (y1 > y2) {
        int t = y1; y1 = y2; y2 = t;
    }
    resp_touch(d, x, y1, x, y2);
}

static void sp_put_pixel(STDL_Surface *d, int x, int y, uint8_t c)
{
    STDL_PutPixel(HALF(d, 0), x, y, (uint8_t)(c & 15));
    STDL_PutPixel(HALF(d, 1), x, y, (uint8_t)(c >> 4));
    resp_touch(d, x, y, x, y);
}

static uint8_t sp_get_pixel(const STDL_Surface *s, int x, int y)
{
    return (uint8_t)(STDL_GetPixel(HALF(s, 0), x, y)
                     | (STDL_GetPixel(HALF(s, 1), x, y) << 4));
}

static void sp_points(STDL_Surface *d, const STDL_Point *p, int n,
                      uint8_t c)
{
    STDL_Points(HALF(d, 0), p, n, (uint8_t)(c & 15));
    STDL_Points(HALF(d, 1), p, n, (uint8_t)(c >> 4));
    resp_touch_points(d, p, n);
}

/* the colours split into nibbles, in a buffer kept for the next call */
static uint8_t *sp_cols;
static int sp_ncols;

static void sp_points_c(STDL_Surface *d, const STDL_Point *p,
                        const uint8_t *c, int n)
{
    int i;

    if (n > sp_ncols) {
        uint8_t *b = realloc(sp_cols, (size_t)n * 2);

        if (b == NULL) {
            return;
        }
        sp_cols = b;
        sp_ncols = n;
    }
    for (i = 0; i < n; i++) {
        sp_cols[i] = (uint8_t)(c[i] & 15);
        sp_cols[sp_ncols + i] = (uint8_t)(c[i] >> 4);
    }
    STDL_PointsC(HALF(d, 0), p, sp_cols, n);
    STDL_PointsC(HALF(d, 1), p, sp_cols + sp_ncols, n);
    resp_touch_points(d, p, n);
}

static int sp_blit(STDL_Surface *src, const STDL_Rect *sr,
                   STDL_Surface *dst, STDL_Rect *dr, unsigned flags)
{
    STDL_Rect a, b;

    if (!STDL_IS_RESP(src) || !STDL_IS_RESP(dst) || flags != 0) {
        STDL_SetError("blit between an 8-plane and a 4-plane surface");
        return -1;
    }
    /* the first blit clips the rectangle, the second gets it as given */
    if (dr != NULL) {
        a = *dr;
    } else {
        a.x = 0;
        a.y = 0;
    }
    b = a;
    if (STDL_BlitSurface(HALF(src, 0), sr, HALF(dst, 0), &a) < 0
        || STDL_BlitSurface(HALF(src, 1), sr, HALF(dst, 1), &b) < 0) {
        return -1;
    }
    if (dr != NULL) {
        *dr = a;
    }
    if (a.w != 0 && a.h != 0) {
        resp_touch(dst, a.x, a.y, a.x + a.w - 1, a.y + a.h - 1);
    }
    return 0;
}

static STDL_Sprite *sp_sprite_from(const STDL_Surface *s, int fw,
                                   uint32_t flags)
{
    ref_sprite *d = calloc(1, sizeof(*d));

    if (d == NULL) {
        STDL_SetError("out of memory");
        return NULL;
    }
    d->half[0] = STDL_SpriteFromSurface(HALF(s, 0), fw, flags);
    d->half[1] = d->half[0] != NULL
                 ? STDL_SpriteFromSurface(HALF(s, 1), fw, flags) : NULL;
    if (d->half[1] == NULL) {
        STDL_FreeSprite(d->half[0]);
        free(d);
        return NULL;
    }
    d->s = *d->half[0];
    d->s.planes = 8;
    d->s.data = NULL;
    return &d->s;
}

static void sp_blit_sprite(STDL_Sprite *spr, int frame, STDL_Surface *d,
                           int x, int y)
{
    STDL_BlitSprite(SHALF(spr, 0), frame, HALF(d, 0), x, y);
    STDL_BlitSprite(SHALF(spr, 1), frame, HALF(d, 1), x, y);
    resp_touch(d, x, y, x + spr->w - 1, y + spr->h - 1);
}

static void sp_free_sprite(STDL_Sprite *spr)
{
    STDL_FreeSprite(SHALF(spr, 0));
    STDL_FreeSprite(SHALF(spr, 1));
    free(spr);
}

static void sp_draw_text(STDL_Surface *d, const STDL_Font *f, int x, int y,
                         const char *t, uint8_t c)
{
    STDL_DrawText(HALF(d, 0), f, x, y, t, (uint8_t)(c & 15));
    STDL_DrawText(HALF(d, 1), f, x, y, t, (uint8_t)(c >> 4));
    resp_touch(d, x, y,
               x + (int)stdl_row_off((int)strlen(t), (uint16_t)f->cw) - 1,
               y + f->ch - 1);
}

static STDL_Surface *sp_from_1bpp(const uint8_t *bits, int w, int h,
                                  uint8_t fg, uint8_t bg)
{
    return sp_wrap(stdl_surface_from_1bpp(bits, w, h, (uint8_t)(fg & 15),
                                          (uint8_t)(bg & 15)),
                   stdl_surface_from_1bpp(bits, w, h, (uint8_t)(fg >> 4),
                                          (uint8_t)(bg >> 4)));
}

static STDL_Surface *sp_from_indexed8(const uint8_t *bytes, int w, int h,
                                      int stride, int key)
{
    STDL_Surface *s, *half;
    const uint8_t *row;
    uint8_t *buf, *nlo, *nhi;
    uint32_t n;
    int x, y, i;

    if (bytes == NULL || w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        STDL_SetError("no indexed pixel data");
        return NULL;
    }
    if (stride <= 0) {
        stride = w;
    }
    n = stdl_row_off(h, (uint16_t)w);
    buf = malloc(n * 2);
    if (buf == NULL) {
        STDL_SetError("out of memory");
        return NULL;
    }
    nlo = buf;
    nhi = buf + n;
    for (y = 0, row = bytes; y < h; y++, row += stride) {
        for (x = 0; x < w; x++) {
            *nlo++ = (uint8_t)(row[x] & 15);
            *nhi++ = (uint8_t)(row[x] >> 4);
        }
    }
    s = sp_wrap(stdl_surface_from_indexed8(buf, w, h, w, -1),
                stdl_surface_from_indexed8(buf + n, w, h, w, -1));
    free(buf);
    if (s == NULL || key < 0 || key > 255) {
        return s;
    }
    /* transparent where the byte is the key, and in the padding */
    for (i = 0; i < 2; i++) {
        half = HALF(s, i);
        if (STDL_CreateMask(half, 1) < 0) {
            sp_free_surface(s);
            return NULL;
        }
        for (y = 0, row = bytes; y < h; y++, row += stride) {
            uint16_t *m = (uint16_t *)(half->mask
                                       + stdl_row_off(y, half->maskstride));

            for (x = 0; x < w; x++) {
                if (row[x] != key) {
                    m[x >> 4] &= (uint16_t)~(0x8000u >> (x & 15));
                }
            }
        }
    }
    s->colourkey = (uint8_t)key;
    sp_sync(s);
    return s;
}

/* load-time: pixels whose colour is under 16 take the map's */
static void sp_remap(STDL_Surface *s, const uint8_t map[16])
{
    STDL_Surface *lo = HALF(s, 0), *hi = HALF(s, 1);
    uint8_t *lm = lo->mask, *hm = hi->mask;
    int x, y;

    /* the mask is not the remap's to change */
    lo->mask = NULL;
    hi->mask = NULL;
    for (y = 0; y < s->h; y++) {
        for (x = 0; x < s->w; x++) {
            uint8_t v;

            if (STDL_GetPixel(hi, x, y) != 0) {
                continue;
            }
            v = map[STDL_GetPixel(lo, x, y)];
            STDL_PutPixel(lo, x, y, (uint8_t)(v & 15));
            STDL_PutPixel(hi, x, y, (uint8_t)(v >> 4));
        }
    }
    lo->mask = lm;
    hi->mask = hm;
}

static void sp_touch_spans(STDL_Surface *d, const STDL_Span *sp, int n,
                           int vertical)
{
    int i;

    for (i = 0; i < n; i++) {
        if (sp[i].len > 0) {
            if (vertical) {
                resp_touch(d, sp[i].x, sp[i].y, sp[i].x,
                           sp[i].y + sp[i].len - 1);
            } else {
                resp_touch(d, sp[i].x, sp[i].y, sp[i].x + sp[i].len - 1,
                           sp[i].y);
            }
        }
    }
}

static void sp_hspans(STDL_Surface *d, const STDL_Span *sp, int n, uint8_t c)
{
    STDL_HSpans(HALF(d, 0), sp, n, (uint8_t)(c & 15));
    STDL_HSpans(HALF(d, 1), sp, n, (uint8_t)(c >> 4));
    sp_touch_spans(d, sp, n, 0);
}

static void sp_vspans(STDL_Surface *d, const STDL_Span *sp, int n, uint8_t c)
{
    STDL_VSpans(HALF(d, 0), sp, n, (uint8_t)(c & 15));
    STDL_VSpans(HALF(d, 1), sp, n, (uint8_t)(c >> 4));
    sp_touch_spans(d, sp, n, 1);
}

static void sp_xor_hspans(STDL_Surface *d, const STDL_Span *sp, int n,
                          uint8_t c)
{
    STDL_XorHSpans(HALF(d, 0), sp, n, (uint8_t)(c & 15));
    STDL_XorHSpans(HALF(d, 1), sp, n, (uint8_t)(c >> 4));
    sp_touch_spans(d, sp, n, 0);
}

static void sp_xor_vspans(STDL_Surface *d, const STDL_Span *sp, int n,
                          uint8_t c)
{
    STDL_XorVSpans(HALF(d, 0), sp, n, (uint8_t)(c & 15));
    STDL_XorVSpans(HALF(d, 1), sp, n, (uint8_t)(c >> 4));
    sp_touch_spans(d, sp, n, 1);
}

static void sp_xor_rect(STDL_Surface *d, STDL_Rect *r, uint8_t c)
{
    STDL_Rect t;

    if (r == NULL) {
        STDL_XorRect(HALF(d, 0), NULL, (uint8_t)(c & 15));
        STDL_XorRect(HALF(d, 1), NULL, (uint8_t)(c >> 4));
        resp_touch(d, 0, 0, d->w - 1, d->h - 1);
        return;
    }
    t = *r;
    STDL_XorRect(HALF(d, 0), r, (uint8_t)(c & 15));
    STDL_XorRect(HALF(d, 1), &t, (uint8_t)(c >> 4));
    resp_touch(d, r->x, r->y, r->x + r->w - 1, r->y + r->h - 1);
}

static void sp_xor_hline(STDL_Surface *d, int x1, int x2, int y, uint8_t c)
{
    STDL_XorHLine(HALF(d, 0), x1, x2, y, (uint8_t)(c & 15));
    STDL_XorHLine(HALF(d, 1), x1, x2, y, (uint8_t)(c >> 4));
    resp_touch(d, x1 < x2 ? x1 : x2, y, x1 < x2 ? x2 : x1, y);
}

static void sp_xor_pixel(STDL_Surface *d, int x, int y, uint8_t c)
{
    STDL_XorPixel(HALF(d, 0), x, y, (uint8_t)(c & 15));
    STDL_XorPixel(HALF(d, 1), x, y, (uint8_t)(c >> 4));
    resp_touch(d, x, y, x, y);
}

static void sp_draw_char(STDL_Surface *d, const STDL_Font *f, int x, int y,
                         int ch, uint8_t c)
{
    STDL_DrawChar(HALF(d, 0), f, x, y, ch, (uint8_t)(c & 15));
    STDL_DrawChar(HALF(d, 1), f, x, y, ch, (uint8_t)(c >> 4));
    resp_touch(d, x, y, x + f->cw - 1, y + f->ch - 1);
}

/* a polygon's coverage, a pixel at a time through the halves */
static void sp_cover(STDL_Surface *d, const STDL_Surface *scratch, int g0,
                     int ng, int y0, int h, uint8_t c, int op)
{
    int x, y;

    for (y = 0; y < h; y++) {
        for (x = 0; x < ng * 16; x++) {
            const uint16_t *sw = (const uint16_t *)(scratch->pixels
                                  + (size_t)y * scratch->stride);
            const int px = (g0 << 4) + x;
            unsigned v;

            if (!(sw[(x >> 4) * 4] & (0x8000u >> (x & 15)))) {
                continue;
            }
            v = sp_get_pixel(d, px, y0 + y);
            v = op == STDL_DRAW_SET ? c : op == STDL_DRAW_XOR ? (v ^ c)
                                                              : (v | c);
            STDL_PutPixel(HALF(d, 0), px, y0 + y, (uint8_t)(v & 15));
            STDL_PutPixel(HALF(d, 1), px, y0 + y, (uint8_t)(v >> 4));
        }
    }
    resp_touch(d, g0 << 4, y0, ((g0 + ng) << 4) - 1, y0 + h - 1);
}

static void sp_blit_indexed8(STDL_Surface *d, const uint8_t *src, int pitch,
                             int x, int y, int w, int h, const uint8_t *map,
                             unsigned flags)
{
    uint8_t lo[16], hi[16];
    int i;

    for (i = 0; i < 16; i++) {
        lo[i] = (uint8_t)(map[i] & 15);
        hi[i] = (uint8_t)(map[i] >> 4);
    }
    /* the mask is read (UNDER) and kept (MARK) the same on both: the
     * high half's pass must see it as it was */
    {
        STDL_Surface *a = HALF(d, 0), *b = HALF(d, 1);
        uint8_t *keep = NULL;
        uint32_t mb = 0;

        if (a->mask != NULL) {
            mb = stdl_row_off(a->h, a->maskstride);
            keep = malloc(mb);
            memcpy(keep, b->mask, mb);
        }
        STDL_BlitIndexed8(a, src, pitch, x, y, w, h, lo, flags);
        if (keep != NULL) {
            memcpy(b->mask, keep, mb);
            free(keep);
        }
        STDL_BlitIndexed8(b, src, pitch, x, y, w, h, hi, flags);
    }
    resp_touch(d, x, y, x + w - 1, y + h - 1);
}

static STDL_Surface *sp_dup(STDL_Surface *s)
{
    STDL_Surface *d = sp_wrap(STDL_DuplicateSurface(HALF(s, 0)),
                              STDL_DuplicateSurface(HALF(s, 1)));

    /* the mask from the key, over both halves */
    if (d != NULL && (s->flags & STDL_SRCKEY)) {
        sp_set_key(d, 1, s->colourkey);
    }
    return d;
}

static void sp_put_group(STDL_Surface *s, int x, int y,
                         const uint16_t planes[4], uint16_t mask)
{
    static const uint16_t zero[4];

    STDL_PutGroup(HALF(s, 0), x, y, planes, mask);
    STDL_PutGroup(HALF(s, 1), x, y, zero, mask);
    resp_touch(s, x & ~15, y, (x & ~15) + 15, y);
}

static void sp_put_group8(STDL_Surface *s, int x, int y,
                          const uint8_t planes[4], uint8_t transmask)
{
    static const uint8_t zero[4];

    STDL_PutGroup8(HALF(s, 0), x, y, planes, transmask);
    STDL_PutGroup8(HALF(s, 1), x, y, zero, transmask);
    resp_touch(s, x & ~7, y, (x & ~7) + 7, y);
}

static int sp_create_mask(STDL_Surface *s, int transparent)
{
    if (STDL_CreateMask(HALF(s, 0), transparent) < 0
        || STDL_CreateMask(HALF(s, 1), transparent) < 0) {
        return -1;
    }
    sp_sync(s);
    return 0;
}

/* ---------------------------------------------------------------- */
/* the screen                                                       */

static int sp_screen_init(void)
{
    int p, i;

    for (p = 0; p < 2; p++) {
        for (i = 0; i < 2; i++) {
            sp_page[p][i] = stdl_surface_create(stdl_rs.w, stdl_rs.h);
            if (sp_page[p][i] == NULL) {
                return -1;
            }
        }
    }
    return 0;
}

static void sp_screen_page(int p)
{
    stdl_rs.screen.s.pixels = sp_page[p][0]->pixels;
    /* a clip set on the screen holds for both pages */
    sp_page[p][0]->clip = stdl_rs.screen.s.clip;
    sp_page[p][1]->clip = stdl_rs.screen.s.clip;
}

static void sp_screen_free(void)
{
    int p, i;

    for (p = 0; p < 2; p++) {
        for (i = 0; i < 2; i++) {
            STDL_FreeSurface(sp_page[p][i]);
            sp_page[p][i] = NULL;
        }
    }
    free(sp_cols);
    sp_cols = NULL;
    sp_ncols = 0;
}

static void sp_present(int p, const resp_box *b)
{
    const STDL_Surface *lo = sp_page[p][0], *hi = sp_page[p][1];
    const int g0 = b->x0 >> 4, ng = (b->x1 >> 4) - g0 + 1;
    const uint8_t *lrow = lo->pixels + stdl_row_off(b->y0, lo->stride)
                        + g0 * 8;
    const uint8_t *hrow = hi->pixels + stdl_row_off(b->y0, hi->stride)
                        + g0 * 8;
    const int line = stdl_rs.line;
    uint8_t *drow = stdl_rs.page[p] + stdl_rs.left
                  + stdl_row_off(stdl_rs.top + (b->y0 << stdl_rs.dbl),
                                 (uint16_t)line) + g0 * 16;
    int y, g;

    if (b->x0 > b->x1) {
        return;
    }

    for (y = b->y0; y <= b->y1; y++) {
        const uint32_t *l = (const uint32_t *)lrow;
        const uint32_t *h = (const uint32_t *)hrow;
        uint32_t *d = (uint32_t *)drow;

        if (stdl_rs.dbl) {
            uint32_t *e = (uint32_t *)(drow + line);

            for (g = ng; --g >= 0; ) {
                const uint32_t a = *l++, c = *l++, e0 = *h++, e1 = *h++;

                *d++ = a; *d++ = c; *d++ = e0; *d++ = e1;
                *e++ = a; *e++ = c; *e++ = e0; *e++ = e1;
            }
            drow += 2 * line;
        } else {
            for (g = ng; --g >= 0; ) {
                *d++ = *l++; *d++ = *l++;
                *d++ = *h++; *d++ = *h++;
            }
            drow += line;
        }
        lrow += lo->stride;
        hrow += hi->stride;
    }
}

static const stdl_resp_ops_t sp_ops = {
    sp_create, sp_free_surface, sp_set_key, sp_set_clip,
    sp_fill_rect, sp_hline, sp_vline, sp_put_pixel, sp_get_pixel,
    sp_points, sp_points_c, sp_blit,
    sp_sprite_from, sp_blit_sprite, sp_free_sprite, sp_draw_text,
    sp_from_1bpp, sp_from_indexed8, sp_remap, resp_set_colours,
    sp_hspans, sp_vspans, sp_xor_rect, sp_xor_hline, sp_xor_pixel,
    sp_xor_hspans, sp_xor_vspans, sp_draw_char, sp_cover,
    sp_blit_indexed8, sp_dup, sp_create_mask, sp_put_group, sp_put_group8
};

static const resp_model sp_model = {
    &sp_ops, 8, sp_screen_init, sp_screen_page, sp_screen_free, sp_present,
    NULL
};

/* as STDL_UseResponsive, with this standing in for the 8 planes */
int p8ref_use(unsigned what)
{
    return stdl_use_responsive(what,
                               (what & STDL_RESPONSIVE_COLOUR) ? &sp_model
                                                              : NULL,
                               (what & STDL_RESPONSIVE_TALL)
                               ? &stdl_resp_tall : NULL);
}
