/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Rows of whole groups copied at movem speed, for every aligned blit
 * whose rows are longer than BLIT_INLINE_MAX on 4 planes (blit.c) and
 * for 8-plane rows of four groups and more (planes8row.c).
 *
 * A scroller's frame is one such copy: a 320x200 window out of a
 * wider buffer. STDL_BlitSurface did it with a memcpy call a row, and
 * each call's set-up was as long as the copy of a 160-byte row: 38.0ms
 * on a plain ST, against 19.5 for a movem.l loop of the program's own
 * and 19.3 for one memcpy of the whole page (SCRLCOST); 20.7 here.
 * 8-plane rows went through a C loop of long moves: 15.7ms on a TT and
 * 31.4 on a Falcon, against 8.1 and 16.2 for the same movem loop; 8.2
 * and 16.5 here.
 *
 * The copy goes as ten longs a movem.l (40 bytes), eight of them
 * unrolled with the stores at fixed offsets, entered by a jump so that
 * a row of n units runs the last n: a strip of up to 320 bytes, every
 * row of it, then the next strip, then the rows' last 0-32 bytes in a
 * pass of their own. The 68000 has fifteen registers, ten of which
 * the movem pair takes; the pointers, the two strides and the row
 * count leave none for a second entry or a unit count, hence passes.
 * Passes in that order are right for any source and destination that
 * do not overlap, and for an overlap where the destination lies at a
 * lower address than the source (left of it, or above) and the rows
 * are one pass (whole units, one strip), as a single forward copy is;
 * stdl_copy_rows_checked declines the rest, and the caller copies those
 * as it did before.
 *
 * Out of line and an object of its own: code added inside
 * STDL_BlitSurfaceEx has moved its other CPU paths 1-7% on a plain ST
 * (AGENTS.md), and planes8row.o shares it.
 */

#include "stdl_internal.h"

#define ROW_UNIT  40            /* bytes a movem.l of ten registers moves */
#define ROW_UNITS 8             /* units unrolled: strips of 320 bytes */

#ifdef __m68k__
/*
 * (uint8_t *dp, const uint8_t *sp, long bytes, long h, long sstride,
 * long dstride): bytes a multiple of 8, from 8; h from 1. The
 * arguments' own slots on the stack keep what lasts from strip to
 * strip (the bases, the units left), as every register is in the
 * loop.
 *
 * A row of a strip costs its units (184 cycles for 40 bytes on a
 * 68000, 4.6 a byte, as mintlib's memcpy within a block) and 48 more:
 * the jump in (14), the two strides (16), and the row count, which
 * shares d0 with the jump's offset (18 with its swaps, against 10 for
 * a dbf of its own). The rows' tails are moved a long at a time (20
 * cycles), 34 more a row: that pass has the movem's registers free, so
 * its jump's target has one of its own. Sharing d0 as the strips do,
 * 48 a row, a ring's window - two pieces and two tails a row - took
 * 27.2ms on a plain ST, against 26.4.
 */
#define RC_STR2(x) #x
#define RC_STR(x) RC_STR2(x)
#define RC_SYM(n) RC_STR(__USER_LABEL_PREFIX__) #n
/* the stores' offsets start at 40, not 0: gas makes 0(%a1) a plain
 * (%a1), two bytes shorter, and the jump counts 10 bytes a unit */
#define RC_UNIT(off) \
    "movem.l (%a0)+,%d1-%d7/%a2-%a4\n\t" \
    "movem.l %d1-%d7/%a2-%a4," #off "(%a1)\n\t"
