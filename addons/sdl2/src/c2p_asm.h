/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The conversion's assembler, as macros: c2p.c, c2pany.c and c2px.c
 * each make a pair of loops from them (c2p.h's variants), one line a
 * row and two. Included once by each, which the top-level __asm__
 * definitions need.
 */

#ifndef STDL_SDL2_C2P_ASM_H
#define STDL_SDL2_C2P_ASM_H

/*
 * The C's steps: d0-d3 its a0-a3, d7 the merges' t; masks 0x0F0F0F0F
 * in d4 and 0x00FF00FF in d5, the other two immediate. The 16-bit
 * exchange is the C's merge and the swaps after it in one: its five
 * instructions move the words so that each long comes out with its
 * planes in screen order, so the swaps cost nothing. Each stage takes
 * S, a store of the previous group's planes, parked in a3-a6 so the
 * writes to ST-RAM go out among the arithmetic: one line a row stores
 * after every second merge, two lines (a TT's) after every merge, as
 * the 68030 holds one write while it works on and a second waits for
 * the first. X is the tail T, applied once the merges are done:
 * nothing for X = 0 (c2p.c), a not.l or a not.w or both on each of
 * planes 4-5 and 6-7 for a fixed X (c2px.c, one object an X), and for
 * an X known only at run time its two longs read from the arguments
 * and XORed in (c2pany.c), which costs a little more; so a program
 * names its X as a constant and links that one pair.
 */
#define C2P_M(sh, back, ra, rb, mask) \
    "	move.l	" rb ",%d7\n" \
    "	lsr.l	#" #sh ",%d7\n" \
    "	eor.l	" ra ",%d7\n" \
    "	and.l	" mask ",%d7\n" \
    "	eor.l	%d7," ra "\n" \
    "	" back "\n" \
    "	eor.l	%d7," rb "\n"
/* ra = (A, B), rb = (C, D) by words: ra = (C, A), rb = (D, B) */
#define C2P_W(ra, rb) \
    "	swap	" rb "\n" \
    "	move.w	" rb ",%d7\n" \
    "	move.w	" ra "," rb "\n" \
    "	move.w	%d7," ra "\n" \
    "	swap	" ra "\n"
#define C2P_GROUP(S1, S2, S3, S4, S5, S6, S7, S8, T) \
    "	move.l	(%a0)+,%d0\n" \
    "	move.l	(%a0)+,%d1\n" \
    "	move.l	(%a0)+,%d2\n" \
    "	move.l	(%a0)+,%d3\n" \
    C2P_M(4, "lsl.l	#4,%d7", "%d0", "%d1", "%d4") S1 \
    C2P_M(4, "lsl.l	#4,%d7", "%d2", "%d3", "%d4") S2 \
    C2P_M(8, "lsl.l	#8,%d7", "%d0", "%d2", "%d5") S3 \
    C2P_M(8, "lsl.l	#8,%d7", "%d1", "%d3", "%d5") S4 \
    C2P_M(1, "add.l	%d7,%d7", "%d0", "%d2", "#0x55555555") S5 \
    C2P_M(1, "add.l	%d7,%d7", "%d1", "%d3", "#0x55555555") S6 \
    C2P_W("%d0", "%d2") S7 \
    C2P_W("%d1", "%d3") S8 \
    C2P_M(2, "lsl.l	#2,%d7", "%d0", "%d2", "#0x33333333") \
    C2P_M(2, "lsl.l	#2,%d7", "%d1", "%d3", "#0x33333333") \
    T \
    "	move.l	%d3,%a3\n" \
    "	move.l	%d1,%a4\n" \
    "	move.l	%d2,%a5\n" \
    "	move.l	%d0,%a6\n"
#define C2P_S1(r) "	move.l	" r ",(%a1)+\n"
#define C2P_S2(r) "	move.l	" r ",(%a2)+\n"
#define C2P_G1(T) C2P_GROUP("", C2P_S1("%a3"), "", C2P_S1("%a4"), \
                            "", C2P_S1("%a5"), "", C2P_S1("%a6"), T)
#define C2P_G2(T) C2P_GROUP(C2P_S1("%a3"), C2P_S2("%a3"), \
                            C2P_S1("%a4"), C2P_S2("%a4"), \
                            C2P_S1("%a5"), C2P_S2("%a5"), \
                            C2P_S1("%a6"), C2P_S2("%a6"), T)

