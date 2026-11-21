/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The ST build's own parts (st.h): the palette's 256 colours reduced
 * to an ST's 16, and 8-bit pixels converted through that reduction
 * into four interleaved planes. For testing a program on an ST, not
 * for playing it there: only libstdl-sdl2-st.a has this
 * object, built for the 68000, and the add-on a TT or a Falcon runs
 * has none of it.
 */

#include "st.h"

#ifndef STDL_SDL2_ST
#error "st.c belongs to the ST build of the add-on (make sdl2st)"
#endif

/*
 * The reduction: the palette's colours as 16, made from the colours on
 * the screen (seen) and not from the palette as a whole. A program's
 * palette has many colours its picture does not show (another room's,
 * entries never used), and every one of them took a share of the 16:
 * over 15 frames of one port's game, the error after a 2x2 average
 * fell from 84 to 47 on an ST and from 83 to 26 on an STE when only
 * the colours on screen took part (score made on the host from frames
 * the 030 build dumped, against the 256 colours a TT showed).
 *
 * - The cut: the colours seen start in one box, and the box widest in
 *   any channel is cut in two at that channel's middle, until there
 *   are 15 or none is wider than a colour. Up to 15 colours beside
 *   black so each get a slot of their own. A slot's colour is the mean
 *   of its colours, then the nearest the machine shows, 3 bits a
 *   channel on an ST and 4 on an STE (bits; anything else as it is).
 * - A colour not seen goes to the nearest slot, so that one drawn
 *   before the next reduction is shown as near as the 16 allow.
 * - Slot 0 is black, the border's register on an ST, and takes the
 *   palette's blacks, the border index and every index past the
 *   palette.
 * - The second slot, b[], for the checkerboard (dither): the
 *   slot whose mix with the first, half and half, is nearest the
 *   colour, if any is nearer than the first alone by more than an
 *   eighth of the two slots' own distance, which keeps a harsh pattern
 *   for where it earns its place. Without the flag, the first again.
 *   Over the same 15 frames, 47 to 38 on an ST and 26 to 23 on an STE.
 *
 * Tried and left out, by the same score over the first two frames:
 * colours snapped to the machine's levels before the cut (an ST worse,
 * 41 to 95), the channels weighted red 2, green 4, blue 1 (worse on
 * both, 41 to 56 and 11 to 26), and refining the cut by moving each
 * colour to the nearest slot (no change at all).
 */

static Uint8 snaps[2][256];     /* 3 bits and 4: a channel as shown */
static int snaps_made;

static const Uint8 *snap_for(int bits)
{
    if (!snaps_made) {
        int v;
        for (v = 0; v < 256; v++) {
            /* the nearest of 8 levels, of 16 */
            snaps[0][v] = (Uint8)(((v * 7 + 127) / 255) * 255 / 7);
            snaps[1][v] = (Uint8)(((v * 15 + 127) / 255) * 17);
        }
        snaps_made = 1;
    }
    return bits == 3 ? snaps[0] : bits == 4 ? snaps[1] : NULL;
}

static __inline__ unsigned absdiff(unsigned a, unsigned b)
{
    return a > b ? a - b : b - a;
}

static __inline__ unsigned dist(const SDL_Color *p, const SDL_Color *q)
{
    return absdiff(p->r, q->r) + absdiff(p->g, q->g) + absdiff(p->b, q->b);
}

/* half and half of two colours, as a checkerboard of them shows */
static __inline__ SDL_Color mix(const SDL_Color *p, const SDL_Color *q)
{
    SDL_Color m;
    m.r = (Uint8)((p->r + q->r + 1u) >> 1);
    m.g = (Uint8)((p->g + q->g + 1u) >> 1);
    m.b = (Uint8)((p->b + q->b + 1u) >> 1);
    m.a = 255;
    return m;
}

/* entry i is a colour: inside the palette, not the border index x, not
 * black; anything else is slot 0 */
static __inline__ int colour(const SDL_Color *col, int ncol, unsigned x,
                             int i)
{
    return i < ncol && !(x && (unsigned)i == x) &&
           (col[i].r | col[i].g | col[i].b) != 0;
}

/* the colours that take part, those seen (all with no seen), as a list
 * of their indices */
static int in_use(const SDL_Color *col, int ncol, unsigned x,
                  const Uint8 *seen, Uint8 *list)
{
    int i, n = 0;
    for (i = 0; i < ncol; i++) {
        if ((!seen || seen[i]) && colour(col, ncol, x, i)) {
            list[n++] = (Uint8)i;
        }
    }
    return n;
}

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

/* each slot's colour the mean of the colours in use whose first slot
 * it is (raw), and as the machine shows it (c); slot 0 and a slot of
 * none black. A quotient fits 16 bits, so the 68000's divide does it,
 * where a 32-bit one is a library call. */
