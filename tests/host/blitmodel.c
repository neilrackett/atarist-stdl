/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * A software BLiTTER for the host tests.
 *
 * blitter.c drives a register struct on the host (see
 * stdl_internal.h) and calls stdl_host_blitter_exec() the moment it
 * sets BUSY; this runs the whole operation, word by word, against
 * real host pointers - so AddressSanitizer sees every word the chip
 * would read or write, including the ones past the rectangle that a
 * skewed source fetches.
 *
 * Written from the chip's documented behaviour (the Atari BLiTTER
 * reference and ST hardware notes), modelling only what STDL uses:
 *
 *  - HOP 0 (all ones) or 2 (source); halftone RAM, SMUDGE and HOP
 *    1/3 are never used and assert.
 *  - The 16 logic operations.
 *  - Endmask 1 on a line's first word, 3 on its last, 2 between;
 *    a one-word line takes endmask 1 alone.
 *  - A 32-bit source buffer: each fetch shifts the previous word to
 *    the high half and places the new one below it (left-to-right
 *    travel, the only direction STDL uses - a negative source x
 *    increment asserts), and the word used is the buffer shifted
 *    right by SKEW.
 *  - FXSR: one extra fetch at the start of each line, to prime the
 *    buffer.
 *  - Address stepping: every fetch and every destination write adds
 *    the x increment, except the last one of a line, which adds the
 *    y increment instead. The priming fetch adds the x increment.
 *
 * NFSR asserts: its behaviour on the real chip does not match the
 * commonly quoted description, and with one-word lines it gives
 * wrong results on hardware; the library never sets it (AGENTS.md).
 */

#include <assert.h>
#include "stdl_internal.h"

volatile stdl_host_blitregs_t stdl_host_blit;

/* operations executed, for tests that need to know the chip ran */
unsigned long stdl_host_blit_ops;

static uint16_t logic(uint8_t op, uint16_t s, uint16_t d)
{
    switch (op & 15) {
    case 0:  return 0;
    case 1:  return (uint16_t)(s & d);
    case 2:  return (uint16_t)(s & ~d);
    case 3:  return s;
    case 4:  return (uint16_t)(~s & d);
    case 5:  return d;
    case 6:  return (uint16_t)(s ^ d);
    case 7:  return (uint16_t)(s | d);
    case 8:  return (uint16_t)(~s & ~d);
    case 9:  return (uint16_t)(~s ^ d);
    case 10: return (uint16_t)~d;
    case 11: return (uint16_t)(s | ~d);
    case 12: return (uint16_t)~s;
    case 13: return (uint16_t)(~s | d);
    case 14: return (uint16_t)(~s | ~d);
    default: return 0xFFFFu;
    }
}

void stdl_host_blitter_exec(void)
{
    volatile stdl_host_blitregs_t *b = &stdl_host_blit;
    const int fxsr = (b->skew & 0x80) != 0;
    const int skew = b->skew & 15;
    const int use_src = (b->hop == 2);
    uintptr_t src = b->src_addr, dst = b->dst_addr;
    uint16_t xc = b->xcount, yc = b->ycount;

    if ((b->ctrl & 0x80) == 0) {
        return;                         /* not started */
    }
    assert((b->ctrl & 0x20) == 0);      /* SMUDGE: unused */
    assert((b->skew & 0x40) == 0);      /* NFSR: never set */
    assert(b->hop == 0 || b->hop == 2); /* no halftone */
    assert(xc != 0 && yc != 0);         /* 0 means 65536: unused */
    assert(!use_src || b->src_xinc >= 0);
    assert((src & 1) == 0 && (dst & 1) == 0);

    while (yc != 0) {
        uint32_t buf = 0;
        unsigned x;

        if (use_src && fxsr) {
            buf = *(const uint16_t *)src;
            src += (intptr_t)b->src_xinc;
        }
        for (x = 0; x < xc; x++) {
            const int last = (x == (unsigned)xc - 1);
            uint16_t s = 0xFFFFu, d, m, r;

            if (use_src) {
                buf = (buf << 16) | *(const uint16_t *)src;
                src += (intptr_t)(last ? b->src_yinc : b->src_xinc);
                s = (uint16_t)(buf >> skew);
            }
            m = (x == 0) ? b->endmask1
              : last ? b->endmask3 : b->endmask2;
            d = *(uint16_t *)dst;
            r = logic(b->op, s, d);
            *(uint16_t *)dst = (uint16_t)((r & m) | (d & (uint16_t)~m));
            dst += (intptr_t)(last ? b->dst_yinc : b->dst_xinc);
        }
        yc--;
    }
    b->src_addr = src;
    b->dst_addr = dst;
    b->ycount = 0;
    b->ctrl = (uint8_t)(b->ctrl & 0x7F);
    stdl_host_blit_ops++;
}
