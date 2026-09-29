/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * BLiTTER driver (STE / Mega STE / Mega ST).
 *
 * The BLiTTER moves one bitplane rectangle per operation. STDL's
 * word-interleaved layout walks a plane with an x increment of 8
 * bytes (2 for the separate mask, whose words cover the same 16
 * pixels); spans are word-aligned by construction, so the CPU
 * paths' edge masks map directly onto endmask1/endmask3. Unaligned
 * blits use the chip's barrel shifter (SKEW, with FXSR for left
 * shifts, never NFSR); see stdl_blitter_blit.
 *
 * Runs in hog mode with a busy-wait: operations are short and the
 * bus is ours. While a border is open, overscan.c installs a policy
 * that is asked before each operation, with an estimate of its bus
 * time; see stdl_blit_policy. Availability comes from Blitmode() at
 * STDL_Init; STDL_UseBlitter() lets benchmarks and debugging force
 * the CPU paths at runtime.
 */

#include "stdl_internal.h"

#ifdef __m68k__
typedef struct {
    uint16_t halftone[16];
    int16_t  src_xinc;
    int16_t  src_yinc;
    uint32_t src_addr;
    uint16_t endmask1;
    uint16_t endmask2;
    uint16_t endmask3;
    int16_t  dst_xinc;
    int16_t  dst_yinc;
    uint32_t dst_addr;
    uint16_t xcount;
    uint16_t ycount;
    uint8_t  hop;
    uint8_t  op;
    volatile uint8_t ctrl;
    uint8_t  skew;
} blitregs_t;

#define BLIT ((volatile blitregs_t *)0xFFFF8A00UL)
/* the chip runs by itself once started */
#define BLIT_STARTED() ((void)0)
#else
/* host: the registers are a struct, and starting an operation runs
 * the software model to completion (see stdl_internal.h) */
typedef stdl_host_blitregs_t blitregs_t;
#define BLIT (&stdl_host_blit)
#define BLIT_STARTED() stdl_host_blitter_exec()
#endif

/* STDL_UseBlitter's setting; stdl_blitter_active() reads it inline */
uint8_t stdl_blit_user = 1;

/*
 * One pass when no border policy is installed - the usual case -
 * written inline where a caller issues several in a row. With a
 * policy the pass may have to be split around a border window, and
 * that is stdl_blitter_run's job.
 */
static __inline__ __attribute__((always_inline))
void blit_pass(uintptr_t src, uintptr_t dst, uint16_t nwords,
               uint16_t nlines, uint8_t hop)
{
    volatile blitregs_t *b = BLIT;

    if (stdl_blit_policy != NULL) {
        stdl_blitter_run(src, dst, nwords, nlines, hop);
        return;
    }
    b->src_addr = src;
    b->dst_addr = dst;
    b->xcount = nwords;
    b->ycount = nlines;
    b->ctrl = 0xC0;                     /* start, hog */
    BLIT_STARTED();
    while ((b->ctrl & 0x80) || b->ycount != 0)
        ;
}

/*
 * Whether a clipped blit is worth the BLiTTER, by its shape (the
 * constants and how they were fitted are in stdl_internal.h). Same
 * phase: whole groups copy fast on the CPU, inline up to
 * BLIT_INLINE_MAX bytes a row and through memcpy beyond, and the
 * BLiTTER wins once h * (ROW + CELL * ng) passes its set-up - with
 * cells = ng * h that is ROW * h + CELL * cells, one multiply; a
 * partial edge group costs the CPU a merge a row and the BLiTTER wins
 * sooner; a keyed copy is a cell count. Different phases: the CPU's
 * shift chain is dear enough that the BLiTTER wins from a handful of
 * cells.
 */
