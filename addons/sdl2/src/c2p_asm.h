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
 * in d4 and 0x00FF00FF in d5, the other two immediate. Each stage
 * takes S, a store of the previous group's planes, parked in a3-a6 so
 * the writes to ST-RAM go out among the arithmetic. The row loop is
 * here too: one call a box, where a call a row cost a port's short
 * boxes more than the pipelining saved them (16.7ms a frame on a TT
 * against the C's 16.0). X is the tail T, applied once the merges are
 * done: nothing for X = 0 (c2p.c), a not.l or a not.w or both on each
 * of planes 4-5 and 6-7 for a fixed X (c2px.c, one object an X), and
 * for an X known only at run time its two longs read from the
 * arguments and XORed in (c2pany.c). Reading them cost 1.6ms of a whole
 * 320x240 frame's 35.0 on a TT and 2.7 of 70.4 on a Falcon (Hatari),
 * where a fixed X's inversions cost what the routine this came from
 * did; so a program names its X as a constant and links that one pair.
 */
#define C2P_M(sh, back, ra, rb, mask) \
    "	move.l	" rb ",%d7\n" \
    "	lsr.l	#" #sh ",%d7\n" \
    "	eor.l	" ra ",%d7\n" \
    "	and.l	" mask ",%d7\n" \
    "	eor.l	%d7," ra "\n" \
    "	" back "\n" \
    "	eor.l	%d7," rb "\n"
#define C2P_W(ra, rb) \
    "	swap	" rb "\n" \
    "	move.w	" rb ",%d7\n" \
    "	move.w	" ra "," rb "\n" \
    "	move.w	%d7," ra "\n" \
    "	swap	" rb "\n"
#define C2P_GROUP(S1, S2, S3, S4, T) \
    "	move.l	(%a0)+,%d0\n" \
    "	move.l	(%a0)+,%d1\n" \
    "	move.l	(%a0)+,%d2\n" \
    "	move.l	(%a0)+,%d3\n" \
    C2P_M(4, "lsl.l	#4,%d7", "%d0", "%d1", "%d4") \
    C2P_M(4, "lsl.l	#4,%d7", "%d2", "%d3", "%d4") \
    S1 \
    C2P_M(8, "lsl.l	#8,%d7", "%d0", "%d2", "%d5") \
    C2P_M(8, "lsl.l	#8,%d7", "%d1", "%d3", "%d5") \
    S2 \
    C2P_M(1, "add.l	%d7,%d7", "%d0", "%d2", "#0x55555555") \
    C2P_M(1, "add.l	%d7,%d7", "%d1", "%d3", "#0x55555555") \
    S3 \
    C2P_W("%d0", "%d2") \
    C2P_W("%d1", "%d3") \
    S4 \
    C2P_M(2, "lsl.l	#2,%d7", "%d0", "%d2", "#0x33333333") \
    C2P_M(2, "lsl.l	#2,%d7", "%d1", "%d3", "#0x33333333") \
    T \
    "	swap	%d3\n" \
    "	swap	%d1\n" \
    "	swap	%d2\n" \
    "	swap	%d0\n" \
    "	move.l	%d3,%a3\n" \
    "	move.l	%d1,%a4\n" \
    "	move.l	%d2,%a5\n" \
    "	move.l	%d0,%a6\n"
#define C2P_S1(r) "	move.l	" r ",(%a1)+\n"
#define C2P_S2(r) "	move.l	" r ",(%a1)+\n	move.l	" r ",(%a2)+\n"
/*
 * The arguments, past the eleven registers saved: src 48(sp), sp 52,
 * dst 56, dp 60, groups 64, h 68, x2 72, x0 76; src, dst and h move on
 * a row at a time. The group loop, label 1 to the dbra, must sit in
 * the 68030's instruction cache, 16 lines of 16 bytes: 240 bytes fit
 * whatever its alignment, and the assembler refuses more.
 */
#define C2P_FUNC(name, S, DST2, DSTSTEP, T) \
    "	.even\n" \
    "	.globl	_" name "\n" \
    "_" name ":\n" \
    "	movem.l	%d2-%d7/%a2-%a6,-(%sp)\n" \
    "	move.l	#0x0F0F0F0F,%d4\n" \
    "	move.l	#0x00FF00FF,%d5\n" \
    "3:	move.l	48(%sp),%a0\n" \
    "	move.l	56(%sp),%a1\n" \
    DST2 \
    "	move.l	64(%sp),%d6\n" \
    "	subq.l	#1,%d6\n" \
    C2P_GROUP("", "", "", "", T) \
    "	bra.w	2f\n" \
    "1:\n" \
    C2P_GROUP(S("%a3"), S("%a4"), S("%a5"), S("%a6"), T) \
    "2:	dbra	%d6,1b\n" \
    "	.if	(2b - 1b) + 4 > 240\n" \
    "	.error	\"" name ": the group loop outgrew the 68030's instruction cache\"\n" \
    "	.endif\n" \
    S("%a3") S("%a4") S("%a5") S("%a6") \
    "	move.l	52(%sp),%d7\n" \
    "	add.l	%d7,48(%sp)\n" \
    "	move.l	60(%sp),%d7\n" \
    DSTSTEP \
    "	add.l	%d7,56(%sp)\n" \
    "	subq.l	#1,68(%sp)\n" \
    "	bne.w	3b\n" \
    "	movem.l	(%sp)+,%d2-%d7/%a2-%a6\n" \
    "	rts\n"

#define C2P_DST2 "	move.l	60(%sp),%d7\n	lea	0(%a1,%d7.l),%a2\n"
#define C2P_STEP2 "	add.l	%d7,%d7\n"

/* a variant's two loops, named PREFIX1 and PREFIX2, with tail T */
#define C2P_PAIR(prefix, T) \
    "	.text\n" \
    C2P_FUNC(prefix "1", C2P_S1, "", "", T) \
    C2P_FUNC(prefix "2", C2P_S2, C2P_DST2, C2P_STEP2, T)

#endif /* STDL_SDL2_C2P_ASM_H */
