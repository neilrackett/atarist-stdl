/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * libgcc's 32-bit multiplies and divides, and two of its 64-bit ones,
 * on a 68020's own instructions when the machine has one: a 68000
 * program that is quicker on a TT or a Falcon. docs/building.md says
 * how a program links it and what it costs and gains.
 *
 * Built as libstdl-arith020.o, an object beside the archives, not a
 * member of them, which a program links through arith020.mk's
 * STDL_ARITH020 and nothing in the library names. Its flags wrap
 * libgcc's __mulsi3, __udivsi3, __divsi3, __umodsi3, __modsi3, __muldi3
 * and __divdi3 (ld's --wrap), so every call comes here first: on a
 * 68020 or later the instruction, on a 68000 libgcc's own
 * (__real___mulsi3, which the link makes __mulsi3). __muldi3 and
 * __divdi3 take the 64-bit forms on a 68020 to 68040 only: a 68060 has
 * none (its support package emulates them, a trap each). It calls the
 * system through its traps and no C library, so one object serves
 * libcmini and mintlib programs.
 *
 * The contract: every answer is libgcc's, to the bit. The low 32 bits
 * of a product whatever the signs, quotients rounded toward zero, a
 * remainder with the dividend's sign. What the instructions cannot
 * give goes to libgcc: -2^31 / -1 (divs.l overflows, libgcc gives -2^31
 * and a remainder of 0), and a 64-bit division whose divisor or
 * quotient does not fit 32 bits. Dividing by 0 takes the zero-divide
 * exception (vector 5) on either path, as libgcc's own divu.w does.
 * d2-d7 and a2-a6 are kept, as the C calls expect, the CPU's first
 * question included.
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
 * Each wrapper starts with its byte's test, tst.b abs.l, and on a
 * 68020 or later that is all it ever costs: the test and a branch not
 * taken. On a 68000 or 68010 the first call rewrites every wrapper's
 * tst.b, six bytes, as a jmp to libgcc's routine, so from then on a
 * call costs that jmp, 12 cycles, and tests nothing. Only there: a
 * 68000 or 68010 has no instruction cache to keep the old test (a
 * Mega STE's cache holds code and data alike, and sees every write
 * the CPU makes) and no MMU to protect the program's text; a 68020
 * or later, which may have both, never has its text written. One movem.l writes the jmp and
 * the branch after it, as it was, eight bytes in one instruction, so
 * an interrupt sees a wrapper whole, old or new, and code interrupted
 * between the tst.b and the branch comes back to that same branch.
 * Old or new, a wrapper answers right: until its jmp is written it
 * tests the byte, which says 0 before the first jmp is written.
 * tests/hatari/arithchk.c (ARITHCHK) is the check, and knows this
 * layout: a wrapper's first six bytes are the tst.b or the jmp.
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

/*
 * The way in: the byte, tested by the first instruction, the one a
 * 68000's first call makes a jmp to libgcc's. 1 the instruction (FAST,
 * ending in rts or a branch to 8f, libgcc's), 0 straight on to
 * libgcc's, -1 the CPU asked (ASK) and the call started again.
 *
 * Every label is a numeric local one, which leaves no symbol: in this
 * toolchain's a.out a .L label is a text symbol, which a profiler or a
 * debugger takes for a function. 6, 8 and 9 are each wrapper's own, 3
 * and 4 the questions' at the end (no wrapper defines those), and FAST
 * defines none of 3, 4, 6, 8 and 9.
 */
#define AR_WRAP(name, flag, ask, fast) \
    "\t.globl " AR_SYM(__wrap_##name) "\n" \
    AR_SYM(__wrap_##name) ":\n\t" \
    "tst.b   " AR_SYM(flag) "\n\t"    /* a 68000's: jmp __real_ */ \
    "ble.s   9f\n\t" \
    fast \
    "9:\n\t" \
    "bmi.s   6f\n" \
    "8:\n\t" \
    "jmp     " AR_SYM(__real_##name) "\n" \
    "6:\n\t" \
    "pea     " AR_SYM(__wrap_##name) "(%pc)\n\t" \
    "bra     " ask "\n"
#define AR_WRAP32(name, fast) AR_WRAP(name, stdl_arith_l32, "3f", fast)
#define AR_WRAP64(name, fast) AR_WRAP(name, stdl_arith_l64, "4f", fast)
#define AR_JMP(name) \
    ".long   " AR_SYM(__real_##name) "," AR_SYM(__wrap_##name) "\n\t"

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
    "bvs.s   8f\n\t"
    "rts\n")

    AR_WRAP32(__umodsi3,
    "move.l  4(%sp),%d0\n\t"
    ".word   0x4C6F,0x0001,0x0008\n\t"      /* divul.l 8(sp),d1:d0 */
    "move.l  %d1,%d0\n\t"
    "rts\n")

    AR_WRAP32(__modsi3,
    "move.l  4(%sp),%d0\n\t"
    ".word   0x4C6F,0x0801,0x0008\n\t"      /* divsl.l 8(sp),d1:d0 */
    "bvs.s   8f\n\t"
    "move.l  %d1,%d0\n\t"
    "rts\n")

    /* long long __muldi3(long long a, long long b): a at 4(sp), its
     * high long first, b at 12(sp); the product's low 64 bits in
     * d0:d1, high long in d0. Where each high long is only its low
     * long's sign, as gcc's (long long)a * b has them, one muls.l of 32
     * bits by 32 is the product; else a_lo * b_lo in 64 bits, and the
     * two cross products' low longs added to its high long. */
    AR_WRAP64(__muldi3,
    "move.l  8(%sp),%d1\n\t"                /* a's low long */
    "smi     %d0\n\t"
    "ext.w   %d0\n\t"
    "ext.l   %d0\n\t"                       /* its sign, as a long */
    "cmp.l   4(%sp),%d0\n\t"                /* a's high long */
    "bne.s   7f\n\t"
    "move.l  16(%sp),%d0\n\t"               /* b's low long */
    "smi     %d0\n\t"
    "ext.w   %d0\n\t"
    "ext.l   %d0\n\t"
    "cmp.l   12(%sp),%d0\n\t"               /* b's high long */
    "bne.s   7f\n\t"
    ".word   0x4C2F,0x1C00,0x0010\n\t"      /* muls.l 16(sp),d0:d1 */
    "rts\n"
    "7:\n\t"
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
    "bne.s   8f\n\t"
    "move.l  %d2,%a0\n\t"
    "move.l  4(%sp),%d2\n\t"                /* a's high long */
    "move.l  8(%sp),%d0\n\t"                /* and its low */
    ".word   0x4C41,0x0C02\n\t"             /* divs.l d1,d2:d0 */
    "exg     %d2,%a0\n\t"                   /* d2 back, V kept */
    "bvs.s   8f\n\t"
    "move.l  %d0,%d1\n\t"
    "smi     %d0\n\t"
    "ext.w   %d0\n\t"
    "ext.l   %d0\n\t"                       /* the quotient's sign */
    "rts\n")

    /* 3, stdl_arith_l32 from the CPU: the index d0 = 1 scaled by 2 on
     * a 68020 and later, by 1 (the scale bits ignored) before. Returns
     * it in d0 too, Z set for 0. On a 68000 or 68010 stdl_arith_l64 is
     * 0 as well, and every wrapper's tst.b becomes a jmp to libgcc's
     * (5, the table): a long of jmp and the address's high word, and
     * one of its low word and the wrapper's ble.s, written by one
     * movem.l. */
    "3:\n\t"
    "moveq   #1,%d0\n\t"
    "suba.l  %a0,%a0\n\t"
    ".word   0x41F0,0x0A00\n\t"             /* lea (0,a0,d0.l*2),a0 */
    "move.l  %a0,%d0\n\t"
    "subq.b  #1,%d0\n\t"
    "move.b  %d0," AR_SYM(stdl_arith_l32) "\n\t"
    "bne.s   1f\n\t"                        /* a 68020 or later */
    "move.b  %d0," AR_SYM(stdl_arith_l64) "\n\t"
    "lea     5f(%pc),%a0\n"
    "2:\n\t"
    "move.l  (%a0)+,%d0\n\t"                /* libgcc's, 0 the end */
    "beq.s   1f\n\t"
    "move.l  (%a0)+,%a1\n\t"                /* the wrapper */
    "move.w  %d0,%d1\n\t"
    "swap    %d1\n\t"
    "move.w  6(%a1),%d1\n\t"                /* its ble.s, kept */
    "move.w  #0x4EF9,%d0\n\t"               /* jmp abs.l */
    "swap    %d0\n\t"
    "movem.l %d0-%d1,(%a1)\n\t"
    "bra.s   2b\n"
    "1:\n\t"
    "rts\n"

    /* 4, stdl_arith_l64: 0 on a 68000 or 68010 (3 says so, d0 0 and Z
     * set), else the _CPU cookie from 20 to 59 */
    "4:\n\t"
    "bsr.s   3b\n\t"
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

    /* 5, what 3 writes on a 68000: libgcc's routine and its wrapper */
    "\t.even\n"
    "5:\n\t"
    AR_JMP(__mulsi3) AR_JMP(__udivsi3) AR_JMP(__divsi3)
    AR_JMP(__umodsi3) AR_JMP(__modsi3) AR_JMP(__muldi3) AR_JMP(__divdi3)
    ".long   0\n"
);

#endif /* __m68k__ */
