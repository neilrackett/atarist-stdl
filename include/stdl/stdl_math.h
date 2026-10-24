/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * 16-bit multiply and divide on the 68000's own instructions.
 */

#ifndef STDL_MATH_H
#define STDL_MATH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The 68000 multiplies 16 bits by 16 and divides 32 by 16; anything
 * wider is a call into libgcc. gcc 4.6 picks the instruction only
 * when it can see, at the point it chooses, that both operands are
 * 16-bit values of the same signedness - and several things hide
 * that from it:
 *
 *  - Mixed widths or signedness: uint8_t * uint8_t, int16_t * int,
 *    int16_t * uint16_t and uint16_t * uint8_t all call __mulsi3.
 *  - Loops: -O2's induction-variable pass (ivopts) rewrites a value
 *    derived from the loop counter as a fresh 32-bit variable, so
 *    `(int32_t)(int16_t)dx * (int16_t)dx` with dx = x - 80 calls
 *    __mulsi3 inside the loop and compiles to muls.w outside it.
 *    Declaring the variables int16_t does not help.
 *  - -O1: the pass that finds widening multiplies runs only at -O2
 *    and -Os, so at -O1 every product is a call.
 *  - Division by anything but a power of two, a constant included:
 *    `x / 12` on an int calls __divsi3, and `%` __modsi3. (A short
 *    divided by a constant becomes a muls.w by its reciprocal.)
 *
 * Measured on a plain ST (tests/hatari/mathchk.c), a call to
 * __mulsi3 costs about 350 cycles against 40 for muls.w, and
 * __divsi3 about 630 against 140 for divs.w. The functions here are
 * the instruction itself, whatever surrounds them - use them in any
 * loop that runs per pixel, per object or per frame. A product with
 * a constant can stay plain C (`y * 160` compiles to shifts and adds
 * for an int, a muls.w #160 for an int16_t), and tools/libcalls.sh
 * lists, per function, every call an
 * object file makes to libgcc's arithmetic, which is how to find the
 * cases nobody wrote down.
 *
 * Operands are truncated to 16 bits. A divide whose quotient does
 * not fit the result leaves it meaningless (the 68000 sets overflow
 * and does not store), and dividing by zero takes the CPU's
 * divide-by-zero exception: those are the caller's to rule out.
 */

#ifdef __m68k__

/* a * b, both as int16_t */
static __inline__ int32_t STDL_MulS16(int a, int b)
{
    int32_t r = a;

    __asm__("muls.w %1,%0" : "+d"(r) : "dmi"((int16_t)b));
    return r;
}

/* a * b, both as uint16_t */
static __inline__ uint32_t STDL_MulU16(unsigned a, unsigned b)
{
    uint32_t r = a;

    __asm__("mulu.w %1,%0" : "+d"(r) : "dmi"((uint16_t)b));
    return r;
}

/* a / b rounded toward zero, as C does; the quotient must fit
 * int16_t */
static __inline__ int16_t STDL_DivS16(int32_t a, int b)
{
    __asm__("divs.w %1,%0" : "+d"(a) : "dmi"((int16_t)b));
    return (int16_t)a;
}

/* a / b; the quotient must fit uint16_t */
static __inline__ uint16_t STDL_DivU16(uint32_t a, unsigned b)
{
    __asm__("divu.w %1,%0" : "+d"(a) : "dmi"((uint16_t)b));
    return (uint16_t)a;
}

#else

static __inline__ int32_t STDL_MulS16(int a, int b)
{
    return (int32_t)(int16_t)a * (int16_t)b;
}

static __inline__ uint32_t STDL_MulU16(unsigned a, unsigned b)
{
    return (uint32_t)(uint16_t)a * (uint16_t)b;
}

static __inline__ int16_t STDL_DivS16(int32_t a, int b)
{
    return (int16_t)(a / (int16_t)b);
}

static __inline__ uint16_t STDL_DivU16(uint32_t a, unsigned b)
{
    return (uint16_t)(a / (uint16_t)b);
}

#endif

/*
 * (a << 16) / b, a 16.16 fixed-point ratio: a polygon edge's slope,
 * a texture step. a and b are -32767..32767 and b is not zero;
 * rounded toward zero, as C does. Two divu.w on the magnitudes, the
 * second dividing the first's remainder: about 330 cycles, where
 * `((int32_t)a << 16) / b` is a __divsi3 call of about 630.
 */
static __inline__ int32_t STDL_FixDiv(int a, int b)
{
    uint32_t t, lo;

    /* the divisor's sign into the dividend, so one sign is left to
     * handle - each test and negate was showing in a polygon's 72
     * slopes a frame */
    if (b < 0) {
        a = -a;
        b = -b;
    }
    t = (uint32_t)(a < 0 ? -a : a);
#ifdef __m68k__
    /* t = remainder:whole; the remainder's sixteen bits below the
     * point, then whole:fraction */
    __asm__("divu.w %1,%0" : "+d"(t) : "d"((uint16_t)b));
    lo = t & 0xFFFF0000u;
    __asm__("divu.w %1,%0" : "+d"(lo) : "d"((uint16_t)b));
    t = (t << 16) | (lo & 0xFFFFu);
#else
    lo = ((t % (uint32_t)b) << 16) / (uint32_t)b;
    t = ((t / (uint32_t)b) << 16) | lo;
#endif
    return a < 0 ? -(int32_t)t : (int32_t)t;
}

/*
 * a * b >> 16 for two 16.16 values: the 64-bit product shifted down
 * and truncated to 32 bits, rounding toward minus infinity - exactly
 * (int32_t)(((int64_t)a * b) >> 16), which is PICO-8's multiply.
 * Any a and b; a product past 32 bits wraps as that expression does.
 * Four mulu.w of the halves, with the sign put right in the high
 * word: 324 cycles on a plain ST, where the C line is a __muldi3
 * call of 1404 (tests/hatari/mathchk.c).
 */
static __inline__ int32_t STDL_FixMul(int32_t a, int32_t b)
{
#ifdef __m68k__
    /* unsigned, bits 16-47 of a * b are
     *   (al bl >> 16) + al bh + ah bl + (ah bh << 16),
     * and a signed operand subtracts the other one << 32, which is
     * the other's low word from the result's high word */
    uint32_t r, t, u;

    __asm__(
        "move.w %[a],%[r]\n\t"
        "mulu.w %[b],%[r]\n\t"      /* al bl */
        "clr.w  %[r]\n\t"
        "swap   %[r]\n\t"           /* >> 16 */
        "move.l %[b],%[t]\n\t"
        "swap   %[t]\n\t"
        "mulu.w %[a],%[t]\n\t"      /* al bh */
        "add.l  %[t],%[r]\n\t"
        "move.l %[a],%[t]\n\t"
        "swap   %[t]\n\t"
        "move.w %[t],%[u]\n\t"      /* ah */
        "mulu.w %[b],%[t]\n\t"      /* ah bl */
        "add.l  %[t],%[r]\n\t"
        "move.l %[b],%[t]\n\t"
        "swap   %[t]\n\t"           /* bh */
        "mulu.w %[t],%[u]\n\t"      /* ah bh: its low word counts */
        "tst.l  %[a]\n\t"
        "bpl.s  1f\n\t"
        "sub.w  %[b],%[u]\n"        /* a < 0: less bl */
        "1:\n\t"
        "tst.l  %[b]\n\t"
        "bpl.s  2f\n\t"
        "sub.w  %[a],%[u]\n"        /* b < 0: less al */
        "2:\n\t"
        "swap   %[r]\n\t"
        "add.w  %[u],%[r]\n\t"
        "swap   %[r]"
        : [r] "=&d"(r), [t] "=&d"(t), [u] "=&d"(u)
        : [a] "d"(a), [b] "d"(b)
        : "cc");
    return (int32_t)r;
#else
    return (int32_t)(((int64_t)a * b) >> 16);
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* STDL_MATH_H */
