/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_Draw: span-based primitives. Every shape decomposes to
 * horizontal spans; a span writes whole plane words with edge masks.
 */

#include <stddef.h>
#include "stdl_internal.h"

/*
 * Core rect fill: rows [y1, y2] over storage-space span [x1, x2],
 * all inclusive and pre-clipped. Edge masks, plane words and the
 * row split are computed once, not per row. Handles the surface
 * mask (opaque fills clear it, transparent fills set it).
 */
static void fill_rows(STDL_Surface *s, int x1, int x2, int y1,
                      int y2, uint8_t col, int transparent)
{
    uint8_t *row = s->pixels + (uint32_t)y1 * s->stride;
    uint8_t *mrow = (s->mask != NULL)
        ? s->mask + (uint32_t)y1 * s->maskstride : NULL;
    int g0 = x1 >> 4, g1 = x2 >> 4, g, p, y;
    int ng = g1 - g0 + 1;
    int rows = y2 - y1 + 1;
    uint16_t lm = (uint16_t)(0xFFFFu >> (x1 & 15));
    uint16_t rm = (uint16_t)(0xFFFFu << (15 - (x2 & 15)));
    uint16_t pw[4];

    if (g0 == g1) {
        lm &= rm;
        rm = lm;
    }
    for (p = 0; p < 4; p++) {
        pw[p] = (col & (1 << p)) ? 0xFFFFu : 0;
    }

    if (ng * rows >= STDL_BLIT_FILL_MIN_CELLS
        && stdl_blitter_active()) {
        uintptr_t base = (uintptr_t)(row + g0 * 8);
        int16_t yinc = (int16_t)(s->stride - (ng - 1) * 8);

        for (p = 0; p < 4; p++) {
            /* HOP all-ones: OP_SRC writes ones, OP_ZERO zeros */
            stdl_blitter_go(0, 0, 0, base + (uintptr_t)(p * 2),
                            8, yinc, lm, rm,
                            (uint16_t)ng, (uint16_t)rows,
                            STDL_BLIT_HOP_ONES,
                            pw[p] ? STDL_BLIT_OP_SRC
                                  : STDL_BLIT_OP_ZERO);
        }
        if (mrow != NULL) {
            stdl_blitter_go(0, 0, 0, (uintptr_t)(mrow + g0 * 2),
                            2, (int16_t)(s->maskstride - (ng - 1) * 2),
                            lm, rm, (uint16_t)ng, (uint16_t)rows,
                            STDL_BLIT_HOP_ONES,
                            transparent ? STDL_BLIT_OP_SRC
                                        : STDL_BLIT_OP_ZERO);
            s->opaque_state = 0;
        }
        return;
    }

    for (y = y1; y <= y2; y++) {
        uint16_t *grp = (uint16_t *)(row + g0 * 8);

        if (lm == 0xFFFFu) {
            for (p = 0; p < 4; p++) grp[p] = pw[p];
        } else {
            for (p = 0; p < 4; p++) {
                grp[p] = (uint16_t)((grp[p] & ~lm) | (pw[p] & lm));
            }
        }
        for (g = g0 + 1; g < g1; g++) {
            grp = (uint16_t *)(row + g * 8);
            grp[0] = pw[0];
            grp[1] = pw[1];
            grp[2] = pw[2];
            grp[3] = pw[3];
        }
        if (g1 != g0) {
            grp = (uint16_t *)(row + g1 * 8);
            if (rm == 0xFFFFu) {
                for (p = 0; p < 4; p++) grp[p] = pw[p];
            } else {
                for (p = 0; p < 4; p++) {
                    grp[p] = (uint16_t)((grp[p] & ~rm) | (pw[p] & rm));
                }
            }
        }

        if (mrow != NULL) {
            uint16_t *mw = (uint16_t *)mrow;
            for (g = g0; g <= g1; g++) {
                uint16_t m = 0xFFFFu;
                if (g == g0) m &= lm;
                if (g == g1) m &= rm;
                if (transparent) {
                    mw[g] |= m;
                } else {
                    mw[g] &= (uint16_t)~m;
                }
            }
            mrow += s->maskstride;
        }
        row += s->stride;
    }
    if (s->mask != NULL) {
        s->opaque_state = 0;
    }
}

/*
 * SDL 1.2 semantics: the rectangle is translated by the surface
 * origin, clipped, and the final rectangle is written back (in
 * logical coordinates). NULL fills the whole clip rect.
 */
