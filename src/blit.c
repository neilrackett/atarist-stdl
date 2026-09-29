/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_Blit: surface copying.
 *
 * Terms: a "group" is 16 pixels = 4 consecutive plane words (8
 * bytes). "Phase" is x & 15. Same-phase blits walk words with edge
 * masks; different phases go through the shift chain, which reads a
 * 32-bit window per group (and may read one group beyond either end
 * of the source rectangle - surfaces carry guard bytes for this).
 *
 * Masks follow the format contract: bit set = destination preserved.
 */

/* BLIT_INLINE_MAX is in stdl_internal.h, shared with the BLiTTER's
 * size rules */

#include <string.h>
#include "stdl_internal.h"

/* --- same-phase copy ------------------------------------------- */

/*
 * A short row, copied inline rather than through memcpy (see
 * BLIT_INLINE_MAX). bytes is a whole number of groups, from 8 up to
 * BLIT_INLINE_MAX; lng says both pointers are long aligned, which a
 * surface from STDL_CreateSurfaceFrom need not be - and a long move
 * to an odd word is an address error on a 68000.
 */
static __inline__ __attribute__((always_inline))
void copy_row_short(uint8_t *dp, const uint8_t *sp, int bytes, int lng)
{
    if (lng) {
        const uint32_t *s4 = (const uint32_t *)sp;
        uint32_t *d4 = (uint32_t *)dp;
        int n = bytes >> 2;
        do {
            *d4++ = *s4++;
        } while (--n != 0);
    } else {
        const uint16_t *s2 = (const uint16_t *)sp;
        uint16_t *d2 = (uint16_t *)dp;
        int n = bytes >> 1;
        do {
            *d2++ = *s2++;
        } while (--n != 0);
    }
}

/*
 * Rows of whole groups, long aligned, no mask: tiles and background
 * restores. Written out because it is the hottest small loop in the
 * library and gcc 4.6 has kept its strides and row count on the stack
 * in some builds of STDL_BlitSurfaceEx and in registers in others -
 * the same C measured 7% apart on a 16x16 restore depending on
 * unrelated code elsewhere in the function. Here they are registers
 * by construction. ng >= 1, h >= 1; the skips are the strides less
 * the row's own bytes.
 */
static __inline__ void copy_rows_groups(uint8_t *dp, const uint8_t *sp,
                                        int ng, int h, int32_t sskip,
                                        int32_t dskip)
{
#ifdef __m68k__
    int16_t rows = (int16_t)h;
    const int16_t n = (int16_t)(ng - 1);

    __asm__ volatile(
        "1:\n\t"
        "move.w %[n],%%d0\n"
        "2:\n\t"
        "move.l (%[s])+,(%[d])+\n\t"
        "move.l (%[s])+,(%[d])+\n\t"
        "dbf    %%d0,2b\n\t"
        "adda.l %[sk],%[s]\n\t"
        "adda.l %[dk],%[d]\n\t"
        "subq.w #1,%[h]\n\t"
        "bne.s  1b"
        : [d] "+a"(dp), [s] "+a"(sp), [h] "+d"(rows)
        : [n] "d"(n), [sk] "d"(sskip), [dk] "d"(dskip)
        : "d0", "cc", "memory");
#else
    /* C twin - what tests/host exercises and the asm must match */
    int y, i;

    for (y = 0; y < h; y++) {
        for (i = 0; i < ng * 8; i += 4) {
            *(uint32_t *)(dp + i) = *(const uint32_t *)(sp + i);
        }
        dp += ng * 8 + dskip;
        sp += ng * 8 + sskip;
    }
#endif
}

/* n mask words cleared, n >= 1; mask rows are word aligned always */
static __inline__ __attribute__((always_inline))
void clear_mask_short(uint16_t *m, int n)
{
    do {
        *m++ = 0;
    } while (--n != 0);
}

/* one group: dst = (dst & ~vis) | (src & vis), mask upkeep. np is
 * the plane budget and is a compile-time constant in every
 * instantiation, so the guarded plane writes vanish. */