/*
 * One call a box, with no copy of the group before the loop: the
 * stores of a row's last group go out during the next row's first,
 * whose own stores the next group makes once a1 (and a2) have moved to
 * the row. So the loop is left twice a row. With d6's high word 0 it
 * was the row's first group: a1 moves on by CORR, and the row's other
 * groups run with the high word 0xFFFF (GM2), so that the next time it
 * is the row's end, for the next row's source and its first group.
 * The box's first stores, of nothing, go to 16 bytes of scratch.
 *
 * The loop and the code round it, from _cache0 to _cache1, must sit
 * in the 68030's instruction cache, 16 lines of 16 bytes: 240 bytes
 * fit whatever its alignment, and sdl2.mk's sdl2check refuses more
 * (the assembler cannot measure across forward branches). A copy of
 * the group before the loop, which this had until 2026-11-07, made a
 * loop of 428 bytes that left the cache at every row: a 320x240 frame
 * as one row of 4,800 groups took 30.4ms on a TT against 34.3 as 240
 * rows, boxes of a group or three went through the C twin instead,
 * and those of 4 cost 49.1 a frame's worth (Hatari).
 *
 * The stack, past 32 bytes of frame, the eleven registers and the
 * return: src 80(sp), sp 84, dst 88, dp 92, groups 96 (32,768 at
 * most), h 100, x2 104, x0 108; the frame holds the scratch, SKIPS 16
 * (from a row's end to the next row's source), DSKIP 20 (the same for
 * the destination), CORR 24 (a1's move at a row's first group: from
 * the scratch for the first row, DSKIP after) and GM2 28 (groups - 2
 * with the high word set, or 0 for a box a group wide). h moves on a
 * row at a time.
 */
#define C2P_FUNC(name, G, DSTEP2, A2SET, A2FIX, DRAIN) \
    "	.even\n" \
    "	.globl	_" name "\n" \
    "_" name ":\n" \
    "	movem.l	%d2-%d7/%a2-%a6,-(%sp)\n" \
    "	lea	-32(%sp),%sp\n" \
    "	move.l	#0x0F0F0F0F,%d4\n" \
    "	move.l	#0x00FF00FF,%d5\n" \
    "	move.l	96(%sp),%d7\n" \
    "	lsl.l	#4,%d7\n" \
    "	move.l	84(%sp),%d0\n" \
    "	sub.l	%d7,%d0\n" \
    "	move.l	%d0,16(%sp)\n" \
    "	move.l	92(%sp),%d0\n" \
    DSTEP2 \
    "	sub.l	%d7,%d0\n" \
    "	move.l	%d0,20(%sp)\n" \
    "	lea	16(%sp),%a1\n" \
    "	move.l	88(%sp),%d0\n" \
    "	sub.l	%a1,%d0\n" \
    "	move.l	%d0,24(%sp)\n" \
    "	move.l	96(%sp),%d0\n" \
    "	subq.l	#2,%d0\n" \
    "	bpl.s	5f\n" \
    "	moveq	#0,%d0\n" \
    "	bra.s	6f\n" \
    "5:	or.l	#0xFFFF0000,%d0\n" \
    "6:	move.l	%d0,28(%sp)\n" \
    "	move.l	80(%sp),%a0\n" \
    "	move.l	%sp,%a1\n" \
    A2SET \
    "	moveq	#0,%d6\n" \
    "_" name "_cache0:\n" \
    "1:\n" G \
    "	dbra	%d6,1b\n" \
    "	tst.l	%d6\n" \
    "	bmi.s	4f\n" \
    "	adda.l	24(%sp),%a1\n" \
    A2FIX \
    "	move.l	28(%sp),%d6\n" \
    "	bne.w	1b\n" \
    "4:	subq.l	#1,100(%sp)\n" \
    "	beq.s	9f\n" \
    "	adda.l	16(%sp),%a0\n" \
    "	move.l	20(%sp),24(%sp)\n" \
    "	moveq	#0,%d6\n" \
    "	bra.w	1b\n" \
    "_" name "_cache1:\n" \
    "9:\n" DRAIN \
    "	lea	32(%sp),%sp\n" \
    "	movem.l	(%sp)+,%d2-%d7/%a2-%a6\n" \
    "	rts\n"

#define C2P_DRAIN1 C2P_S1("%a3") C2P_S1("%a4") C2P_S1("%a5") C2P_S1("%a6")
#define C2P_DRAIN2 C2P_S1("%a3") C2P_S2("%a3") C2P_S1("%a4") C2P_S2("%a4") \
                   C2P_S1("%a5") C2P_S2("%a5") C2P_S1("%a6") C2P_S2("%a6")

/* a variant's two loops, named PREFIX1 and PREFIX2, with tail T */
#define C2P_PAIR(prefix, T) \
    "	.text\n" \
    C2P_FUNC(prefix "1", C2P_G1(T), "", "", "", C2P_DRAIN1) \
    C2P_FUNC(prefix "2", C2P_G2(T), "	add.l	%d0,%d0\n", \
             "	move.l	%sp,%a2\n", \
             "	move.l	%a1,%a2\n	adda.l	92(%sp),%a2\n", C2P_DRAIN2)

#endif /* STDL_SDL2_C2P_ASM_H */
