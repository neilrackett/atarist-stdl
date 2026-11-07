/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Unkeyed 8-plane blits of 64 pixels and more, a block at a time
 * rather than a row at a time (planes8.c's blit hands them here after
 * its clip): a scroller's window, a screen-wide strip.
 *
 * - The whole groups between the edges: at the same phase rowcopy.c's
 *   movem copy, every row in one call; at another stdl_p8_shift_rows
 *   below. The row loop's long moves took a 320x200 window 15.7ms on a
 *   TT and 31.4 on a Falcon, against 8.1 and 16.2 for a movem loop
 *   (SCRLCOST), 8.2 and 16.5 here. At a sub-group x it fetches each
 *   plane word through p8_fetch and its bounds tests: 117-125ms on a TT
 *   and 236 on a Falcon, where a whole-page 1-pixel ROXL moved the same
 *   bytes in 10.2 and 20.3; 12.4 and 24.9 here.
 * - The edges, where the destination's x or its end is off a group, a
 *   group column at a time as the row loop draws them, at either phase
 *   (at the same phase a window's word is its first's).
 *
 * What it does not take it leaves before writing anything, and
 * planes8.c's loop draws it: a keyed source, a 4-plane one, and an
 * overlap that a row at a time gets right and these would not - any
 * with an edge column, as the edges go before the middle, and whole
 * groups at the same phase where rowcopy.c says. An object of its own, as
 * planes8ex.c is, so planes8.c's code stays its own. The call after
 * p8_blit's clip still reshaped p8_blit's registers; a call at its top
 * that clipped for itself, as planes8ex.c's does, measured within 1-3%
 * of it either way on a TT and a Falcon (SCRLCOST, restores and small
 * blits included) and was 620 bytes larger.
 */

#include "responsive.h"

/*
 * Whole destination groups from a source at another phase, every row.
 * A plane pair is a long, and one rotation serves it twice: with R =
 * rol(b, sh) for the source long b and P the same of the long a group
 * before, the pair out is P where M is set and swap(R) where it is
 * not, M being 0xFFFF << sh in both halves - P's halves hold each
 * word's own bits moved up, swap(R)'s each next word's top bits moved
 * down. So a pair is a load, a rotate, a swap and a merge, and the
 * rotation carries to the next group in its register.
 *
 * (uint8_t *dp, const uint8_t *sp, long ng, long h, long sstride,
 * long dstride, long sh): dp the first group written, sp the source
 * group whose bits begin it; ng >= 1, h >= 1, sh 1-15. A row reads
 * ng + 1 groups.
 */
#ifdef __m68k__
#define P8_STR2(x) #x
#define P8_STR(x) P8_STR2(x)
#define P8_SYM(n) P8_STR(__USER_LABEL_PREFIX__) #n
#define P8_PAIR(p) \
    "move.l  (%a0)+,%d0\n\t" \
    "rol.l   %d7,%d0\n\t" \
    "move.l  %d0,%d1\n\t" \
    "swap    %d1\n\t" \
    "eor.l   %d1," p "\n\t" \
    "and.l   %d6," p "\n\t" \
    "eor.l   " p ",%d1\n\t" \
    "move.l  %d1,(%a1)+\n\t" \
    "move.l  %d0," p "\n\t"
#define P8_FIRST(p) \
    "move.l  (%a0)+," p "\n\t" \
    "rol.l   %d7," p "\n\t"