#define RC_LONG "move.l  (%a0)+,(%a1)+\n\t"
__asm__(
    "\t.text\n\t.even\n\t.globl " RC_SYM(stdl_copy_rows) "\n"
    RC_SYM(stdl_copy_rows) ":\n\t"
    "movem.l %d2-%d7/%a2-%a6,-(%sp)\n\t"
    "move.l  56(%sp),%d0\n\t"
    "divu.w  #40,%d0\n\t"               /* units in d0.w, the tail above */
    "move.l  %d0,56(%sp)\n"
    "1:\n\t"                            /* a strip */
    "move.w  58(%sp),%d1\n\t"           /* units left */
    "beq     4f\n\t"
    "moveq   #8,%d2\n\t"
    "cmp.w   %d2,%d1\n\t"
    "bls.s   2f\n\t"
    "move.w  %d2,%d1\n"                 /* n: at most 8 units */
    "2:\n\t"
    "sub.w   %d1,58(%sp)\n\t"
    "sub.w   %d1,%d2\n\t"               /* k = 8 - n, the units skipped */
    "move.w  %d2,%d3\n\t"
    "lsl.w   #3,%d3\n\t"
    "move.w  %d3,%d0\n\t"
    "add.w   %d2,%d0\n\t"
    "add.w   %d2,%d0\n\t"               /* 10k: 10 bytes a unit's code */
    "swap    %d0\n\t"
    "move.w  62(%sp),%d0\n\t"
    "subq.w  #1,%d0\n\t"                /* rows - 1 */
    "move.w  %d3,%d2\n\t"
    "lsl.w   #2,%d3\n\t"
    "add.w   %d2,%d3\n\t"               /* 40k: 40 bytes a unit skipped */
    "move.l  48(%sp),%a1\n\t"
    "suba.w  %d3,%a1\n\t"
    "lea     -40(%a1),%a1\n\t"          /* the first unit run stores at 0 */
    "move.l  52(%sp),%a0\n\t"
    "move.w  %d1,%d2\n\t"
    "lsl.w   #3,%d2\n\t"
    "move.w  %d2,%d3\n\t"
    "add.w   %d2,%d2\n\t"
    "add.w   %d2,%d2\n\t"
    "add.w   %d3,%d2\n\t"               /* 40n, the strip's bytes */
    "ext.l   %d2\n\t"
    "add.l   %d2,48(%sp)\n\t"           /* the next strip's bases */
    "add.l   %d2,52(%sp)\n\t"
    "move.l  64(%sp),%a5\n\t"
    "suba.l  %d2,%a5\n\t"               /* source: its stride less 40n */
    "move.l  68(%sp),%a6\n"             /* destination: its stride */
    "3:\n\t"
    "swap    %d0\n\t"
    "jmp     5f(%pc,%d0.w)\n"
    "5:\n\t"
    RC_UNIT(40) RC_UNIT(80) RC_UNIT(120) RC_UNIT(160)
    RC_UNIT(200) RC_UNIT(240) RC_UNIT(280) RC_UNIT(320)
    "adda.l  %a5,%a0\n\t"
    "adda.l  %a6,%a1\n\t"
    "swap    %d0\n\t"
    "dbf     %d0,3b\n\t"
    "bra     1b\n"
    "4:\n\t"                            /* the tails: 0-32 bytes a row */
    "move.w  56(%sp),%d1\n\t"
    "beq.s   7f\n\t"
    "moveq   #32,%d0\n\t"
    "sub.w   %d1,%d0\n\t"
    "lsr.w   #1,%d0\n\t"                /* 2 bytes a long's move skipped */
    "lea     8f(%pc),%a2\n\t"
    "adda.w  %d0,%a2\n\t"
    "move.w  62(%sp),%d0\n\t"
    "subq.w  #1,%d0\n\t"
    "move.l  48(%sp),%a1\n\t"
    "move.l  52(%sp),%a0\n\t"
    "ext.l   %d1\n\t"
    "move.l  64(%sp),%a5\n\t"
    "suba.l  %d1,%a5\n\t"
    "move.l  68(%sp),%a6\n\t"
    "suba.l  %d1,%a6\n"
    "6:\n\t"
    "jmp     (%a2)\n"
    "8:\n\t"
    RC_LONG RC_LONG RC_LONG RC_LONG RC_LONG RC_LONG RC_LONG RC_LONG
    "adda.l  %a5,%a0\n\t"
    "adda.l  %a6,%a1\n\t"
    "dbf     %d0,6b\n"
    "7:\n\t"
    "movem.l (%sp)+,%d2-%d7/%a2-%a6\n\t"
    "rts\n");
#else
/*
 * C twin - what tests/host exercises and the asm must match, pass for
 * pass: a unit read whole before it is written, as a movem does, so an
 * overlap comes out here as it would on the machine. Words, which the
 * host's alignment sanitizer accepts at any even address.
 */
void stdl_copy_rows(uint8_t *dp, const uint8_t *sp, long bytes, long h,
                    long sstride, long dstride)
{
    long q = bytes / ROW_UNIT, r = bytes % ROW_UNIT, y;
    uint16_t unit[ROW_UNIT / 2];
    int u, i, n;

    while (q > 0) {
        n = q > ROW_UNITS ? ROW_UNITS : (int)q;
        for (y = 0; y < h; y++) {
            const uint16_t *s = (const uint16_t *)(sp + y * sstride);
            uint16_t *d = (uint16_t *)(dp + y * dstride);

            for (u = 0; u < n; u++) {
                for (i = 0; i < ROW_UNIT / 2; i++) {
                    unit[i] = *s++;
                }
                for (i = 0; i < ROW_UNIT / 2; i++) {
                    *d++ = unit[i];
                }
            }
        }
        dp += n * ROW_UNIT;
        sp += n * ROW_UNIT;
        q -= n;
    }
    for (y = 0; r != 0 && y < h; y++) {
        const uint16_t *s = (const uint16_t *)(sp + y * sstride);
        uint16_t *d = (uint16_t *)(dp + y * dstride);

        for (i = 0; i < r / 2; i += 2) {
            const uint16_t a = s[i], b = s[i + 1];

            d[i] = a;
            d[i + 1] = b;
        }
    }
}
#endif
/* declared here and nowhere else: a caller goes through the overlap
 * test below, stdl_copy_rows_checked */
void stdl_copy_rows(uint8_t *dp, const uint8_t *sp, long bytes, long h,
                    long sstride, long dstride);

/*
 * The copy where its passes are right for these rows (see the top of
 * this file): 0, or -1 having done nothing. The test is the rows'
 * extents, so two surfaces over one block meet it as one surface's
 * rectangles do. bytes a multiple of 8, from 8, under 65536; h >= 1.
 * The strides may be negative: a blit whose destination's rows lie
 * below its source's runs them bottom up (blit.c), the rows passed
 * their last, and one pass is right for those where no row overlaps
 * its own source.
 */
int stdl_copy_rows_checked(uint8_t *dp, const uint8_t *sp, int bytes,
                           int h, int sstride, int dstride)
{
    const int32_t sr = stdl_mul16(h - 1, sstride);
    const int32_t dr = stdl_mul16(h - 1, dstride);

    if (dp + (dr < 0 ? dr : 0) < sp + (sr > 0 ? sr : 0) + bytes
        && sp + (sr < 0 ? sr : 0) < dp + (dr > 0 ? dr : 0) + bytes
        && (bytes > ROW_UNIT * ROW_UNITS
            || stdl_row_off(stdl_divu((uint32_t)bytes, ROW_UNIT),
                            ROW_UNIT) != (uint32_t)bytes
            || (sstride > 0 ? dp > sp : dp < sp + bytes))) {
        return -1;
    }
    stdl_copy_rows(dp, sp, bytes, h, sstride, dstride);
    return 0;
}