static void means(const SDL_Color *col, const Uint8 *list, int n,
                  const Uint8 *a, int bits, SDL_Color *raw, SDL_Color *c)
{
    const Uint8 *sn = snap_for(bits);
    unsigned sum[16][3], cnt[16];
    int i, k;
    memset(sum, 0, sizeof(sum));
    memset(cnt, 0, sizeof(cnt));
    for (i = 0; i < n; i++) {
        const SDL_Color *w = &col[list[i]];
        const unsigned s = a[list[i]];
        sum[s][0] += w->r;
        sum[s][1] += w->g;
        sum[s][2] += w->b;
        cnt[s]++;
    }
    for (k = 0; k < 16; k++) {
        const unsigned m = cnt[k];
        raw[k].r = raw[k].g = raw[k].b = 0;
        raw[k].a = 255;
        if (k && m) {
            raw[k].r = (Uint8)STDL_DivU16(sum[k][0] + m / 2, m);
            raw[k].g = (Uint8)STDL_DivU16(sum[k][1] + m / 2, m);
            raw[k].b = (Uint8)STDL_DivU16(sum[k][2] + m / 2, m);
        }
        c[k] = raw[k];
        if (sn) {
            c[k].r = sn[c[k].r];
            c[k].g = sn[c[k].g];
            c[k].b = sn[c[k].b];
        }
    }
}

/* each colour not seen to the nearest slot, the lowest of equals; a
 * channel at a time, as a slot whose first channels are already as far
 * as the best cannot be nearer */
static void nearest(const SDL_Color *col, int ncol, unsigned x,
                    const Uint8 *seen, const SDL_Color *c, Uint8 *a)
{
    int i, k;
    if (!seen) {
        return;
    }
    for (i = 0; i < ncol; i++) {
        const SDL_Color *w = &col[i];
        unsigned best, bk = 0;
        if (seen[i] || !colour(col, ncol, x, i)) {
            continue;
        }
        best = (unsigned)w->r + w->g + w->b;
        for (k = 1; k < 16 && best; k++) {
            unsigned d = absdiff(w->r, c[k].r);
            if (d >= best) {
                continue;
            }
            d += absdiff(w->g, c[k].g);
            if (d >= best) {
                continue;
            }
            d += absdiff(w->b, c[k].b);
            if (d < best) {
                best = d;
                bk = (unsigned)k;
            }
        }
        a[i] = (Uint8)bk;
    }
}

/* the palette's colours the reduction takes, 256 at most */
static int palette_size(int ncol)
{
    return ncol < 0 ? 0 : ncol > 256 ? 256 : ncol;
}

/* every colour's second slot, with the checkerboard; the first again
 * without it, and 0 for anything not a colour. A pair's mix and the
 * penalty for its pattern, an eighth of the two slots' distance, are
 * made once for the 256 pairs rather than at each of up to 3,840
 * candidates; a pair whose penalty alone is as far as the best cannot
 * win, and a slot's pair with itself has a penalty nothing beats. */
static void seconds(const SDL_Color *col, int ncol, unsigned x,
                    const SDL_Color *c, const Uint8 *a, Uint8 *b,
                    int dither)
{
    SDL_Color mid[16][16];
    Uint16 pen[16][16];
    int i, k;
    memcpy(b, a, 256);
    if (!dither) {
        return;
    }
    for (i = 0; i < 16; i++) {
        for (k = 0; k < 16; k++) {
            mid[i][k] = mix(&c[i], &c[k]);
            pen[i][k] = i == k ? 0xFFFF : (Uint16)(dist(&c[i], &c[k]) >> 3);
        }
    }
    for (i = 0; i < ncol; i++) {
        const SDL_Color *w = &col[i];
        const int p = a[i];
        unsigned best;
        if (!colour(col, ncol, x, i)) {
            continue;
        }
        best = dist(w, &c[p]);
        for (k = 0; k < 16 && best; k++) {
            unsigned d;
            if (pen[p][k] >= best) {
                continue;
            }
            d = dist(w, &mid[p][k]) + pen[p][k];
            if (d < best) {
                best = d;
                b[i] = (Uint8)k;
            }
        }
    }
}

void stdl_sdl2_st_seconds(const SDL_Color *col, int ncol, unsigned x,
                          const SDL_Color *c, const Uint8 *a, Uint8 *b,
                          int dither)
{
    seconds(col, palette_size(ncol), x, c, a, b, dither);
}

