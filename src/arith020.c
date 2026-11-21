/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * libgcc's 32-bit multiplies and divides, and two of its 64-bit ones,
 * on a 68020's own instructions when the machine has one: a 68000
 * program that is quicker on a TT or a Falcon (docs/building.md).
 *
 * gcc 4.6 compiles a 32-bit multiply or divide for the 68000 as a call
 * of libgcc's __mulsi3, __udivsi3 and the rest, hundreds of cycles to
 * thousands. A program linked with STDL_ARITH020's flags (arith020.mk)
 * wraps them (ld's --wrap): every call, the program's, the library's,
 * the C library's and libgcc's own, comes here first. On a 68020 or
 * later the answer is one muls.l or divu.l; on a 68000 it goes on to
 * libgcc's (__real___mulsi3, which the link makes __mulsi3). __muldi3
 * and __divdi3 take the 64-bit forms on a 68020 to 68040: a 68060 has
 * none (its support package emulates them, a trap each), and keeps
 * libgcc's. Every answer is libgcc's, to the bit: the low 32 bits of
 * a product whatever the signs, quotients rounded toward zero, a
 * remainder with the dividend's sign. What the instructions cannot
 * give goes to libgcc as well: -2^31 / -1 (divs.l overflows, libgcc
 * gives -2^31 and a remainder of 0), and a 64-bit division whose
 * divisor or quotient does not fit 32 bits. Dividing by 0 takes the
 * zero-divide exception (vector 5) on either path, as libgcc's own
 * divu.w does: TOS shows bombs, MiNT sends SIGFPE.
 *
 * An object of its own, and nothing else names it: a program without
 * the flags links none of it and is byte for byte what it was.
 *
 * Which path, asked on the first call and kept in a byte each:
 *
 *  - stdl_arith_l32 (-1 not asked yet, 0 a 68000 or 68010, 1 a 68020
 *    or later) from the CPU itself: lea (0,a0,d0.l*2) scales the index
 *    on a 68020 and later, and the 68000 and 68010 ignore the scale
 *    bits. No supervisor mode, no cookie jar, no system call: right
 *    before main, before STDL_Init, in user mode under MiNT's memory
 *    protection, and from an interrupt.
 *  - stdl_arith_l64 (-1, 0, 1 a 68020 to 68040) needs the _CPU cookie
 *    to tell a 68060, so it is asked only on a 68020 or later, at the
 *    first 64-bit call: Ssystem(S_GETCOOKIE) where there is one (MiNT,
 *    MagiC), else Supexec, both good from user or supervisor mode. Not
 *    from an interrupt routine: a program whose first 64-bit multiply
 *    or divide could be in one does one in main first. No cookie (a
 *    68020 card in an ST without a jar) keeps libgcc's.
 *
 * What it costs a 68000: 41 cycles a call (the byte tested, two
 * branches, the jump), and libgcc's own inner calls pay it too (its
 * __modsi3 calls __divsi3, which calls __udivsi3, and __mulsi3): 10-18%
 * more than libgcc alone on an 8MHz STE, 275 cycles for __mulsi3
 * against 234, 4,633 for __divdi3 against 4,182. So a program for the
 * ST alone leaves the flags out. On a 68020 or later a call is about
 * the instruction's own time: tests/hatari/arithchk.c (ARITHCHK) has
 * the figures, and docs/building.md what they mean.
 *
 * The 68020 instructions are written as words, as the library is
 * assembled for the 68000.
 */

#ifdef __m68k__

#include <mint/osbind.h>
#include <mint/mintbind.h>

#define AR_STR2(x) #x
#define AR_STR(x) AR_STR2(x)
#define AR_SYM(n) AR_STR(__USER_LABEL_PREFIX__) #n

signed char stdl_arith_l32 = -1;
signed char stdl_arith_l64 = -1;

long stdl_arith_cpu(void);