STDL_PLANE_INLINE void copy_group(const uint16_t *sg, uint16_t *dg,
                                  uint16_t vis, uint16_t *dm, int g,
                                  const unsigned flags, const int np)
{
    if (dm != NULL && (flags & STDL_BLIT_UNDER) != 0) {
        vis &= (uint16_t)~dm[g];    /* marked pixels protect themselves */
    }
    if (vis == 0xFFFFu) {
        dg[0] = sg[0];
        if (np > 1) dg[1] = sg[1];
        if (np > 2) dg[2] = sg[2];
        if (np > 3) dg[3] = sg[3];
    } else if (vis != 0) {
        uint16_t keep = (uint16_t)~vis;
        dg[0] = (uint16_t)((dg[0] & keep) | (sg[0] & vis));
        if (np > 1) dg[1] = (uint16_t)((dg[1] & keep) | (sg[1] & vis));
        if (np > 2) dg[2] = (uint16_t)((dg[2] & keep) | (sg[2] & vis));
        if (np > 3) dg[3] = (uint16_t)((dg[3] & keep) | (sg[3] & vis));
    }
    if (dm != NULL && vis != 0) {
        if ((flags & STDL_BLIT_MARK) != 0) {
            dm[g] |= vis;          /* blitted pixels become foreground */
        } else if ((flags & STDL_BLIT_UNDER) == 0) {
            dm[g] &= (uint16_t)~vis;   /* blitted pixels become opaque */
        }
        /*
         * Under UNDER alone the clear is provably a no-op: vis has
         * already had the marked bits removed above, so
         * dm & ~(vis & ~dm) is dm. It stays for UNDER|MARK, where
         * the set above is emphatically not identity. A port whose
         * sprites all draw with UNDER was paying a load, a not, an
         * and and a store per group-row for nothing.
         */
    }
}

/*
 * Edge groups are peeled out of the row loop so the middle groups
 * run without per-group edge tests; the unmasked middle collapses
 * to a straight byte copy when the whole group is in budget, and to
 * a strided plane copy when it is not.
 */
STDL_PLANE_INLINE void blit_rows_aligned(const uint8_t *srow,
                              uint8_t *drow,
                              const uint8_t *smrow, uint8_t *dmrow,
                              int sstride, int dstride,
                              int smstride, int dmstride,
                              int ng, int h,
                              uint16_t lm, uint16_t rm, int masked,
                              int lng, const unsigned flags,
                              const int np)
{
    int y, g;
    /* the unmasked middle's size, for the inline-or-memcpy choice;
     * it starts a group into the row, so it shares the row's long
     * alignment */
    const int mbytes = (ng - 2) * 8;

    if (ng == 1) {
        lm &= rm;
    }
    for (y = 0; y < h; y++) {
        const uint16_t *sg = (const uint16_t *)srow;
        uint16_t *dg = (uint16_t *)drow;
        const uint16_t *sm = (const uint16_t *)smrow;
        uint16_t *dm = (uint16_t *)dmrow;

        copy_group(sg, dg,
                   masked ? (uint16_t)(lm & ~sm[0]) : lm, dm, 0,
                   flags, np);
        if (ng > 1) {
            if (masked) {
                for (g = 1; g < ng - 1; g++) {
                    copy_group(sg + g * 4, dg + g * 4,
                               (uint16_t)~sm[g], dm, g, flags, np);
                }
            } else if (ng > 2 && flags != 0) {
                for (g = 1; g < ng - 1; g++) {
                    copy_group(sg + g * 4, dg + g * 4, 0xFFFFu,
                               dm, g, flags, np);
                }
            } else if (ng > 2) {
                /*
                 * The middle of a row whose edges are partial groups
                 * - every restore at an unaligned x. It went through
                 * memcpy a row at a time, whose ~650-cycle prologue
                 * cost a 32x32 restore at x&15=5 two and a half times
                 * the aligned one on a plain ST; short middles are
                 * copied inline now, as whole-group rows already
                 * were.
                 */
                if (np == 4 && mbytes <= BLIT_INLINE_MAX) {
                    copy_row_short((uint8_t *)(dg + 4),
                                   (const uint8_t *)(sg + 4), mbytes, lng);
                } else if (np == 4) {
                    memcpy(dg + 4, sg + 4, (size_t)mbytes);
                } else {
                    const uint16_t *sp = sg + 4;
                    uint16_t *dp = dg + 4;
                    for (g = 1; g < ng - 1; g++) {
                        copy_group(sp, dp, 0xFFFFu, NULL, 0, flags, np);
                        sp += 4;
                        dp += 4;
                    }
                }
                if (dm != NULL) {
                    if (mbytes <= BLIT_INLINE_MAX) {
                        clear_mask_short(dm + 1, ng - 2);
                    } else {
                        memset(dm + 1, 0, (size_t)(ng - 2) * 2);
                    }
                }
            }
            copy_group(sg + (ng - 1) * 4, dg + (ng - 1) * 4,
                       masked ? (uint16_t)(rm & ~sm[ng - 1]) : rm,
                       dm, ng - 1, flags, np);
        }

        srow += sstride;
        drow += dstride;
        smrow += smstride;
        if (dmrow != NULL) {
            dmrow += dmstride;
        }
    }
}