void STDL_FillRect(STDL_Surface *dst, STDL_Rect *r, uint8_t col)
{
    int x1, y1, x2, y2;
    int transparent;

    if (dst == NULL) {
        return;
    }
    if (r == NULL) {
        x1 = dst->clip.x;
        y1 = dst->clip.y;
        x2 = dst->clip.x + dst->clip.w - 1;
        y2 = dst->clip.y + dst->clip.h - 1;
    } else {
        x1 = r->x - dst->org_x;
        y1 = r->y - dst->org_y;
        x2 = x1 + r->w - 1;
        y2 = y1 + r->h - 1;
        if (x1 < dst->clip.x) x1 = dst->clip.x;
        if (y1 < dst->clip.y) y1 = dst->clip.y;
        if (x2 > dst->clip.x + dst->clip.w - 1)
            x2 = dst->clip.x + dst->clip.w - 1;
        if (y2 > dst->clip.y + dst->clip.h - 1)
            y2 = dst->clip.y + dst->clip.h - 1;
        if (x1 > x2 || y1 > y2) {
            r->w = 0;
            r->h = 0;
            return;
        }
        r->x = (int16_t)(x1 + dst->org_x);
        r->y = (int16_t)(y1 + dst->org_y);
        r->w = (uint16_t)(x2 - x1 + 1);
        r->h = (uint16_t)(y2 - y1 + 1);
    }
    if (x1 > x2 || y1 > y2) {
        return;
    }
    /* STDL_TRANSPARENT punches a hole in a masked surface; on a
     * maskless surface it degrades to colour 0 */
    transparent = (col >= STDL_TRANSPARENT && dst->mask != NULL);
    col &= 15;
    if (transparent) {
        col = 0;
    }
    fill_rows(dst, x1, x2, y1, y2, col, transparent);
}

void STDL_HLine(STDL_Surface *dst, int x1, int x2, int y, uint8_t col)
{
    int t;

    if (dst == NULL) {
        return;
    }
    if (x1 > x2) { t = x1; x1 = x2; x2 = t; }
    if (y < dst->clip.y || y >= dst->clip.y + dst->clip.h) {
        return;
    }
    if (x1 < dst->clip.x) x1 = dst->clip.x;
    if (x2 >= dst->clip.x + dst->clip.w)
        x2 = dst->clip.x + dst->clip.w - 1;
    if (x1 > x2) {
        return;
    }
    {
        int transparent = (col >= STDL_TRANSPARENT
                           && dst->mask != NULL);
        fill_rows(dst, x1, x2, y, y, (uint8_t)(transparent ? 0 : col & 15),
                  transparent);
    }
}

void STDL_VLine(STDL_Surface *dst, int x, int y1, int y2, uint8_t col)
{
    int t, y, p;
    uint16_t bit;
    uint8_t *base;

    if (dst == NULL) {
        return;
    }
    if (y1 > y2) { t = y1; y1 = y2; y2 = t; }
    if (x < dst->clip.x || x >= dst->clip.x + dst->clip.w) {
        return;
    }
    if (y1 < dst->clip.y) y1 = dst->clip.y;
    if (y2 >= dst->clip.y + dst->clip.h)
        y2 = dst->clip.y + dst->clip.h - 1;
    if (y1 > y2) {
        return;
    }
    {
        int transparent = (col >= STDL_TRANSPARENT
                           && dst->mask != NULL);
        uint8_t *mbase = (dst->mask != NULL)
            ? dst->mask + (uint32_t)y1 * dst->maskstride
              + ((x >> 4) * 2)
            : NULL;

        col = transparent ? 0 : (uint8_t)(col & 15);
        bit = (uint16_t)(0x8000u >> (x & 15));
        base = dst->pixels + (uint32_t)y1 * dst->stride
             + ((x >> 4) * 8);
        for (y = y1; y <= y2; y++) {
            uint16_t *grp = (uint16_t *)base;
            for (p = 0; p < 4; p++) {
                if (col & (1 << p)) {
                    grp[p] |= bit;
                } else {
                    grp[p] &= (uint16_t)~bit;
                }
            }
            if (mbase != NULL) {
                if (transparent) {
                    *(uint16_t *)mbase |= bit;
                } else {
                    *(uint16_t *)mbase &= (uint16_t)~bit;
                }
                mbase += dst->maskstride;
            }
            base += dst->stride;
        }
        if (dst->mask != NULL) {
            dst->opaque_state = 0;
        }
    }
}