__asm__(
    "\t.text\n\t.even\n\t.globl " P8_SYM(stdl_p8_shift_rows) "\n"
    P8_SYM(stdl_p8_shift_rows) ":\n\t"
    "movem.l %d2-%d7/%a2-%a6,-(%sp)\n\t"
    "move.l  48(%sp),%a1\n\t"
    "move.l  52(%sp),%a0\n\t"
    "move.l  56(%sp),%d0\n\t"
    "lsl.l   #4,%d0\n\t"
    "move.l  %d0,%a5\n\t"               /* a row's bytes written */
    "move.l  68(%sp),%d1\n\t"
    "move.l  %d1,%a4\n\t"
    "suba.l  %d0,%a4\n\t"               /* the destination's skip */
    "move.l  64(%sp),%a3\n\t"
    "suba.l  %d0,%a3\n\t"
    "lea     -16(%a3),%a3\n\t"          /* the source's: a group more */
    "move.w  62(%sp),%d0\n\t"
    "mulu.w  %d1,%d0\n\t"
    "lea     0(%a1,%d0.l),%a6\n\t"      /* past the last row */
    "move.l  72(%sp),%d7\n\t"
    "moveq   #-1,%d6\n\t"
    "lsl.w   %d7,%d6\n\t"
    "move.w  %d6,%d0\n\t"
    "swap    %d6\n\t"
    "move.w  %d0,%d6\n"                 /* M: 0xFFFF << sh, both halves */
    "1:\n\t"
    P8_FIRST("%d2") P8_FIRST("%d3") P8_FIRST("%d4") P8_FIRST("%d5")
    "lea     0(%a1,%a5.l),%a2\n"
    "2:\n\t"
    P8_PAIR("%d2") P8_PAIR("%d3") P8_PAIR("%d4") P8_PAIR("%d5")
    "cmpa.l  %a2,%a1\n\t"
    "bne.s   2b\n\t"
    "adda.l  %a3,%a0\n\t"
    "adda.l  %a4,%a1\n\t"
    "cmpa.l  %a6,%a1\n\t"
    "bne.s   1b\n\t"
    "movem.l (%sp)+,%d2-%d7/%a2-%a6\n\t"
    "rts\n");
#else
/* C twin - what tests/host exercises and the asm must match; either
 * order of a pair's words in its long, as the merge treats both halves
 * alike */
static __inline__ uint32_t p8_rol(uint32_t v, int n)
{
    return (v << n) | (v >> (32 - n));
}

void stdl_p8_shift_rows(uint8_t *dp, const uint8_t *sp, long ng, long h,
                        long sstride, long dstride, long sh)
{
    const uint32_t m = (uint32_t)((0xFFFFu << sh) & 0xFFFFu) * 0x10001u;
    long y, g;
    int k;

    for (y = 0; y < h; y++) {
        const stdl_wlong *s = (const stdl_wlong *)(sp + y * sstride);
        stdl_wlong *d = (stdl_wlong *)(dp + y * dstride);
        uint32_t p[4];

        for (k = 0; k < 4; k++) {
            p[k] = p8_rol(s[k], (int)sh);
        }
        s += 4;
        for (g = 0; g < ng; g++, s += 4, d += 4) {
            for (k = 0; k < 4; k++) {
                const uint32_t r = p8_rol(s[k], (int)sh);
                const uint32_t t = (r << 16) | (r >> 16);

                d[k] = t ^ ((t ^ p[k]) & m);
                p[k] = r;
            }
        }
    }
}
#endif
void stdl_p8_shift_rows(uint8_t *dp, const uint8_t *sp, long ng, long h,
                        long sstride, long dstride, long sh);

/*
 * One destination group, every row, where it is a partial edge: as the
 * row loop draws it, a source word past either end of the row zero.
 * Which source groups those are is the same in every row, so they are
 * chosen once and walked by the stride (the zero group by none), as
 * the destination and its mask are: a row offset is a mulu.w, and
 * there were three a row an edge.
 */