int stdl_blitter_wants(int sx, int dx, int w, int h, int masked)
{
    const int bph = dx & 15;
    const int bng = (bph + w + 15) >> 4;
    const uint32_t cells = stdl_row_off(bng, (uint16_t)h);

    if ((sx & 15) != bph) {
        return cells >= (masked ? STDL_BLIT_SHIFT_KEYED_MIN_CELLS
                                : STDL_BLIT_SHIFT_MIN_CELLS);
    }
    if (masked) {
        return cells >= STDL_BLIT_MASKED_MIN_CELLS;
    }
    if ((bph | ((bph + w) & 15)) != 0) {
        return cells >= STDL_BLIT_EDGE_MIN_CELLS;
    }
    if (bng * 8 <= BLIT_INLINE_MAX) {
        return STDL_BLIT_CPU_ROW * (uint32_t)h
             + STDL_BLIT_CPU_CELL * cells > STDL_BLIT_SETUP;
    }
    return STDL_BLIT_MEM_ROW * (uint32_t)h
         + STDL_BLIT_MEM_CELL * cells > STDL_BLIT_SETUP;
}

/* the same test out of line, for blit.c: see stdl_internal.h */
int stdl_blitter_allowed(void)
{
    return stdl_blitter_active();
}

/* 16x16 -> 32 signed multiply on the 68000's own instruction:
 * promoted to int, gcc 4.6 calls __mulsi3 for it */
static __inline__ int32_t blit_muls(int16_t a, int16_t b)
{
#ifdef __m68k__
    int32_t r = a;

    __asm__("muls.w %1,%0" : "+d"(r) : "d"(b));
    return r;
#else
    return (int32_t)a * b;
#endif
}

int STDL_UseBlitter(int enable)
{
    int old = stdl_blit_user;

    if (enable >= 0) {
        stdl_blit_user = (uint8_t)(enable != 0);
    }
    return old;
}

/*
 * One plane-rectangle operation, then wait for completion.
 * endmask1 masks the first word of each line, endmask3 the last
 * (merged when the line is a single word). hop: 0 = all ones,
 * 2 = source. op: 0 zeros, 1 src AND dst, 3 src, 6 src XOR dst.
 */
/*
 * Everything the BLiTTER needs that does not change between the
 * planes of one operation. A four-plane copy used to write these
 * eleven registers four times over, and a masked one twelve: at
 * I/O speed that is 400-500 cycles of pure repetition against a
 * fitted setup cost of 860, so half the preamble was the same
 * numbers going back into the same registers. Callers that issue
 * one plane still use stdl_blitter_go, which is this plus a run.
 */
/*
 * The increments and fetch count, for the split path below: the
 * BLiTTER is one global device and these mirror registers it already
 * holds. How far a line moves each address - every fetch and write
 * adds the x increment but the last of a line, which adds the y
 * increment, and under FXSR a line fetches one word more than it
 * writes - is worked out there, when a split needs it: two
 * multiplies in every setup cost every BLiTTER operation about 1%.
 */
static int16_t bl_sxinc, bl_syinc, bl_dxinc, bl_dyinc;
static uint8_t bl_fxsr;

void stdl_blitter_setup(int16_t sxinc, int16_t syinc,
                        int16_t dxinc, int16_t dyinc,
                        uint16_t em1, uint16_t em3, uint16_t nwords,
                        uint8_t hop, uint8_t op, uint8_t skew)
{
    volatile blitregs_t *b = BLIT;

    bl_sxinc = sxinc;
    bl_syinc = syinc;
    bl_dxinc = dxinc;
    bl_dyinc = dyinc;
    bl_fxsr = (uint8_t)(skew >> 7);

    b->src_xinc = sxinc;
    b->src_yinc = syinc;
    b->endmask1 = (nwords == 1) ? (uint16_t)(em1 & em3) : em1;
    b->endmask2 = 0xFFFF;
    b->endmask3 = em3;
    b->dst_xinc = dxinc;
    b->dst_yinc = dyinc;
    b->hop = hop;
    b->op = op;
    b->skew = skew;
}

void stdl_blitter_go(uintptr_t src, int16_t sxinc, int16_t syinc,
                     uintptr_t dst, int16_t dxinc, int16_t dyinc,
                     uint16_t em1, uint16_t em3,
                     uint16_t nwords, uint16_t nlines,
                     uint8_t hop, uint8_t op, uint8_t skew)
{
    stdl_blitter_setup(sxinc, syinc, dxinc, dyinc, em1, em3, nwords,
                       hop, op, skew);
    stdl_blitter_run(src, dst, nwords, nlines, hop);
}

