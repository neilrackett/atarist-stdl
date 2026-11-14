/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The ST build's own parts (st.h): the palette's 256 colours reduced
 * to an ST's 16, and 8-bit pixels converted through that reduction
 * into ST Low's four interleaved planes. For testing a program on an
 * ST, not for playing it there: only libstdl-sdl2-st.a has this
 * object, built for the 68000, and the add-on a TT or a Falcon runs
 * has none of it.
 */

#include "st.h"

#ifndef STDL_SDL2_ST
#error "st.c belongs to the ST build of the add-on (make sdl2st)"
#endif

/*
 * The reduction: a cut at the midpoint. The colours start in one box;
 * the box widest in any channel is cut in two at the middle of that
 * channel, until there are 15 or none is wider than a colour. A box's
 * slot shows the mean of its colours. Slot 0 is black, the border's
 * register on an ST, and takes the palette's blacks and the border
 * index. No colour is weighed by how much of the picture uses it, as
 * that would mean counting pixels: this reads the palette alone, 256
 * entries a few times over.
 *
 * Up to 15 colours beside black each get a slot of their own, since a
 * box of two colours can always be cut.
 */

struct box {
    int start, count;
    int axis;                   /* its widest channel: 0 r, 1 g, 2 b */
    int lo, hi;                 /* and that channel's range */
};

static int channel(const SDL_Color *c, int axis)
{
    return axis == 0 ? c->r : axis == 1 ? c->g : c->b;
}

/* a box's widest channel and its range; the three written out and in
 * scalars, as gcc 4.6 kept arrays of them on the stack */
static void measure(struct box *b, const Uint8 *idx, const SDL_Color *col)
{
    int rlo = 255, glo = 255, blo = 255, rhi = 0, ghi = 0, bhi = 0;
    int i;
    for (i = b->start; i < b->start + b->count; i++) {
        const SDL_Color *c = &col[idx[i]];
        const int cr = c->r, cg = c->g, cb = c->b;
        if (cr < rlo) {
            rlo = cr;
        }
        if (cr > rhi) {
            rhi = cr;
        }
        if (cg < glo) {
            glo = cg;
        }
        if (cg > ghi) {
            ghi = cg;
        }
        if (cb < blo) {
            blo = cb;
        }
        if (cb > bhi) {
            bhi = cb;
        }
    }
    b->axis = 0;
    b->lo = rlo;
    b->hi = rhi;
    if (ghi - glo > b->hi - b->lo) {
        b->axis = 1;
        b->lo = glo;
        b->hi = ghi;
    }
    if (bhi - blo > b->hi - b->lo) {
        b->axis = 2;
        b->lo = blo;
        b->hi = bhi;
    }
}

Uint32 stdl_sdl2_st_reduce(const SDL_Color *col, int ncol, unsigned x,
                         Uint8 *map, SDL_Color *out)
{
    Uint8 idx[256];
    struct box box[15];
    int n = 0, nbox = 0, i, k;

    memset(map, 0, 256);
    for (i = 0; i < ncol && i < 256; i++) {
        if ((x && (unsigned)i == x) || !(col[i].r | col[i].g | col[i].b)) {
            continue;
        }
        idx[n++] = (Uint8)i;
    }
    if (n) {
        box[0].start = 0;
        box[0].count = n;
        measure(&box[0], idx, col);
        nbox = 1;
    }
    while (nbox && nbox < 15) {
        struct box *b = &box[0], *nb = &box[nbox];
        int mid, lo, hi;
        for (k = 1; k < nbox; k++) {
            if (box[k].hi - box[k].lo > b->hi - b->lo) {
                b = &box[k];
            }
        }
        if (b->hi == b->lo) {
            break;
        }
        /* lo < mid <= hi, so neither half is empty */
        mid = (b->lo + b->hi + 1) >> 1;
        lo = b->start;
        hi = b->start + b->count - 1;
        while (lo <= hi) {
            if (channel(&col[idx[lo]], b->axis) < mid) {
                lo++;
            } else {
                const Uint8 t = idx[lo];
                idx[lo] = idx[hi];
                idx[hi--] = t;
            }
        }
        nb->start = lo;
        nb->count = b->start + b->count - lo;
        b->count = lo - b->start;
        measure(b, idx, col);
        measure(nb, idx, col);
        nbox++;
    }
    for (k = 0; k < nbox; k++) {
        for (i = box[k].start; i < box[k].start + box[k].count; i++) {
            map[idx[i]] = (Uint8)(k + 1);
        }
    }
    return stdl_sdl2_st_fit(col, ncol, x, map, out);
}

static unsigned absdiff(unsigned a, unsigned b)
{
    return a > b ? a - b : b - a;
}