/* --- shift chain ------------------------------------------------ */
/*
 * off = sx - dx is the source-pixel index that lands on destination
 * pixel 0. For destination group g the needed source window starts
 * at pixel dgbase + 16*g + off, i.e. source word sw with an in-word
 * offset r in 1..15 (r == 0 is the aligned case above). Each output
 * word is (s[sw] << r) | (s[sw+1] >> (16 - r)) per plane, via
 * walking pointers; the single mask word is carried across groups.
 * (Carrying the four plane words was tried and measured slower:
 * gcc 4.6 spills the array to the stack, trading RAM reads for
 * RAM reads plus copies.)
 */

/*
 * The shifted blit's inner merge: four plane words of one 16-pixel
 * group, each built from a 32-bit window across two source groups,
 * shifted into phase and merged through the visibility mask.
 *
 * This is the port-critical loop - game sprites land on arbitrary x,
 * so every sprite pixel comes through here. gcc 4.6 compiles the C
 * twin below to ~13 instructions per plane including a stack
 * round-trip per plane (it spills the merged word and reloads it to
 * OR in the kept bits). The hand-written version keeps everything in
 * registers: build the window with move.w/swap/move.w (no
 * clear-and-or), one variable long shift, mask, merge, store.
 */
static __inline__ void blit_merge4(uint16_t *dg, const uint16_t *sp,
                                   uint16_t vis, int rr)
{
#ifdef __m68k__
    uint16_t keep = (uint16_t)~vis;
    __asm__ volatile(
        "move.w (%1),%%d0\n\t"
        "swap   %%d0\n\t"
        "move.w 8(%1),%%d0\n\t"
        "lsr.l  %4,%%d0\n\t"
        "and.w  %2,%%d0\n\t"
        "move.w (%0),%%d1\n\t"
        "and.w  %3,%%d1\n\t"
        "or.w   %%d1,%%d0\n\t"
        "move.w %%d0,(%0)\n\t"

        "move.w 2(%1),%%d0\n\t"
        "swap   %%d0\n\t"
        "move.w 10(%1),%%d0\n\t"
        "lsr.l  %4,%%d0\n\t"
        "and.w  %2,%%d0\n\t"
        "move.w 2(%0),%%d1\n\t"
        "and.w  %3,%%d1\n\t"
        "or.w   %%d1,%%d0\n\t"
        "move.w %%d0,2(%0)\n\t"

        "move.w 4(%1),%%d0\n\t"
        "swap   %%d0\n\t"
        "move.w 12(%1),%%d0\n\t"
        "lsr.l  %4,%%d0\n\t"
        "and.w  %2,%%d0\n\t"
        "move.w 4(%0),%%d1\n\t"
        "and.w  %3,%%d1\n\t"
        "or.w   %%d1,%%d0\n\t"
        "move.w %%d0,4(%0)\n\t"

        "move.w 6(%1),%%d0\n\t"
        "swap   %%d0\n\t"
        "move.w 14(%1),%%d0\n\t"
        "lsr.l  %4,%%d0\n\t"
        "and.w  %2,%%d0\n\t"
        "move.w 6(%0),%%d1\n\t"
        "and.w  %3,%%d1\n\t"
        "or.w   %%d1,%%d0\n\t"
        "move.w %%d0,6(%0)"
        :
        : "a"(dg), "a"(sp), "d"(vis), "d"(keep), "d"(rr)
        : "d0", "d1", "memory", "cc");
#else
    /* C twin - what tests/host exercises and the asm must match */
    const uint16_t keep = (uint16_t)~vis;
    const uint16_t *s2 = sp + 4;
    int p;
    for (p = 0; p < 4; p++) {
        uint16_t v = (uint16_t)
            ((((uint32_t)sp[p] << 16) | s2[p]) >> rr);
        dg[p] = (uint16_t)((dg[p] & keep) | (v & vis));
    }
#endif
}