void stdl_blitter_run(uintptr_t src, uintptr_t dst, uint16_t nwords,
                      uint16_t nlines, uint8_t hop)
{
    volatile blitregs_t *b = BLIT;
    /* bus cycles per line: ~4.5 a word for a fill, ~9 for a copy,
     * plus a little per line (measured on an STE, and the same on
     * a Mega STE - the blitter runs on the 8MHz bus whatever the
     * CPU does) */
    const uint32_t cpl = (hop == STDL_BLIT_HOP_ONES)
                       ? (uint32_t)nwords * 5u + 16 : (uint32_t)nwords * 10u + 16;

    /* Hog mode stalls the CPU for the whole operation, which is
     * fine until something needs an interrupt serviced on time: the
     * border overscan trick must take Timer A/B inside a scanline
     * window, and a multi-millisecond hog blit across it drops the
     * border for that frame. Shared mode (64 bus accesses each in
     * turn) lets the interrupt in, at about twice the wall time and
     * still with the blitter's slices stretching the ISR's own
     * timing. So while a border is open the policy is asked, per
     * operation, how many lines may run now in hog mode from where
     * the beam is: the operation is split around an ISR window,
     * the part before it runs in hog, the policy waits the few
     * lines the window lasts, and the rest runs in hog after it.
     * Shared mode is the fallback for what cannot be placed, and
     * the ISRs pause a shared blit across their flick. Do NOT
     * "help" a shared blit by re-setting the busy bit inside the
     * poll loop: that read-modify-write races the blitter's own
     * control state and re-arms a finished blit with a spent line
     * count. It measures faster and passes BLITCHK, and it turns
     * palette fades into wrong colours on screen.
     *
     * The poll waits on the line count as well as busy: a paused
     * blit still reads busy, but the count is what says the
     * transfer is done. */
    while (nlines != 0) {
        uint16_t n = nlines;
        uint8_t ctrl = 0xC0;                /* start, hog */

        if (stdl_blit_policy != NULL) {
            n = stdl_blit_policy(nlines, cpl);
            if (n == 0) {
                n = nlines;
                ctrl = 0x80;                /* start, shared */
            } else if (n > nlines) {
                n = nlines;
            }
        }
        b->src_addr = src;
        b->dst_addr = dst;
        b->xcount = nwords;
        b->ycount = n;
        b->ctrl = ctrl;
        BLIT_STARTED();
        while ((b->ctrl & 0x80) || b->ycount != 0)
            ;
        nlines = (uint16_t)(nlines - n);
        if (nlines != 0) {
            /* advance to the first unblitted line; 16-bit multiplies,
             * the 32-bit kind being a library call */
            const int16_t sl = (int16_t)(blit_muls(
                (int16_t)(nwords - 1 + bl_fxsr), bl_sxinc) + bl_syinc);
            const int16_t dl = (int16_t)(blit_muls(
                (int16_t)(nwords - 1), bl_dxinc) + bl_dyinc);
            src = (uintptr_t)((intptr_t)src + blit_muls((int16_t)n, sl));
            dst = (uintptr_t)((intptr_t)dst + blit_muls((int16_t)n, dl));
        }
    }
}

/*
 * The BLiTTER path, one plane rectangle per pass, at any pair of
 * phases, using its barrel shifter (SKEW) where they differ.
 *
 * A right shift (source phase below destination) fetches the words
 * it needs as it goes; a left shift primes the source buffer with one
 * extra fetch at the start of each line (FXSR). NFSR is never set (see
 * stdl_blitter_setup), so a line can read one source word past the
 * span it copies - within the next group of the row, or the guard
 * bytes every surface carries past its last row. The endmasks are the
 * destination's, as in the CPU paths, and the source's y increment
 * accounts for the extra fetch, so each line starts one stride on.
 * Masked blits are the same-phase path's XOR-AND-XOR with the mask
 * pass shifted by the same skew.
 *
 * At equal phases all of this reduces to the registers the same-phase
 * path always wrote: skew 0, no extra fetch.
 *
 * Here rather than in blit.c so that each plane's pass is written
 * inline (blit_pass): as a call to stdl_blitter_run, every pass saved
 * and restored eight registers, four passes to a copy and thirteen
 * to a keyed blit. And out of STDL_BlitSurfaceEx, whose CPU paths
 * are compiled the same whatever the BLiTTER code does.
 */