Uint32 stdl_sdl2_st_fit(const SDL_Color *col, int ncol, unsigned x,
                        const Uint8 *map, SDL_Color *out)
{
    unsigned sum[16][3], cnt[16];
    Uint32 err = 0;
    int i, k;
    if (ncol > 256) {
        ncol = 256;
    }
    memset(sum, 0, sizeof(sum));
    memset(cnt, 0, sizeof(cnt));
    for (i = 0; i < ncol; i++) {
        const unsigned s = map[i];
        sum[s][0] += col[i].r;
        sum[s][1] += col[i].g;
        sum[s][2] += col[i].b;
        cnt[s]++;
    }
    memset(out, 0, 16 * sizeof(*out));
    for (k = 0; k < 16; k++) {
        const unsigned n = cnt[k];
        out[k].a = 255;
        if (k && n) {
            out[k].r = (Uint8)((sum[k][0] + n / 2) / n);
            out[k].g = (Uint8)((sum[k][1] + n / 2) / n);
            out[k].b = (Uint8)((sum[k][2] + n / 2) / n);
        }
    }
    for (i = 0; i < ncol; i++) {
        const SDL_Color *o = &out[map[i]];
        if (x && (unsigned)i == x) {
            continue;
        }
        err += absdiff(col[i].r, o->r) + absdiff(col[i].g, o->g) +
               absdiff(col[i].b, o->b);
    }
    return err;
}

/*
 * A palette change. A new map means every page converted again, a
 * whole screen each (video.c), where new colours through the old map
 * are 16 registers; and a fade's steps, the same colours scaled, are
 * rarely cut quite alike, as a colour a step from a cut's middle
 * falls on its other side. So the map in use stays while it shows
 * the new colours nearly as well as the one made for them: its error
 * within a quarter of the new one's, each the colours' distances from
 * their slots' means, a channel at a time. An ST took 0.76s a step of
 * a fade to black in 16 over a ramp of 256 colours when every step
 * moved the map (Hatari, 8MHz).
 */
int stdl_sdl2_st_update(const SDL_Color *col, int ncol, unsigned x,
                        Uint8 *map, SDL_Color *out)
{
    Uint8 fresh[256];
    SDL_Color kept[16];
    Uint32 efresh, ekept;
    efresh = stdl_sdl2_st_reduce(col, ncol, x, fresh, out);
    if (memcmp(fresh, map, sizeof(fresh)) == 0) {
        return 0;
    }
    ekept = stdl_sdl2_st_fit(col, ncol, x, map, kept);
    if (ekept <= efresh + efresh / 4) {
        memcpy(out, kept, sizeof(kept));
        return 0;
    }
    memcpy(map, fresh, sizeof(fresh));
    return 1;
}

/*
 * The conversion. A slot's four bits are spread a byte each over a
 * long (stdl_sdl2_st_spread), so eight pixels shifted in one after
 * another leave each of the long's bytes a plane's eight pixels: a
 * group is two such longs, the first the high bytes of its four plane
 * words and the second the low ones, which is what movep writes. A
 * lookup a pixel and no merges, in gcc's C about 60 cycles a pixel.
 */

void stdl_sdl2_st_spread(const Uint8 *map, Uint32 *tab)
{
    int i;
    for (i = 0; i < 256; i++) {
        const Uint32 m = map[i];
        tab[i] = (m & 1) << 24 | (m & 2) << 15 | (m & 4) << 6 | (m & 8) >> 3;
    }
}

#define PIX(a, s) ((a) + (a) + tab[*(s)++])

void stdl_sdl2_st_c2p(const Uint8 *src, long sp, Uint8 *dst, long dp,
                      long groups, long h, const Uint32 *tab)
{
    for (; h > 0; h--, src += sp, dst += dp) {
        const Uint8 *s = src;
        Uint8 *d = dst;
        long g;
        for (g = groups; g > 0; g--, d += 8) {
            Uint32 a = tab[*s++], b;
            a = PIX(a, s);
            a = PIX(a, s);
            a = PIX(a, s);
            a = PIX(a, s);
            a = PIX(a, s);
            a = PIX(a, s);
            a = PIX(a, s);
            b = tab[*s++];
            b = PIX(b, s);
            b = PIX(b, s);
            b = PIX(b, s);
            b = PIX(b, s);
            b = PIX(b, s);
            b = PIX(b, s);
            b = PIX(b, s);
#ifdef __m68k__
            __asm__ volatile("movep.l %0,0(%2)\n\tmovep.l %1,1(%2)"
                             : : "d" (a), "d" (b), "a" (d) : "memory");
#else
            d[0] = (Uint8)(a >> 24);
            d[2] = (Uint8)(a >> 16);
            d[4] = (Uint8)(a >> 8);
            d[6] = (Uint8)a;
            d[1] = (Uint8)(b >> 24);
            d[3] = (Uint8)(b >> 16);
            d[5] = (Uint8)(b >> 8);
            d[7] = (Uint8)b;
#endif
        }
    }
}
