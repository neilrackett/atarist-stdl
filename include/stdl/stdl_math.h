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
 * second dividing the first's remainder: about 420 cycles, where
 * `((int32_t)a << 16) / b` is a __divsi3 call of about 630.
 */
static __inline__ int32_t STDL_FixDiv(int a, int b)
{
    int16_t sa = (int16_t)a, sb = (int16_t)b;
    uint16_t ua = (uint16_t)(sa < 0 ? -sa : sa);
    uint16_t ub = (uint16_t)(sb < 0 ? -sb : sb);
    uint32_t q;
#ifdef __m68k__
    uint32_t t = ua;

    /* t = remainder:whole, then q = whole, then the remainder's
     * sixteen bits below the point */
    __asm__("divu.w %2,%1\n\t"
            "move.w %1,%0\n\t"
            "swap %0\n\t"
            "clr.w %1\n\t"
            "divu.w %2,%1\n\t"
            "move.w %1,%0"
            : "=&d"(q), "+d"(t) : "d"(ub));
#else
    q = ((uint32_t)(ua / ub) << 16) | ((uint32_t)(ua % ub) << 16) / ub;
#endif
    return (sa < 0) != (sb < 0) ? -(int32_t)q : (int32_t)q;
}

#ifdef __cplusplus
}
#endif

#endif /* STDL_MATH_H */