int stdl_sdl2_st_use(struct stdl_sdl2_st_maps *m, const Uint8 *a,
                     const Uint8 *b)
{
    if (memcmp(a, m->a, sizeof(m->a)) == 0 &&
        memcmp(b, m->b, sizeof(m->b)) == 0) {
        return 0;
    }
    memcpy(m->a, a, sizeof(m->a));
    memcpy(m->b, b, sizeof(m->b));
    return 1;
}

/* how far the colours in use are from the mix of their two slots, as
 * a share of those colours' own size, so that the same picture dimmed
 * has the same figure: 256ths of a 16th. The slots' colours are their
 * means as they are, not the machine's levels, which says how well the
 * maps fit and not how the machine rounds: by the levels, a fade to
 * black in 16 over a ramp made the maps again at 9 of its steps on an
 * ST, a whole screen converted each time, its error growing as the
 * colours neared the darkest levels. */
static Uint32 error(const SDL_Color *col, const Uint8 *list, int n,
                    const SDL_Color *raw, const Uint8 *a, const Uint8 *b)
{
    Uint32 err = 0, size = 0;
    int i;
    for (i = 0; i < n; i++) {
        const SDL_Color *w = &col[list[i]];
        const SDL_Color m = mix(&raw[a[list[i]]], &raw[b[list[i]]]);
        err += dist(w, &m);
        size += (Uint32)w->r + w->g + w->b;
    }
    return (err << 8) / ((size >> 4) + 1);
}

Uint32 stdl_sdl2_st_reduce(const SDL_Color *col, int ncol, unsigned x,
                           int bits, int dither, const Uint8 *seen,
                           Uint8 *a, Uint8 *b, SDL_Color *out)
{
    SDL_Color c[16], raw[16];
    Uint8 idx[256];
    struct box box[15];
    int n, nbox = 0, i, k;

    ncol = palette_size(ncol);
    n = in_use(col, ncol, x, seen, idx);
    memset(a, 0, 256);
    if (n) {
        box[0].start = 0;
        box[0].count = n;
        measure(&box[0], idx, col);
        nbox = 1;
    }
    while (nbox && nbox < 15) {
        struct box *bx = &box[0], *nb = &box[nbox];
        int mid, lo, hi;
        for (k = 1; k < nbox; k++) {
            if (box[k].hi - box[k].lo > bx->hi - bx->lo) {
                bx = &box[k];
            }
        }
        if (bx->hi == bx->lo) {
            break;
        }
        /* lo < mid <= hi, so neither half is empty */
        mid = (bx->lo + bx->hi + 1) >> 1;
        lo = bx->start;
        hi = bx->start + bx->count - 1;
        while (lo <= hi) {
            if (channel(&col[idx[lo]], bx->axis) < mid) {
                lo++;
            } else {
                const Uint8 t = idx[lo];
                idx[lo] = idx[hi];
                idx[hi--] = t;
            }
        }
        nb->start = lo;
        nb->count = bx->start + bx->count - lo;
        bx->count = lo - bx->start;
        measure(bx, idx, col);
        measure(nb, idx, col);
        nbox++;
    }
    for (k = 0; k < nbox; k++) {
        for (i = box[k].start; i < box[k].start + box[k].count; i++) {
            a[idx[i]] = (Uint8)(k + 1);
        }
    }
    /* idx is the colours in use still, in another order */
    means(col, idx, n, a, bits, raw, c);
    nearest(col, ncol, x, seen, c, a);
    seconds(col, ncol, x, c, a, b, dither);
    memcpy(out, c, sizeof(c));
    return error(col, idx, n, raw, a, b);
}

Uint32 stdl_sdl2_st_fit(const SDL_Color *col, int ncol, unsigned x, int bits,
                        const Uint8 *seen, const Uint8 *a, const Uint8 *b,
                        SDL_Color *out)
{
    SDL_Color c[16], raw[16];
    Uint8 idx[256];
    int n;
    ncol = palette_size(ncol);
    n = in_use(col, ncol, x, seen, idx);
    means(col, idx, n, a, bits, raw, c);
    memcpy(out, c, sizeof(c));
    return error(col, idx, n, raw, a, b);
}

/*
 * A palette change, or colours seen that were not. A new map means
 * every page converted again, a whole screen (video.c), where new
 * colours through the old map are 16 registers; and a fade's steps,
 * the same colours dimmed, are rarely cut quite alike, as a colour a
 * step from a cut's middle falls on its other side. So the maps in use
 * stay while they show the colours seen nearly as well as they showed
 * those they were made for: their error within a quarter of what it
 * was then (m->made), and a little for rounding. That is one pass over
 * the colours in use where making new maps to compare with was the
 * whole reduction. An ST took 0.76s a step of a fade to black in 16
 * over a ramp of 256 colours when every step moved the map (Hatari,
 * 8MHz).
 */