/* in supervisor mode: the jar's _CPU, 0 without one */
static long cpu_cookie(void)
{
    const long *jar = *(const long *const *)0x5A0UL;

    if (jar != 0) {
        for (; jar[0] != 0; jar += 2) {
            if (jar[0] == 0x5F435055L) {        /* '_CPU' */
                return jar[1];
            }
        }
    }
    return 0;
}

/* The _CPU cookie (0 without one), from user or supervisor mode,
 * under MiNT's memory protection too: Ssystem(S_GETCOOKIE) answers 0
 * and the value where it is there, and -32 (EINVFN) on a TOS without
 * it, which then reads the jar through Supexec. Called from the
 * wrappers below, which keep nothing in d0-d1/a0-a1. */
long stdl_arith_cpu(void)
{
    long cpu = 0;
    long r = Ssystem(8, 0x5F435055L, (long)&cpu);       /* S_GETCOOKIE */

    if (r == -32) {
        return Supexec(cpu_cookie);
    }
    return r == 0 ? cpu : 0;
}

/* The way in: the byte, 0 straight on to libgcc's (the 68000's way,
 * 38 cycles: the test, the branch and the jump), 1 the instruction
 * (FAST, ending in rts or a branch to libgcc's), -1 the CPU asked and
 * the call started again. */
