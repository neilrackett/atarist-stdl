/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * A blit onto its own memory - one surface onto itself, or two
 * borrowed over one block - with the destination at the higher
 * address: decided once, after the clip, for STDL_BlitSurfaceEx
 * (blit.c, through overlap4.c) and the 8-plane blits (planes8.c and
 * planes8ex.c, through overlap8.c). Its pieces go through the caller's
 * own blit: a call to STDL_BlitSurfaceEx here linked blit.o, all 13.8K
 * of the 4-plane paths, into every program drawing on 8 planes.
 *
 * An object of its own and built for size, as a rare path: code added
 * to blit.c has moved STDL_BlitSurfaceEx's other paths 1-7% on a plain
 * ST (AGENTS.md), and at -O2 gcc copied the pieces' set-up into every
 * branch here that reaches it: 1,500 bytes, against 1,224 at -Os.
 */

#include <string.h>
#include "stdl_internal.h"

/* bytes of rows a move right goes through at a time, and a group of
 * guard either side, which a shifted blit reads beside its rows */
#define BOUNCE 1024

/*
 * Headers over rows sy.. of src and dy.. of dst, h of them, for a blit
 * onto its own memory: no origin, clip or borrowing, so that the blit
 * through them is not asked about overlap again; with up, their first
 * rows the last and their 16-bit pixel strides stepping back a row,
 * which the unmasked same-phase row paths read signed.
 */
static void blit_heads(STDL_Surface *t,
        const STDL_Surface *src, const STDL_Surface *dst, int sy, int dy,
        int h, int up)
{
    int k;

    for (k = 0; k < 2; k++, t++) {
        const STDL_Surface *s = k ? dst : src;
        int y = (k ? dy : sy) + (up ? h - 1 : 0);

        *t = *s;
        t->org_x = t->org_y = 0;
        t->flags &= ~STDL_PREALLOC;
        t->clip.x = t->clip.y = 0;
        t->clip.w = (uint16_t)s->w;
        t->clip.h = (uint16_t)h;
        t->h = (int16_t)h;
        t->pixels = s->pixels + stdl_row_off(y, s->stride);
        if (s->mask != NULL) {
            t->mask = s->mask + stdl_row_off(y, s->maskstride);
        }
        if (up) {                       /* never with a mask read */
            t->stride = (uint16_t)-s->stride;
        }
    }
}

/*
 * A blit onto its own memory, decided once after the clip
 * (STDL_BlitSurfaceEx, p8_blit and p8_blit_ex ask only where src and
 * dst are one surface, or one is borrowed; coordinates are clipped
 * storage ones): -1 where the rows go as they always have, 0 having
 * done it.
 *
 * Rows top down and left to right are right with the destination at
 * the lower address. Rows below the source's run from the bottom: a
 * page moved down - the same phase, no key, no destination mask - on 4
 * planes through the usual paths over headers that step back a row
 * (the BLiTTER's line increments are signed too), on 8 planes as
 * rowcopy.c's movem copy from the last row where its one pass holds
 * them; anything else in bands from the bottom, none taller than the
 * rows between a source row's end and its destination. Along the same
 * rows to the right, whole groups at the same phase with no key, flags
 * or destination mask (every plane, on 4 planes) are a memmove a row,
 * which copies backwards where it must; anything else goes through a
 * buffer on the stack as many rows at a time as fit, or, keyed or
 * wider than the buffer, in columns from the right none wider than the
 * move. No piece overlaps its own source or writes where a later one
 * reads. The BLiTTER's keyed passes - XOR, AND and XOR over the whole
 * rectangle, the last reading what the first wrote - are wrong for any
 * overlap, so a keyed one goes to the CPU, as one the beam would see
 * does. The extents are the rows' groups, a destination row taken a
 * group (16 bytes) wider, as its phase can reach one more than the
 * source's.
 */
