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
 * bus is ours. Availability comes from Blitmode() at STDL_Init;
 * STDL_UseBlitter() lets benchmarks and debugging force the CPU
 * paths at runtime.
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

    b->src_xinc = sxinc;
    b->src_yinc = syinc;
    b->src_addr = src;
    b->endmask1 = (nwords == 1) ? (uint16_t)(em1 & em3) : em1;
    b->endmask2 = 0xFFFF;
    b->endmask3 = em3;
    b->dst_xinc = dxinc;
    b->dst_yinc = dyinc;
    b->dst_addr = dst;
    b->xcount = nwords;
    b->ycount = nlines;
    b->hop = hop;
    b->op = op;
    b->skew = 0;
    /* Hog mode stalls the CPU for the whole operation, which is
     * fine until something needs an interrupt serviced on time: the
     * border overscan trick must take Timer A/B inside a scanline
     * window, and a multi-millisecond hog blit across it drops the
     * border for that frame. While a border is open the library
     * starts blits in shared mode instead - the bus alternates
     * 64 cycles blitter / 64 CPU, the operation takes about twice
     * the wall time, and interrupt latency stays in the
     * microseconds. */
    b->ctrl = stdl_no_hog ? 0x80 : 0xC0;    /* start */
    while (b->ctrl & 0x80)
        ;
}