void stdl_blitter_blit(const STDL_Surface *src, STDL_Surface *dst,
                       int sx, int sy, int dx, int dy, int w, int h,
                       int masked)
{
    const int np = stdl_planes;
    const int sph = sx & 15, dph = dx & 15;
    const int dn = (dph + w + 15) >> 4;         /* destination words  */
    const int fxsr = sph > dph;
    const int reads = dn + fxsr;                /* source words/line  */
    const uint8_t skew = (uint8_t)((fxsr << 7) | ((dph - sph) & 15));
    const uint16_t lm = (uint16_t)(0xFFFFu >> dph);
    const uint16_t rm = (uint16_t)(0xFFFFu << (15 - ((dph + w - 1) & 15)));
    const uintptr_t sbase = (uintptr_t)(src->pixels
        + stdl_row_off(sy, src->stride) + (sx >> 4) * 8);
    const uintptr_t dbase = (uintptr_t)(dst->pixels
        + stdl_row_off(dy, dst->stride) + (dx >> 4) * 8);
    const int16_t s_yinc = (int16_t)(src->stride - (reads - 1) * 8);
    const int16_t d_yinc = (int16_t)(dst->stride - (dn - 1) * 8);
    uintptr_t smbase = 0;
    int16_t sm_yinc = 0;
    int p;

    if (masked) {
        smbase = (uintptr_t)(src->mask
            + stdl_row_off(sy, src->maskstride) + (sx >> 4) * 2);
        sm_yinc = (int16_t)(src->maskstride - (reads - 1) * 2);
        stdl_blitter_setup(8, s_yinc, 8, d_yinc, lm, rm, (uint16_t)dn,
                           STDL_BLIT_HOP_SRC, STDL_BLIT_OP_XOR, skew);
        for (p = 0; p < np; p++) {
            blit_pass(sbase + (uintptr_t)(p * 2),
                             dbase + (uintptr_t)(p * 2),
                             (uint16_t)dn, (uint16_t)h,
                             STDL_BLIT_HOP_SRC);
        }
        stdl_blitter_setup(2, sm_yinc, 8, d_yinc, lm, rm, (uint16_t)dn,
                           STDL_BLIT_HOP_SRC, STDL_BLIT_OP_AND, skew);
        for (p = 0; p < np; p++) {
            blit_pass(smbase, dbase + (uintptr_t)(p * 2),
                             (uint16_t)dn, (uint16_t)h,
                             STDL_BLIT_HOP_SRC);
        }
    }
    /* the copy, or the masked blit's second XOR: same registers */
    stdl_blitter_setup(8, s_yinc, 8, d_yinc, lm, rm, (uint16_t)dn,
                       STDL_BLIT_HOP_SRC,
                       masked ? STDL_BLIT_OP_XOR : STDL_BLIT_OP_SRC, skew);
    for (p = 0; p < np; p++) {
        blit_pass(sbase + (uintptr_t)(p * 2),
                         dbase + (uintptr_t)(p * 2),
                         (uint16_t)dn, (uint16_t)h, STDL_BLIT_HOP_SRC);
    }
    if (dst->mask != NULL) {
        const uintptr_t dmbase = (uintptr_t)(dst->mask
            + stdl_row_off(dy, dst->maskstride) + (dx >> 4) * 2);
        const int16_t dm_yinc =
            (int16_t)(dst->maskstride - (dn - 1) * 2);

        if (masked) {
            /* dstmask &= srcmask inside the span */
            stdl_blitter_go(smbase, 2, sm_yinc, dmbase, 2, dm_yinc,
                            lm, rm, (uint16_t)dn, (uint16_t)h,
                            STDL_BLIT_HOP_SRC, STDL_BLIT_OP_AND, skew);
        } else {
            stdl_blitter_go(0, 0, 0, dmbase, 2, dm_yinc,
                            lm, rm, (uint16_t)dn, (uint16_t)h,
                            STDL_BLIT_HOP_ONES, STDL_BLIT_OP_ZERO, 0);
        }
    }
}