static void p8_edge(const STDL_Surface *src, const uint8_t *srow,
                    STDL_Surface *dst, uint8_t *drow, uint8_t *mrow, int g,
                    int off, unsigned em, int h)
{
    const unsigned sng = (unsigned)src->stride >> 4;
    const unsigned wi = (unsigned)(((g << 4) + off) >> 4);
    const int in0 = wi < sng, in1 = wi + 1 < sng;
    const int sh = off & 15, as = in0 ? src->stride : 0;
    const int bs = in1 ? src->stride : 0;
    const uint32_t m = p8_m32(em);
    const uint8_t *a = in0 ? srow + (wi << 4) : (const uint8_t *)p8_zero;
    const uint8_t *b = in1 ? srow + ((wi + 1) << 4)
                           : (const uint8_t *)p8_zero;
    uint8_t *d = drow + (g << 4);
    uint16_t *mk = mrow == NULL ? NULL : (uint16_t *)mrow + g;
    int k;

    for (; h > 0; h--, a += as, b += bs, d += dst->stride) {
        const uint16_t *aw = (const uint16_t *)a;
        const uint16_t *bw = (const uint16_t *)b;
        stdl_wlong *dl = (stdl_wlong *)d;

        /* p8_merge's merge a plane pair at a time, as each is built:
         * the four built first into an array for it took 48 bytes more.
         * At the same phase a pair is the first group's long as it is:
         * a 304-pixel panel at x = 8 took 10.8ms on a TT through the
         * window, 9.7 so; the test a pair cost shifted edges 1.5% */
        if (sh == 0) {
            const stdl_wlong *al = (const stdl_wlong *)a;

            for (k = 0; k < 4; k++) {
                dl[k] ^= (dl[k] ^ al[k]) & m;
            }
        } else {
            for (k = 0; k < 4; k++) {
                const uint32_t s = STDL_PACK2(P8_WIN(aw[2 * k], bw[2 * k],
                                                     sh),
                                              P8_WIN(aw[2 * k + 1],
                                                     bw[2 * k + 1], sh));

                dl[k] ^= (dl[k] ^ s) & m;
            }
        }
        if (mk != NULL) {
            *mk &= (uint16_t)~em;
            mk = (uint16_t *)((uint8_t *)mk + dst->maskstride);
        }
    }
}

int p8_blit_rows(const STDL_Surface *src, STDL_Surface *dst, int sx,
                 int sy, int dx, int dy, int w, int h)
{
    int off, sh, g0, g1, ga, gb, y, g;
    const uint8_t *srow, *s0;
    uint8_t *drow, *mrow;

    /* a restore of a sprite or two leaves at the first test */
    if (w < 64 || !STDL_IS_RESP(src)) {
        return -1;
    }
    off = sx - dx;
    sh = off & 15;
    g0 = dx >> 4;
    g1 = (dx + w - 1) >> 4;
    srow = src->pixels + stdl_row_off(sy, src->stride);
    drow = dst->pixels + stdl_row_off(dy, dst->stride);
    mrow = dst->mask == NULL ? NULL
         : dst->mask + stdl_row_off(dy, dst->maskstride);
    /* the whole destination groups: their 16 pixels' source lies
     * inside the source rectangle, so both source groups are in the
     * row, which only an edge has to test */
    ga = g0 + ((dx & 15) != 0);
    gb = g1 - (((dx + w) & 15) != 0);
    s0 = srow + ((((ga << 4) + off) >> 4) << 4);
    /* an edge column or a shift: no overlap, as the edges go first;
     * whole groups at the same phase leave it to rowcopy.c */
    if ((sh != 0 || ga != g0 || gb != g1)
        && (gb <= ga
            || (drow < srow + stdl_row_off(h, src->stride)
                && srow < drow + stdl_row_off(h, dst->stride)))) {
        return -1;
    }
    if (ga > g0) {
        p8_edge(src, srow, dst, drow, mrow, g0, off,
                (0xFFFFu >> (dx & 15)), h);
    }
    if (gb < g1) {
        p8_edge(src, srow, dst, drow, mrow, g1, off,
                (0xFFFFu << (15 - ((dx + w - 1) & 15))) & 0xFFFFu, h);
    }
    if (sh != 0) {
        stdl_p8_shift_rows(drow + (ga << 4), s0, gb - ga + 1, h,
                           src->stride, dst->stride, sh);
    } else if (stdl_copy_rows_checked(drow + (ga << 4), s0,
                                      (gb - ga + 1) << 4, h, src->stride,
                                      dst->stride) != 0) {
        /* whole groups whose overlap its passes would get wrong:
         * nothing drawn yet. With an edge there is no overlap, and it
         * copies - a wide restore at an unaligned x, a panel in a
         * frame */
        return -1;
    }
    for (y = 0; mrow != NULL && y < h; y++, mrow += dst->maskstride) {
        uint16_t *m = (uint16_t *)mrow;

        for (g = ga; g <= gb; g++) {
            m[g] = 0;
        }
    }
    return 0;
}