#define AR_WRAP(name, flag, ask, fast) \
    "\t.globl " AR_SYM(__wrap_##name) "\n" \
    AR_SYM(__wrap_##name) ":\n\t" \
    "tst.b   " AR_SYM(flag) "\n\t" \
    "beq.s   .Larith_real_" #name "\n\t" \
    "bmi.s   1f\n\t" \
    fast \
    ".Larith_real_" #name ":\n\t" \
    "jmp     " AR_SYM(__real_##name) "\n" \
    "1:\n\t" \
    "pea     " AR_SYM(__wrap_##name) "(%pc)\n\t" \
    "bra     " ask "\n"
#define AR_WRAP32(name, fast) \
    AR_WRAP(name, stdl_arith_l32, ".Larith_cpu32", fast)
#define AR_WRAP64(name, fast) \
    AR_WRAP(name, stdl_arith_l64, ".Larith_cpu64", fast)

__asm__(
    "\t.text\n\t.even\n"

    /* the low 32 bits of the product, signed or not */
    AR_WRAP32(__mulsi3,
    "move.l  4(%sp),%d0\n\t"
    ".word   0x4C2F,0x0800,0x0008\n\t"      /* muls.l 8(sp),d0 */
    "rts\n")

    AR_WRAP32(__udivsi3,
    "move.l  4(%sp),%d0\n\t"
    ".word   0x4C6F,0x0000,0x0008\n\t"      /* divu.l 8(sp),d0 */
    "rts\n")

    /* -2^31 / -1 overflows divs.l, which then leaves d0 as it was:
     * libgcc's answer, -2^31, is the dividend, but take it from
     * libgcc all the same */
    AR_WRAP32(__divsi3,
    "move.l  4(%sp),%d0\n\t"
    ".word   0x4C6F,0x0800,0x0008\n\t"      /* divs.l 8(sp),d0 */
    "bvs.s   .Larith_real___divsi3\n\t"
    "rts\n")

    AR_WRAP32(__umodsi3,
    "move.l  4(%sp),%d0\n\t"
    ".word   0x4C6F,0x0001,0x0008\n\t"      /* divul.l 8(sp),d1:d0 */
    "move.l  %d1,%d0\n\t"
    "rts\n")

    AR_WRAP32(__modsi3,
    "move.l  4(%sp),%d0\n\t"
    ".word   0x4C6F,0x0801,0x0008\n\t"      /* divsl.l 8(sp),d1:d0 */
    "bvs.s   .Larith_real___modsi3\n\t"
    "move.l  %d1,%d0\n\t"
    "rts\n")

    /* long long __muldi3(long long a, long long b): a at 4(sp), its
     * high long first, b at 12(sp); the product's low 64 bits in
     * d0:d1, high long in d0. a_lo * b_lo in 64 bits, and the two
     * cross products' low longs added to its high long. */
    AR_WRAP64(__muldi3,
    "move.l  8(%sp),%d1\n\t"                /* a's low long */
    ".word   0x4C2F,0x1400,0x0010\n\t"      /* mulu.l 16(sp),d0:d1 */
    "move.l  %d0,%a0\n\t"
    "move.l  4(%sp),%d0\n\t"
    ".word   0x4C2F,0x0800,0x0010\n\t"      /* muls.l 16(sp),d0: a_hi*b_lo */
    "add.l   %d0,%a0\n\t"
    "move.l  8(%sp),%d0\n\t"
    ".word   0x4C2F,0x0800,0x000C\n\t"      /* muls.l 12(sp),d0: a_lo*b_hi */
    "add.l   %a0,%d0\n\t"
    "rts\n")

    /* long long __divdi3(long long a, long long b): with b a 32-bit
     * number (its high long only its low long's sign), one divs.l of
     * 64 bits by 32; a quotient past 32 bits overflows it, and goes
     * to libgcc. b = 0 takes the zero-divide exception, as libgcc's
     * 1 / 0 does. */
    AR_WRAP64(__divdi3,
    "move.l  16(%sp),%d1\n\t"               /* b's low long */
    "smi     %d0\n\t"
    "ext.w   %d0\n\t"
    "ext.l   %d0\n\t"                       /* its sign, as a long */
    "cmp.l   12(%sp),%d0\n\t"               /* b's high long */
    "bne.s   .Larith_real___divdi3\n\t"
    "move.l  %d2,%a0\n\t"
    "move.l  4(%sp),%d2\n\t"                /* a's high long */
    "move.l  8(%sp),%d0\n\t"                /* and its low */
    ".word   0x4C41,0x0C02\n\t"             /* divs.l d1,d2:d0 */
    "exg     %d2,%a0\n\t"                   /* d2 back, V kept */
    "bvs.s   .Larith_real___divdi3\n\t"
    "move.l  %d0,%d1\n\t"
    "smi     %d0\n\t"
    "ext.w   %d0\n\t"
    "ext.l   %d0\n\t"                       /* the quotient's sign */
    "rts\n")

    /* stdl_arith_l32 from the CPU: the index d0 = 1 scaled by 2 on a
     * 68020 and later, by 1 (the scale bits ignored) before */
    ".Larith_cpu32:\n\t"
    "moveq   #1,%d0\n\t"
    "suba.l  %a0,%a0\n\t"
    ".word   0x41F0,0x0A00\n\t"             /* lea (0,a0,d0.l*2),a0 */
    "move.l  %a0,%d0\n\t"
    "subq.b  #1,%d0\n\t"
    "move.b  %d0," AR_SYM(stdl_arith_l32) "\n\t"
    "rts\n"

    /* stdl_arith_l64: 0 on a 68000 or 68010, else the _CPU cookie
     * from 20 to 59 */
    ".Larith_cpu64:\n\t"
    "tst.b   " AR_SYM(stdl_arith_l32) "\n\t"
    "bpl.s   1f\n\t"
    "bsr.s   .Larith_cpu32\n"
    "1:\n\t"
    "moveq   #0,%d0\n\t"
    "tst.b   " AR_SYM(stdl_arith_l32) "\n\t"
    "beq.s   2f\n\t"
    "jsr     " AR_SYM(stdl_arith_cpu) "\n\t"
    "moveq   #20,%d1\n\t"
    "cmp.l   %d1,%d0\n\t"
    "blt.s   1f\n\t"
    "moveq   #60,%d1\n\t"
    "cmp.l   %d1,%d0\n\t"
    "bge.s   1f\n\t"
    "moveq   #1,%d0\n\t"
    "bra.s   2f\n"
    "1:\n\t"
    "moveq   #0,%d0\n"
    "2:\n\t"
    "move.b  %d0," AR_SYM(stdl_arith_l64) "\n\t"
    "rts\n"
);

#endif /* __m68k__ */
