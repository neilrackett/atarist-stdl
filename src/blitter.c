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
 * paths' edge masks map directly onto endmask1/endmask3. Only
 * same-phase operations are accelerated - unaligned blits stay on
 * the CPU shift chain, and pre-shifted sprites are the designed
 * answer for free positioning.
 *
 * Runs in hog mode with a busy-wait: operations are short and the
 * bus is ours. While a border is open, overscan.c installs a policy
 * that is asked before each operation, with an estimate of its bus
 * time; see stdl_blit_policy. Availability comes from Blitmode() at
 * STDL_Init; STDL_UseBlitter() lets benchmarks and debugging force
 * the CPU paths at runtime.
 */

#include "stdl_internal.h"

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

static int user_enable = 1;

/* 16x16 -> 32 signed multiply on the 68000's own instruction:
 * promoted to int, gcc 4.6 calls __mulsi3 for it */
static __inline__ int32_t blit_muls(int16_t a, int16_t b)
{
    int32_t r = a;

    __asm__("muls.w %1,%0" : "+d"(r) : "d"(b));
    return r;
}

int stdl_blitter_active(void)
{
    return user_enable && stdl.mach.has_blitter;
}

int STDL_UseBlitter(int enable)
{
    int old = user_enable;

    if (enable >= 0) {
        user_enable = (enable != 0);
    }
    return old;
}

/*
 * One plane-rectangle operation, then wait for completion.
 * endmask1 masks the first word of each line, endmask3 the last
 * (merged when the line is a single word). hop: 0 = all ones,
 * 2 = source. op: 0 zeros, 1 src AND dst, 3 src, 6 src XOR dst.
 */
void stdl_blitter_go(uintptr_t src, int16_t sxinc, int16_t syinc,
                     uintptr_t dst, int16_t dxinc, int16_t dyinc,
                     uint16_t em1, uint16_t em3,
                     uint16_t nwords, uint16_t nlines,
                     uint8_t hop, uint8_t op)
{
    volatile blitregs_t *b = BLIT;
    /* bus cycles per line: ~4.5 a word for a fill, ~9 for a copy,
     * plus a little per line (measured on an STE, and the same on
     * a Mega STE - the blitter runs on the 8MHz bus whatever the
     * CPU does) */
    const uint32_t cpl = (uint32_t)nwords
                       * (uint16_t)(hop == STDL_BLIT_HOP_ONES ? 5 : 10) + 16;

    b->src_xinc = sxinc;
    b->src_yinc = syinc;
    b->endmask1 = (nwords == 1) ? (uint16_t)(em1 & em3) : em1;
    b->endmask2 = 0xFFFF;
    b->endmask3 = em3;
    b->dst_xinc = dxinc;
    b->dst_yinc = dyinc;
    b->hop = hop;
    b->op = op;
    b->skew = 0;
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
        while ((b->ctrl & 0x80) || b->ycount != 0)
            ;
        nlines = (uint16_t)(nlines - n);
        if (nlines != 0) {
            /* advance to the first unblitted line: n lines of
             * (nwords-1) x steps and one y step each. 16-bit
             * multiplies - the 32-bit kind is a library call */
            const int16_t sl = (int16_t)(blit_muls((int16_t)(nwords - 1),
                                                   sxinc) + syinc);
            const int16_t dl = (int16_t)(blit_muls((int16_t)(nwords - 1),
                                                   dxinc) + dyinc);
            src = (uintptr_t)((int32_t)src + blit_muls((int16_t)n, sl));
            dst = (uintptr_t)((int32_t)dst + blit_muls((int16_t)n, dl));
        }
    }
}