/*
 * An opaque group of the shifted blit - every middle group of an
 * unmasked blit, and the solid inside of a masked one: the same
 * windows and shifts as blit_merge4, stored without reading the
 * destination or masking, which is five instructions a plane fewer.
 * The CPU's version of the BLiTTER skipping the destination read
 * wherever its endmask is all ones.
 */
static __inline__ void blit_store4(uint16_t *dg, const uint16_t *sp,
                                   int rr)
{
#ifdef __m68k__
    __asm__ volatile(
        "move.w (%1),%%d0\n\t"
        "swap   %%d0\n\t"
        "move.w 8(%1),%%d0\n\t"
        "lsr.l  %2,%%d0\n\t"
        "move.w %%d0,(%0)\n\t"

        "move.w 2(%1),%%d0\n\t"
        "swap   %%d0\n\t"
        "move.w 10(%1),%%d0\n\t"
        "lsr.l  %2,%%d0\n\t"
        "move.w %%d0,2(%0)\n\t"

        "move.w 4(%1),%%d0\n\t"
        "swap   %%d0\n\t"
        "move.w 12(%1),%%d0\n\t"
        "lsr.l  %2,%%d0\n\t"
        "move.w %%d0,4(%0)\n\t"

        "move.w 6(%1),%%d0\n\t"
        "swap   %%d0\n\t"
        "move.w 14(%1),%%d0\n\t"
        "lsr.l  %2,%%d0\n\t"
        "move.w %%d0,6(%0)"
        :
        : "a"(dg), "a"(sp), "d"(rr)
        : "d0", "memory", "cc");
#else
    /* C twin - what tests/host exercises and the asm must match */
    const uint16_t *s2 = sp + 4;
    int p;
    for (p = 0; p < 4; p++) {
        dg[p] = (uint16_t)((((uint32_t)sp[p] << 16) | s2[p]) >> rr);
    }
#endif
}

STDL_PLANE_INLINE void blit_rows_shift(const uint8_t *srow,
                            uint8_t *drow,
                            const uint8_t *smrow, uint8_t *dmrow,
                            int sstride, int dstride,
                            int smstride, int dmstride,
                            int ng, int h,
                            uint16_t lm, uint16_t rm, int masked,
                            unsigned flags, int sw0, int r, const int np)
{
    int y, g, p;
    int rr = 16 - r;

    if (ng == 1) {
        lm &= rm;
    }
    for (y = 0; y < h; y++) {
        const uint16_t *sp = (const uint16_t *)(srow + sw0 * 8);
        const uint16_t *mp = masked
            ? (const uint16_t *)(smrow + sw0 * 2) : NULL;
        uint16_t *dg = (uint16_t *)drow;
        uint16_t *dm = (uint16_t *)dmrow;
        uint16_t bm = 0;

        if (masked) {
            bm = *mp++;
        }

        for (g = 0; g < ng; g++) {
            uint16_t vis = 0xFFFFu;

            if (g == 0) vis &= lm;
            if (g == ng - 1) vis &= rm;
            if (masked) {
                uint16_t am = bm;
                bm = *mp++;
                vis &= (uint16_t)~(uint16_t)
                    ((((uint32_t)am << 16) | bm) >> rr);
            }
            if (dm != NULL && (flags & STDL_BLIT_UNDER) != 0) {
                vis &= (uint16_t)~dm[g];
            }
            if (vis != 0) {
                if (np == 4) {
                    blit_merge4(dg, sp, vis, rr);
                } else {
                    uint16_t keep = (uint16_t)~vis;
                    const uint16_t *s2 = sp + 4;
                    for (p = 0; p < np; p++) {
                        uint16_t v = (uint16_t)
                            ((((uint32_t)sp[p] << 16) | s2[p]) >> rr);
                        dg[p] = (uint16_t)((dg[p] & keep) | (v & vis));
                    }
                }
                if (dm != NULL) {
                    /* the clear is identity under UNDER alone -
                     * see copy_group */
                    if ((flags & STDL_BLIT_MARK) != 0) {
                        dm[g] |= vis;
                    } else if ((flags & STDL_BLIT_UNDER) == 0) {
                        dm[g] &= (uint16_t)~vis;
                    }
                }
            }
            sp += 4;
            dg += 4;
        }
        srow += sstride;
        drow += dstride;
        smrow += smstride;
        if (dmrow != NULL) {
            dmrow += dmstride;
        }
    }
}