int stdl_blit_overlap_by(stdl_blit_fn blit, const STDL_Surface *src,
                         STDL_Surface *dst, int sx, int sy, int dx, int dy,
                         int w, int h, unsigned flags)
{
    const int gb = (dst->planes & 8) ? 4 : 3;     /* a group: 16 or 8 */
    const uint8_t *s0 = src->pixels + stdl_row_off(sy, src->stride)
                      + ((sx >> 4) << gb);
    const uint8_t *d0 = dst->pixels + stdl_row_off(dy, dst->stride)
                      + ((dx >> 4) << gb);
    const int span = (((sx & 15) + w + 15) >> 4) << gb;
    const int masked = (src->flags & STDL_SRCKEY) && src->mask != NULL;
    const int keyed = masked && gb == 3;
    const int right = d0 > s0 || (d0 == s0 && (dx & 15) > (sx & 15));
    STDL_Surface t[3];
    uint32_t buf[(BOUNCE + 32) / 4];
    int d = w, e = h, x, y, n, m, was, up, bounce = 0;

    if (((src->planes ^ dst->planes) & 8)       /* 4 planes over 8 */
        || (right ? d0 >= s0 + stdl_row_off(h - 1, src->stride) + span
                  : !keyed || s0 >= d0 + stdl_row_off(h - 1, dst->stride)
                                    + span + 16)) {
        return -1;
    }
    up = right && d0 >= s0 + span;
    if (up) {
        /* 4-plane paths read their strides signed only at the same
         * phase, with no key and no destination mask - signed
         * everywhere, keyed blits took 1.2-1.7% longer on a plain ST
         * (SPRCOST), through BlitSurfaceEx's registers. 8-plane whole
         * groups are the movem copy from the last row; with a test of
         * their own in p8_blit's row loop, small shifted blits were 10%
         * slower on a TT and a Falcon. The rest go in bands from the
         * bottom, none taller than the rows between a source row's end
         * and its destination, so none overlaps its own source. */
        const int plain = ((sx ^ dx) & 15) == 0 && !masked
                          && dst->mask == NULL;

        if (!plain || gb == 4) {
            if (plain && ((sx | w) & 15) == 0
                && stdl_copy_rows_checked(
                       (uint8_t *)d0 + stdl_row_off(h - 1, dst->stride),
                       s0 + stdl_row_off(h - 1, src->stride), span, h,
                       -(int)src->stride, -(int)dst->stride) == 0) {
                return 0;
            }
            up = 0;
            e = src->stride == dst->stride
              ? stdl_divu((uint32_t)(d0 - s0 - span), src->stride) + 1
              : 1;
        }
    } else if (right) {
        /* the shift in pixels, in memory: two surfaces over one block
         * have coordinates of their own */
        d = (int)((d0 - s0) >> gb) * 16 + (dx & 15) - (sx & 15);
        if (((sx | dx | w) & 15) == 0 && flags == 0 && !masked
            && dst->mask == NULL && (gb == 4 || stdl_planes == 4)) {
            for (n = h; n > 0; n--) {
                memmove((uint8_t *)d0, s0, (size_t)(w >> 4) << gb);
                s0 += src->stride;
                d0 += dst->stride;
            }
            return 0;
        }
        /* the rest through a buffer on the stack, as many rows at a
         * time as fit: copied there whole groups at the same phase,
         * then blitted from there as asked. Columns no wider than the
         * move took 3.6s to move 304x200 pixels right by one on a
         * plain ST, a call a column; a keyed source, whose mask the
         * buffer would need too, still goes that way */
        if (!masked && span <= BOUNCE) {
            e = stdl_divu(BOUNCE, (uint16_t)span);
            d = w;
            bounce = 1;
        }
    }
    blit_heads(t, src, dst, sy, dy, h, up);
    t[2] = t[0];
    t[2].pixels = (uint8_t *)buf + 16;
    t[2].stride = (uint16_t)span;
    t[2].w = t[2].clip.w = (uint16_t)(span << (4 - gb));
    t[2].h = t[2].clip.h = (uint16_t)e;
    t[2].mask = NULL;
    was = STDL_UseBlitter(stdl_drawn_visibly(dst) || keyed ? 0 : -1);
    /* columns from the right no wider than d, or bands from the bottom
     * no taller than e: one of the two, or the whole */
    for (x = w, y = h; x > 0 && y > 0;) {
        STDL_Rect r, at;

        n = x < d ? x : d;
        m = y < e ? y : e;
        r.x = (int16_t)(sx + x - n);
        r.y = (int16_t)(y - m);
        r.w = (uint16_t)n;
        r.h = (uint16_t)m;
        at.x = (int16_t)(dx + x - n);
        at.y = r.y;
        if (bounce) {
            STDL_Rect f, z;

            f.x = (int16_t)(sx & ~15);
            f.y = r.y;
            f.w = (uint16_t)t[2].w;
            f.h = (uint16_t)m;
            z.x = z.y = 0;
            blit(&t[0], &f, &t[2], &z, 0);
            r.x = (int16_t)(sx & 15);
            r.y = 0;
        }
        blit(bounce ? &t[2] : &t[0], &r, &t[1], &at, flags);
        if (m < h) {
            y -= m;
        } else {
            x -= n;
        }
    }
    STDL_UseBlitter(was);
    dst->opaque_state = 0;
    return 0;
}
