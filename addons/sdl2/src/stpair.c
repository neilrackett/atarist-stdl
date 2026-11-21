/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Pixel pairs on an ST's screen (st.h), in the experimental 68000
 * build only: STDL_SDL2_RenderCopyPairs's groups converted from their
 * eight distinct pixels, a lookup a pair where st.c's conversion has a
 * lookup a pixel. An object of its own, which c2ppair.c's pointers name,
 * so that a program that never asks for pairs links none of it, and
 * the renderer allocates the tables only at a first pairs copy.
 */

#include "st.h"

#ifndef STDL_SDL2_ST
#error "stpair.c belongs to the ST build of the add-on (make sdl2st)"
#endif

/*
 * The tables. st.c's conversion has a slot's four bits spread a byte
 * each over a long, shifted 3, 2, 1 and 0 for a pixel's place among
 * four, and a shift by 4 between the two fours of a byte of each plane.
 * A pair's two pixels have one colour and an x even and odd, so the
 * checkerboard shows them through tables 0 and 1 (or 2 and 3) of that
 * row's set, and their two bits are those two tables' entries ORed. So
 * a pair's table is made from the conversion's own: the pixels' slots
 * and the checkerboard are its, and a group in pairs comes out as the
 * conversion would give it, by construction. Four tables a set, for a
 * pair's place among four in a byte of each plane (the first two
 * shifted 4 more), so a group is eight lookups and no shift: 8K for
 * the two rows' sets, as the conversion's own.
 */
void stdl_sdl2_st_pairtabs(const Uint32 *tab, Uint32 *ptab)
{
    int set, i;
    for (set = 0; set < 2; set++, tab += 1024, ptab += 1024) {
        for (i = 0; i < 256; i++) {
            const Uint32 a = tab[i] | tab[256 + i];
            const Uint32 b = tab[512 + i] | tab[768 + i];
            ptab[i] = a << 4;
            ptab[256 + i] = b << 4;
            ptab[512 + i] = a;
            ptab[768 + i] = b;
        }
    }
}

/*
 * The loop: a pair is one word read, both its pixels, and the second's
 * byte (and.w with $FF, the low byte being the odd address's) is the
 * index. About 40 cycles a pair where st.c's loop spends about 40 a
 * pixel: move.w (a0)+ 8, and.w 4, two add.w 8, or.l d8(An,Xn) 20 (the
 * first of a long a move.l, 18). A group is eight of them, two movep.l
 * and the loop's own: 382 cycles against 734, 24 a pixel against 46.
 * The word read wants src and sp even, which the renderer sees to.
 */
#ifdef __m68k__
/* d0 and d2 the group's two longs, d1 the index, d7 its mask, a2-a5 the
 * four tables; d3 groups - 1, d6 the group count, d4 and d5 what is
 * left of each row's strides, and the rows counted in their argument */
#define SP_P(op, t) \
    "	move.w	(%a0)+,%d1\n" \
    "	and.w	%d7,%d1\n" \
    "	add.w	%d1,%d1\n" \
    "	add.w	%d1,%d1\n" \
    "	" op "	(" t ",%d1.w),"
#define SP_L(r) \
    SP_P("move.l", "%a2") r "\n" SP_P("or.l", "%a3") r "\n" \
    SP_P("or.l", "%a4") r "\n" SP_P("or.l", "%a5") r "\n"

__asm__(
"	.text\n"
"	.even\n"
"	.globl	_stdl_sdl2_st_c2pp\n"
"_stdl_sdl2_st_c2pp:\n"
"	movem.l	%d2-%d7/%a2-%a5,-(%sp)\n"
"	move.l	44(%sp),%a0\n"            /* src */
"	move.l	48(%sp),%d4\n"            /* sp */
"	move.l	52(%sp),%a1\n"            /* dst */
"	move.l	56(%sp),%d5\n"            /* dp */
"	move.l	60(%sp),%d3\n"            /* groups; 64: h */
"	move.l	68(%sp),%a2\n"            /* this row's tables; 72: the next's */
"	move.w	#255,%d7\n"
"	move.l	%d3,%d0\n"
"	lsl.l	#3,%d0\n"
"	sub.l	%d0,%d5\n"                /* dp - 8 * groups */
"	add.l	%d0,%d0\n"
"	sub.l	%d0,%d4\n"                /* sp - 16 * groups */
"	subq.w	#1,%d3\n"
"1:	lea	1024(%a2),%a3\n"
"	lea	1024(%a3),%a4\n"
"	lea	1024(%a4),%a5\n"
"	move.w	%d3,%d6\n"
"2:\n"
SP_L("%d0")
SP_L("%d2")
"	movep.l	%d0,0(%a1)\n"
"	movep.l	%d2,1(%a1)\n"
"	addq.l	#8,%a1\n"
"	dbra	%d6,2b\n"
"	add.l	%d4,%a0\n"
"	add.l	%d5,%a1\n"
"	move.l	72(%sp),%d0\n"            /* the two rows' tables change places */
"	move.l	%a2,72(%sp)\n"
"	move.l	%d0,%a2\n"
"	subq.l	#1,64(%sp)\n"
"	bne	1b\n"
"	movem.l	(%sp)+,%d2-%d7/%a2-%a5\n"
"	rts\n"
);
#else
/* the second pixel of each pair, as the loop's word read takes it */
#define PFOUR(s) (tab[(s)[1]] | tab[256 + (s)[3]] | tab[512 + (s)[5]] | \
                  tab[768 + (s)[7]])

void stdl_sdl2_st_c2pp(const Uint8 *src, long sp, Uint8 *dst, long dp,
                       long groups, long h, const Uint32 *tab,
                       const Uint32 *next)
{
    for (; h > 0; h--, src += sp, dst += dp) {
        const Uint8 *s = src;
        const Uint32 *row = tab;
        Uint8 *d = dst;
        long g;
        for (g = groups; g > 0; g--, s += 16, d += 8) {
            const Uint32 a = PFOUR(s);
            const Uint32 b = PFOUR(s + 8);
            d[0] = (Uint8)(a >> 24);
            d[2] = (Uint8)(a >> 16);
            d[4] = (Uint8)(a >> 8);
            d[6] = (Uint8)a;
            d[1] = (Uint8)(b >> 24);
            d[3] = (Uint8)(b >> 16);
            d[5] = (Uint8)(b >> 8);
            d[7] = (Uint8)b;
        }
        tab = next;
        next = row;
    }
}
#endif