/*
 * The shift chain for the commonest case - no source mask, no
 * composition flags - with its edge groups peeled: only the first
 * and last groups of a row can be partial, so every group between
 * them is opaque and is stored outright (blit_store4), with no mask
 * window, no per-group edge tests and no read of the destination.
 * Keyed blits and flagged ones keep the general loop above, which
 * this leaves exactly as it was: testing for an opaque group there
 * cost keyed blits 2% for the few groups it caught.
 */
STDL_PLANE_INLINE void blit_rows_shift_plain(const uint8_t *srow,
                            uint8_t *drow, uint8_t *dmrow,
                            int sstride, int dstride, int dmstride,
                            int ng, int h, uint16_t lm, uint16_t rm,
                            int sw0, int r, const int np)
{
    int y, p;
    const int rr = 16 - r;
    const int mid = ng - 2;

    if (ng == 1) {
        lm &= rm;
    }
    for (y = 0; y < h; y++) {
        const uint16_t *sp = (const uint16_t *)(srow + sw0 * 8);
        uint16_t *dg = (uint16_t *)drow;
        uint16_t *dm = (uint16_t *)dmrow;
        int n;

        if (np == 4) {
            blit_merge4(dg, sp, lm, rr);
        } else {
            for (p = 0; p < np; p++) {
                uint16_t v = (uint16_t)
                    ((((uint32_t)sp[p] << 16) | sp[p + 4]) >> rr);
                dg[p] = (uint16_t)((dg[p] & (uint16_t)~lm) | (v & lm));
            }
        }
        if (dm != NULL) {
            dm[0] &= (uint16_t)~lm;
        }
        if (mid >= 0) {
            for (n = mid; n > 0; n--) {
                sp += 4;
                dg += 4;
                if (np == 4) {
                    blit_store4(dg, sp, rr);
                } else {
                    for (p = 0; p < np; p++) {
                        dg[p] = (uint16_t)
                            ((((uint32_t)sp[p] << 16) | sp[p + 4]) >> rr);
                    }
                }
            }
            sp += 4;
            dg += 4;
            if (np == 4) {
                blit_merge4(dg, sp, rm, rr);
            } else {
                for (p = 0; p < np; p++) {
                    uint16_t v = (uint16_t)
                        ((((uint32_t)sp[p] << 16) | sp[p + 4]) >> rr);
                    dg[p] = (uint16_t)((dg[p] & (uint16_t)~rm)
                                       | (v & rm));
                }
            }
            if (dm != NULL) {
                /* opaque blit: every drawn pixel's bit clears */
                if (mid > 0) {
                    clear_mask_short(dm + 1, mid);
                }
                dm[ng - 1] &= (uint16_t)~rm;
            }
        }
        srow += sstride;
        drow += dstride;
        if (dmrow != NULL) {
            dmrow += dmstride;
        }
    }
}

/* --------------------------------------------------------------- */

#ifdef STDL_BLIT_STATS
/*
 * Opt-in counters, compiled in only with -DSTDL_BLIT_STATS. They
 * exist because a port measured a frame-time step across a library
 * commit whose every individual change had been exonerated, and the
 * question that could not be answered from outside was whether the
 * number of blits had changed rather than their cost. Nothing here
 * is in a normal build: no counter, no branch, no bytes.
 *
 * Read them from the game, print them per frame, reset them when
 * you like. stdl_blit_ticks is 200Hz ticks accumulated around the
 * call, so it is 5ms-grained per blit and only means anything
 * summed over a frame or a run.
 */
unsigned long stdl_blit_calls;      /* entries                    */
unsigned long stdl_blit_blitter;    /* rows through the BLiTTER   */
unsigned long stdl_blit_inline;     /* short rows copied inline   */
unsigned long stdl_blit_memcpy;     /* rows through memcpy        */
unsigned long stdl_blit_merge;      /* same-phase rows merged per
                                     * group: masked, partial edge
                                     * groups, or composition flags */
unsigned long stdl_blit_shift;      /* the unaligned shift path   */
unsigned long stdl_blit_unaligned;  /* rows that wanted the inline
                                     * copy and could not have it */
unsigned long stdl_blit_rows;       /* rows, whichever path       */
unsigned long stdl_blit_ticks;      /* 200Hz ticks inside         */
int stdl_blit_force;                /* ignore the size thresholds */
#endif