void STDL_PutPixel(STDL_Surface *dst, int x, int y, uint8_t col)
{
    uint16_t *grp;
    uint16_t bit;
    int p, transparent;

    if (dst == NULL
        || x < dst->clip.x || x >= dst->clip.x + dst->clip.w
        || y < dst->clip.y || y >= dst->clip.y + dst->clip.h) {
        return;
    }
    transparent = (col >= STDL_TRANSPARENT && dst->mask != NULL);
    col = transparent ? 0 : (uint8_t)(col & 15);
    bit = (uint16_t)(0x8000u >> (x & 15));
    grp = (uint16_t *)(dst->pixels + (uint32_t)y * dst->stride
                       + ((x >> 4) * 8));
    for (p = 0; p < 4; p++) {
        if (col & (1 << p)) {
            grp[p] |= bit;
        } else {
            grp[p] &= (uint16_t)~bit;
        }
    }
    if (dst->mask != NULL) {
        uint16_t *m = (uint16_t *)(dst->mask
            + (uint32_t)y * dst->maskstride) + (x >> 4);
        if (transparent) {
            *m |= bit;
        } else {
            *m &= (uint16_t)~bit;
        }
        dst->opaque_state = 0;
    }
}

uint8_t STDL_GetPixel(const STDL_Surface *src, int x, int y)
{
    const uint16_t *grp;
    uint16_t bit;
    uint8_t col = 0;

    if (src == NULL || x < 0 || x >= src->w || y < 0 || y >= src->h) {
        return 0;
    }
    bit = (uint16_t)(0x8000u >> (x & 15));
    grp = (const uint16_t *)(src->pixels + (uint32_t)y * src->stride
                             + ((x >> 4) * 8));
    if (grp[0] & bit) col |= 1;
    if (grp[1] & bit) col |= 2;
    if (grp[2] & bit) col |= 4;
    if (grp[3] & bit) col |= 8;
    return col;
}

void STDL_Line(STDL_Surface *dst, int x1, int y1, int x2, int y2,
               uint8_t col)
{
    int dx, dy, sx, sy, err, e2;

    if (dst == NULL) {
        return;
    }
    if (y1 == y2) {
        STDL_HLine(dst, x1, x2, y1, col);
        return;
    }
    if (x1 == x2) {
        STDL_VLine(dst, x1, y1, y2, col);
        return;
    }
    dx = x2 > x1 ? x2 - x1 : x1 - x2;
    dy = y2 > y1 ? y1 - y2 : y2 - y1;   /* negative magnitude */
    sx = x1 < x2 ? 1 : -1;
    sy = y1 < y2 ? 1 : -1;
    err = dx + dy;
    for (;;) {
        STDL_PutPixel(dst, x1, y1, col);
        if (x1 == x2 && y1 == y2) {
            break;
        }
        e2 = err * 2;
        if (e2 >= dy) { err += dy; x1 += sx; }
        if (e2 <= dx) { err += dx; y1 += sy; }
    }
}

void STDL_Circle(STDL_Surface *dst, int cx, int cy, int r, uint8_t col)
{
    int x = r, y = 0, err = 1 - r;

    if (dst == NULL || r < 0) {
        return;
    }
    while (x >= y) {
        STDL_PutPixel(dst, cx + x, cy + y, col);
        STDL_PutPixel(dst, cx - x, cy + y, col);
        STDL_PutPixel(dst, cx + x, cy - y, col);
        STDL_PutPixel(dst, cx - x, cy - y, col);
        STDL_PutPixel(dst, cx + y, cy + x, col);
        STDL_PutPixel(dst, cx - y, cy + x, col);
        STDL_PutPixel(dst, cx + y, cy - x, col);
        STDL_PutPixel(dst, cx - y, cy - x, col);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void STDL_FillCircle(STDL_Surface *dst, int cx, int cy, int r,
                     uint8_t col)
{
    int x = r, y = 0, err = 1 - r;

    if (dst == NULL || r < 0) {
        return;
    }
    while (x >= y) {
        STDL_HLine(dst, cx - x, cx + x, cy + y, col);
        STDL_HLine(dst, cx - x, cx + x, cy - y, col);
        STDL_HLine(dst, cx - y, cx + y, cy + x, col);
        STDL_HLine(dst, cx - y, cx + y, cy - x, col);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}