int stdl_sdl2_st_keep(const SDL_Color *col, int ncol, unsigned x, int bits,
                      const Uint8 *seen, const struct stdl_sdl2_st_maps *m,
                      SDL_Color *out)
{
    Uint32 e;
    if (!m->made_valid) {
        return 0;
    }
    e = stdl_sdl2_st_fit(col, ncol, x, bits, seen, m->a, m->b, out);
    return e <= m->made + (m->made >> 2) + 2;
}

int stdl_sdl2_st_remake(const SDL_Color *col, int ncol, unsigned x, int bits,
                        int dither, const Uint8 *seen,
                        struct stdl_sdl2_st_maps *m, SDL_Color *out)
{
    Uint8 a[256], b[256];
    m->made = stdl_sdl2_st_reduce(col, ncol, x, bits, dither, seen, a, b,
                                  out);
    m->made_valid = 1;
    return stdl_sdl2_st_use(m, a, b);
}

/*
 * The conversion. A slot's four bits are spread a byte each over a
 * long, so the pixels of a byte of each plane ORed together, each
 * shifted to its place, leave each of the long's bytes a plane's eight
 * pixels: a group is two such longs, the first the high bytes of its
 * four plane words and the second the low ones, which is what movep
 * writes. The table has the spread four times over, shifted 3, 2, 1
 * and 0 (stdl_sdl2_st_spread), so four pixels are four lookups and no
 * shifts, and the next four go in after one shift by 4.
 *
 * The checkerboard costs the loop nothing: there are two such tables,
 * one for the rows where a pixel at an even x shows its first slot
 * and one at an odd x its second, the other for the rows between, and
 * the tables for a pixel's place already tell an even x from an odd
 * one, a group starting on an even one. The rows take them in turn.
 *
 * On the machine the rows are assembler, with the C as its twin, which
 * the host runs: gcc 4.6 built one table and a shift a pixel at about
 * 60 cycles a pixel, a clear and a 16-bit index among them where the
 * assembler has 40 (a clear, the byte, two adds for the index, the OR)
 * and a shift every fourth. The test's own pixel-by-pixel conversion
 * is what both are held to (test_render.c), on the machine too.
 */

static Uint32 spread(Uint32 m)
{
    return (m & 1) << 24 | (m & 2) << 15 | (m & 4) << 6 | (m & 8) >> 3;
}

void stdl_sdl2_st_spread(const Uint8 *a, const Uint8 *b, Uint32 *tab)
{
    int i, k;
    for (i = 0; i < 256; i++) {
        const Uint32 va = spread(a[i]), vb = spread(b[i]);
        for (k = 0; k < 4; k++) {
            /* an even row: the first slot at an even x */
            tab[k * 256 + i] = ((k & 1) ? vb : va) << (3 - k);
            tab[1024 + k * 256 + i] = ((k & 1) ? va : vb) << (3 - k);
        }
    }
}

#ifdef __m68k__
/* a pixel: its byte, doubled twice for a long's index, d1 */
#define ST_P(op, t) \
    "	moveq	#0,%d1\n" \
    "	move.b	(%a0)+,%d1\n" \
    "	add.w	%d1,%d1\n" \
    "	add.w	%d1,%d1\n" \
    "	" op "	(" t ",%d1.w),"
#define ST_L(r) \
    ST_P("move.l", "%a2") r "\n" ST_P("or.l", "%a3") r "\n" \
    ST_P("or.l", "%a4") r "\n" ST_P("or.l", "%a5") r "\n" \
    "	lsl.l	#4," r "\n" \
    ST_P("or.l", "%a2") r "\n" ST_P("or.l", "%a3") r "\n" \
    ST_P("or.l", "%a4") r "\n" ST_P("or.l", "%a5") r "\n"

__asm__(ST_FUNC("stdl_sdl2_st_c2p", "", ST_L("%d0") ST_L("%d2")));
#else
#define FOUR(s) (tab[(s)[0]] | tab[256 + (s)[1]] | tab[512 + (s)[2]] | \
                 tab[768 + (s)[3]])

void stdl_sdl2_st_c2p(const Uint8 *src, long sp, Uint8 *dst, long dp,
                      long groups, long h, const Uint32 *tab,
                      const Uint32 *next)
{
    for (; h > 0; h--, src += sp, dst += dp) {
        const Uint8 *s = src;
        const Uint32 *row = tab;
        Uint8 *d = dst;
        long g;
        for (g = groups; g > 0; g--, s += 16, d += 8) {
            stdl_sdl2_st_store(d, FOUR(s) << 4 | FOUR(s + 4),
                               FOUR(s + 8) << 4 | FOUR(s + 12));
        }
        tab = next;
        next = row;
    }
}
#endif