/*
 * One surface onto another of the same size, whole - the background
 * restore - on a machine without a BLiTTER. STDL_BlitSurfaceEx does
 * it a memcpy call a row, and each call's set-up is as long as the
 * copy of a 320-wide row: 42ms for a full screen on a plain ST.
 * Rows with no padding make the surface one block, and one memcpy
 * of it takes 19. Caught here, before the general path, because a
 * long-row path inside STDL_BlitSurfaceEx made every small blit 1-2%
 * slower by changing how gcc allocates registers in that function
 * (measured with tests/hatari/blitcost.c; a padding-only control
 * moved nothing). Out of line, and only reached without a source
 * rectangle, so a blit that names one pays a single test; anything
 * else it passes on to the general path. With a BLiTTER the general
 * path is faster still (17ms on an STE), so it keeps the copy.
 */
static __attribute__((noinline)) int
blit_whole(STDL_Surface *src, STDL_Surface *dst, STDL_Rect *dstrect)
{
    if (src == NULL || dst == NULL
        || (dstrect != NULL && (dstrect->x != 0 || dstrect->y != 0))
        || src->w != dst->w || src->h != dst->h
        || (dst->w & 15) != 0
        || src->stride != (uint16_t)(dst->w >> 1)
        || dst->stride != (uint16_t)(dst->w >> 1)
        || src->pixels == NULL || dst->pixels == NULL
        || ((src->flags & STDL_SRCKEY) && src->mask != NULL)
        || dst->mask != NULL
        || (src->org_x | src->org_y | dst->org_x | dst->org_y) != 0
        || dst->clip.x != 0 || dst->clip.y != 0
        || dst->clip.w < dst->w || dst->clip.h < dst->h
        || stdl_planes != 4 || stdl_blitter_allowed()) {
        return STDL_BlitSurfaceEx(src, NULL, dst, dstrect, 0);
    }
#ifdef STDL_BLIT_STATS
    stdl_blit_memcpy += (unsigned long)dst->h;
    stdl_blit_rows += (unsigned long)dst->h;
#endif
    memcpy(dst->pixels, src->pixels, (size_t)dst->stride * dst->h);
    if (dstrect != NULL) {
        dstrect->w = (uint16_t)dst->w;
        dstrect->h = (uint16_t)dst->h;
    }
    return 0;
}

int STDL_BlitSurface(STDL_Surface *src, const STDL_Rect *srcrect,
                     STDL_Surface *dst, STDL_Rect *dstrect)
{
#ifdef STDL_BLIT_STATS
    uint32_t t0 = STDL_HZ200;
    int r;

    stdl_blit_calls++;
    r = srcrect == NULL ? blit_whole(src, dst, dstrect)
        : STDL_BlitSurfaceEx(src, srcrect, dst, dstrect, 0);
    stdl_blit_ticks += STDL_HZ200 - t0;
    return r;
#else
    if (srcrect == NULL) {
        return blit_whole(src, dst, dstrect);
    }
    return STDL_BlitSurfaceEx(src, srcrect, dst, dstrect, 0);
#endif
}

int STDL_BlitSurfaceEx(STDL_Surface *src, const STDL_Rect *srcrect,
                       STDL_Surface *dst, STDL_Rect *dstrect,
                       unsigned flags)
{
    int sx, sy, w, h, dx, dy;
    int cx1, cy1, cx2, cy2;
    int masked, np = stdl_planes;

    if (src == NULL || dst == NULL || src->pixels == NULL
        || dst->pixels == NULL) {
        STDL_SetError("null surface in blit");
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

    /* translate logical coordinates into storage space */
    sx -= src->org_x;
    sy -= src->org_y;
    dx -= dst->org_x;
    dy -= dst->org_y;

    /* clip source rectangle against the source surface */
    if (sx < 0) { w += sx; dx -= sx; sx = 0; }
    if (sy < 0) { h += sy; dy -= sy; sy = 0; }
    if (sx + w > src->w) w = src->w - sx;
    if (sy + h > src->h) h = src->h - sy;

    /* clip destination against the destination clip rect */
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
    /* SDL 1.2 semantics: store the final clipped rectangle back,
     * in the destination's logical coordinates */
    if (dstrect != NULL) {
        dstrect->x = (int16_t)(dx + dst->org_x);
        dstrect->y = (int16_t)(dy + dst->org_y);
        dstrect->w = (uint16_t)w;
        dstrect->h = (uint16_t)h;
    }

    masked = (src->flags & STDL_SRCKEY) && src->mask != NULL;
    /*
     * With no destination mask there is nothing for UNDER or MARK
     * to read or set, so they are no-ops - but the fast routes are
     * gated on flags being zero, so leaving them set costs a caller
     * that passes them scene-wide both the BLiTTER and the whole-row
     * copy on every blit to an unmasked surface. blit8.c already
     * strips its own pair for the same reason.
     */
    if (dst->mask == NULL) {
        flags &= ~(unsigned)(STDL_BLIT_UNDER | STDL_BLIT_MARK);
    }
    /*
     * The BLiTTER, when the machine has one. Both the choice
     * (stdl_blitter_wants, the size rules) and the work
     * (stdl_blitter_blit) are out of line in blitter.c, so what this
     * function holds for them is two byte tests and two calls: with
     * the rules written inline here, a plain ST - which fails the
     * byte test and never reaches them - ran its unaligned CPU blits
     * 1-2% slower, through what they did to this function's register
     * allocation. The passes fix the mask upkeep themselves; UNDER
     * and MARK go the CPU route.
     */
    if (flags == 0 && stdl_blitter_active()
        && (STDL_BLIT_FORCED() || stdl_blitter_wants(sx, dx, w, h, masked))
        && STDL_BLIT_REACHES(src, dst)) {
#ifdef STDL_BLIT_STATS
        stdl_blit_blitter += (unsigned long)h;
        stdl_blit_rows += (unsigned long)h;
#endif
        stdl_blitter_blit(src, dst, sx, sy, dx, dy, w, h, masked);
        if (dst->mask != NULL) {
            dst->opaque_state = 0;
        }
        return 0;
    }

    {
        int sphase = sx & 15;
        int dphase = dx & 15;
        int sg0 = sx >> 4;
        int dg0 = dx >> 4;
        int ng = ((dphase + w + 15) >> 4);
        uint16_t lm = (uint16_t)(0xFFFFu >> dphase);
        uint16_t rm =
            (uint16_t)(0xFFFFu << (15 - ((dphase + w - 1) & 15)));
        const uint8_t *srow =
            src->pixels + stdl_row_off(sy, src->stride);
        uint8_t *drow =
            dst->pixels + stdl_row_off(dy, dst->stride) + dg0 * 8;
        const uint8_t *smrow = masked
            ? src->mask + stdl_row_off(sy, src->maskstride)
            : NULL;
        uint8_t *dmrow = (dst->mask != NULL)
            ? dst->mask + stdl_row_off(dy, dst->maskstride) + dg0 * 2
            : NULL;
        int smstride = masked ? src->maskstride : 0;
        int dmstride = (dst->mask != NULL) ? dst->maskstride : 0;

        if (dst->mask != NULL) {
            dst->opaque_state = 0;
        }

        if (sphase == dphase) {
            /* fully aligned, unmasked, whole groups: straight rows,
             * but only while every plane of the group is in budget */
            if (!masked && lm == 0xFFFFu && rm == 0xFFFFu
                && np == 4 && flags == 0) {
                const uint8_t *sp = srow + sg0 * 8;
                uint8_t *dp = drow;
                int bytes = ng * 8;
                int y;
                /*
                 * A tile blit is one group - eight bytes a row - and
                 * a libc memcpy call for eight bytes is very nearly
                 * all prologue: measured 40us a row on a 16MHz Mega
                 * STE, about 650 cycles to move what a pair of
                 * move.l do in thirty. Short rows are copied inline
                 * instead, and the same goes for the two-byte mask
                 * clear beside it.
                 *
                 * The alignment test hoists: a stride is a whole
                 * number of groups, so if the first row is long
                 * aligned every row is. It has to be asked rather
                 * than assumed, because STDL_CreateSurfaceFrom
                 * promises word alignment and no more, and a long
                 * move to an odd word is an address error on a
                 * 68000.
                 */
                const int shortrow = bytes <= BLIT_INLINE_MAX;
                const int lng = (((uintptr_t)sp | (uintptr_t)dp) & 3) == 0;
#ifdef STDL_BLIT_STATS
                if (shortrow) {
                    stdl_blit_inline += (unsigned long)h;
                    if (!lng) {
                        /* short enough to copy inline but the rows
                         * are not long aligned, so it takes the
                         * word loop instead. A borrowed buffer from
                         * a generic allocator is misaligned half
                         * the time, and the only symptom is the
                         * clock - which cost a port a day. */
                        stdl_blit_unaligned += (unsigned long)h;
                    }
                } else {
                    stdl_blit_memcpy += (unsigned long)h;
                }
                stdl_blit_rows += (unsigned long)h;
#endif
                if (shortrow && lng && dmrow == NULL) {
                    /*
                     * Tiles and restores: short rows, long aligned,
                     * no mask to keep. The loop below asks all three
                     * of those again for every row (gcc 4.6 does not
                     * unswitch), which cost more than a 16-pixel
                     * row's two moves. Here they are asked once.
                     */
                    copy_rows_groups(dp, sp, ng, h,
                                     (int32_t)src->stride - bytes,
                                     (int32_t)dst->stride - bytes);
                    return 0;
                }
                for (y = 0; y < h; y++) {
                    if (shortrow) {
                        /* word aligned only - a borrowed block may be
                         * - still beats the call */
                        copy_row_short(dp, sp, bytes, lng);
                    } else {
                        memcpy(dp, sp, (size_t)bytes);
                    }
                    if (dmrow != NULL) {
                        /* mask rows are words and every other mask
                         * write in the library already assumes it,
                         * so there is nothing to test for here */
                        if (shortrow) {
                            clear_mask_short((uint16_t *)dmrow, ng);
                        } else {
                            memset(dmrow, 0, (size_t)(ng * 2));
                        }
                        dmrow += dmstride;
                    }
                    sp += src->stride;
                    dp += dst->stride;
                }
            } else {
#ifdef STDL_BLIT_STATS
                stdl_blit_merge += (unsigned long)h;
                stdl_blit_rows += (unsigned long)h;
#endif
                const int lng = ((((uintptr_t)(srow + sg0 * 8))
                                  | (uintptr_t)drow) & 3) == 0;
#define BLIT_ALIGNED_F(np, fl) \
                blit_rows_aligned(srow + sg0 * 8, drow, \
                                  masked ? smrow + sg0 * 2 : NULL, \
                                  dmrow, \
                                  src->stride, dst->stride, \
                                  smstride, dmstride, \
                                  ng, h, lm, rm, masked, lng, (fl), (np))
#define BLIT_ALIGNED_N2(fl) BLIT_ALIGNED_F(2, (fl))
#define BLIT_ALIGNED_N4(fl) BLIT_ALIGNED_F(4, (fl))
                if (np <= 2) {
                    STDL_FLAG_DISPATCH(flags, BLIT_ALIGNED_N2);
                } else {
                    STDL_FLAG_DISPATCH(flags, BLIT_ALIGNED_N4);
                }
#undef BLIT_ALIGNED_F
#undef BLIT_ALIGNED_N2
#undef BLIT_ALIGNED_N4
            }
        } else {
            /*
             * Shift chain. Source pixel landing on destination
             * group base: sp0 = (dg0*16) + off with off = sx - dx.
             * Split into word index and residue r in 1..15.
             */
            int off = sx - dx;
            int sp0 = dg0 * 16 + off;
            int sw0, r;

            /* floor division / positive modulo for negative sp0 */
            sw0 = sp0 >> 4;
            r = sp0 & 15;
            if (sp0 < 0) {
                sw0 = -((-sp0 + 15) >> 4);
                r = sp0 - sw0 * 16;
            }
#ifdef STDL_BLIT_STATS
            stdl_blit_shift += (unsigned long)h;
            stdl_blit_rows += (unsigned long)h;
#endif
#define BLIT_SHIFT_F(np, fl) \
            blit_rows_shift(srow, drow, smrow, dmrow, \
                            src->stride, dst->stride, \
                            smstride, dmstride, \
                            ng, h, lm, rm, masked, (fl), sw0, r, (np))
#define BLIT_SHIFT_N2(fl) BLIT_SHIFT_F(2, (fl))
#define BLIT_SHIFT_N4(fl) BLIT_SHIFT_F(4, (fl))
#define BLIT_SHIFT_PLAIN(np) \
            blit_rows_shift_plain(srow, drow, dmrow, src->stride, \
                                  dst->stride, dmstride, ng, h, lm, rm, \
                                  sw0, r, (np))
            if (!masked && flags == 0) {
                STDL_PLANE_DISPATCH(np, BLIT_SHIFT_PLAIN);
            } else if (np <= 2) {
                STDL_FLAG_DISPATCH(flags, BLIT_SHIFT_N2);
            } else {
                STDL_FLAG_DISPATCH(flags, BLIT_SHIFT_N4);
            }
#undef BLIT_SHIFT_PLAIN
#undef BLIT_SHIFT_F
#undef BLIT_SHIFT_N2
#undef BLIT_SHIFT_N4
        }
    }
    return 0;
}
