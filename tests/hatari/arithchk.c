/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * src/arith020.c on the machine (ARITHCHK): every wrapper against
 * libgcc's own routine, which the link names __real___mulsi3 and so on.
 * A host test cannot run 68020 instructions, so this is the check.
 *
 *  1. The CPU, asked: the two bytes start at -1 (not asked), and the
 *     first 32-bit call sets stdl_arith_l32 alone, to 1 where the _CPU
 *     cookie says 20 or more; the first 64-bit call sets
 *     stdl_arith_l64, to 1 where it says 20 to 59. Asked again in
 *     supervisor mode, as a program is after STDL_Init. What they were
 *     at main is printed too: the C library's start-up has divided by
 *     then.
 *  2. Values: each routine on every pair of an edge set (0, +-1,
 *     -2^31, -2^31 / -1, 16-bit boundaries; for 64 bits the 32-bit
 *     boundaries and +-2^63 as well) and on 5000 random pairs of
 *     varied magnitude and sign, against libgcc's answer with both
 *     bytes at 0 (so that libgcc's own inner calls, which the link
 *     wraps too, are libgcc's as well). On a 68020 or later twice: the
 *     68020's path, then the 68000's (the bytes at 0). __divdi3's line
 *     says how many cases took divs.l (a divisor and a quotient of 32
 *     bits); the rest went to libgcc.
 *  3. Registers: every wrapper called with d2-d7 and a2-a6 holding
 *     known values, on both paths and with the CPU not asked yet (the
 *     cookie's system call), which must keep them.
 *  4. Dividing by 0: libgcc's divu.w takes the zero-divide exception
 *     (vector 5), and so must the wrappers. A handler of the probe's
 *     own counts and returns; each divide by 0 must trap at least once
 *     on both (libgcc's 68000 path traps twice, its two divu.w).
 *  5. Time a call, in ns and cycles at the machine's clock, the empty
 *     call's subtracted: libgcc's own, the wrapper on the 68000's path
 *     (what the flags cost a 68000) and on the 68020's; on a 68030
 *     with the caches as found, then off.
 *
 * In Hatari at the machine's own speed, from boot to the end, 47s on
 * an 8MHz STE and 48s on a TT (which runs both paths, and the times
 * twice); about 12s of the STE's and 24s of the TT's are the times.
 * Ends with "AC:done", the count of checks and of wrong answers.
 *
 *   STCMD_NO_TTY=1 stcmd make libstdl.a libstdl-arith020.o
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 -Wall \
 *       -Wextra -o dist/ARITHCHK.TOS tests/hatari/arithchk.c \
 *       libstdl-arith020.o libstdl.a \
 *       -Wl,--wrap=__mulsi3 -Wl,--wrap=__udivsi3 -Wl,--wrap=__divsi3 \
 *       -Wl,--wrap=__umodsi3 -Wl,--wrap=__modsi3 -Wl,--wrap=__muldi3 \
 *       -Wl,--wrap=__divdi3
 *   MACHINE=tt TOS="<TOS 3.06 or a 512K EmuTOS>" \
 *       tests/hatari/run.sh ac_tt dist/ARITHCHK.TOS 8 "waitfor AC:done"
 *   MACHINE=falcon TOS=... tests/hatari/run.sh ac_falcon ...
 *   MACHINE=ste TOS=... tests/hatari/run.sh ac_ste ...
 *
 * (the object and the flags are arith020.mk's STDL_ARITH020). Built with
 * -DARITHCHK_PLAIN and without the flags it times libgcc's routines
 * alone, by their own names (ARITHCHP.TOS), for what a program
 * without the flags pays: with them, libgcc's own inner calls (its
 * __modsi3 calls __divsi3 and __mulsi3) go through the wrappers too:
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 -Wall \
 *       -Wextra -DARITHCHK_PLAIN -o dist/ARITHCHP.TOS \
 *       tests/hatari/arithchk.c
 */
#include <stdint.h>
#include <stdio.h>
#include <mint/osbind.h>
#include <mint/cookie.h>
#define HOLD_NO_STDL
#include "../../examples/hold.h"

typedef long long i64;
typedef unsigned long long u64;

#ifdef ARITHCHK_PLAIN
/* built without the flags: libgcc's routines by their own names,
 * timed alone, for what a program without the flags pays */
#define PAIR(r, n, t) r __##n(t, t);
#define REAL(n) __##n
#else
/* arith020.c's, which no header declares: only this probe sets them */
extern signed char stdl_arith_l32, stdl_arith_l64;

#define PAIR(r, n, t) \
    r __wrap___##n(t, t); \
    r __real___##n(t, t);
#define REAL(n) __real___##n
#endif
PAIR(long, mulsi3, long)
PAIR(unsigned long, udivsi3, unsigned long)
PAIR(long, divsi3, long)
PAIR(unsigned long, umodsi3, unsigned long)
PAIR(long, modsi3, long)
PAIR(i64, muldi3, i64)
PAIR(i64, divdi3, i64)

static long cpu;                /* the _CPU cookie, 0 without one */
static signed char m32, m64;    /* the path under test */
static FILE *out;

/* the two bytes, which say the path (-1 the CPU not asked yet) */
static void set_path(int p32, int p64)
{
#ifndef ARITHCHK_PLAIN
    stdl_arith_l32 = (signed char)p32;
    stdl_arith_l64 = (signed char)p64;
#else
    (void)p32;
    (void)p64;
#endif
}

#ifndef ARITHCHK_PLAIN
/* ---------------------------------------------------------------- */
/* Zero divides counted, and every wrapper called with the registers
 * it must keep holding known values. */

volatile long ac_zdiv;

long ac_regs(void *fn, long a, long b, long c, long d);

/* register REG still VAL, or bit BIT set in d0 */
#define AC_KEPT(reg, val, bit) \
    "cmp.l   #" val ",%" reg "\n\t" \
    "beq.s   1f\n\t" \
    "bset    #" bit ",%d0\n" \
    "1:\t"

__asm__(
    "\t.text\n\t.even\n"
    "_ac_zdiv_handler:\n\t"
    "addq.l  #1,_ac_zdiv\n\t"
    "rte\n"

    /* long ac_regs(fn, a, b, c, d): fn(a, b, c, d) with d2-d7 and
     * a2-a6 at known values; 0 if they come back, else a bit for each
     * one that did not (d2 bit 0 ... a6 bit 10) */
    "\t.globl _ac_regs\n"
    "_ac_regs:\n\t"
    "movem.l %d2-%d7/%a2-%a6,-(%sp)\n\t"   /* 44 bytes */
    "move.l  48(%sp),%a0\n\t"
    "move.l  64(%sp),-(%sp)\n\t"
    "move.l  64(%sp),-(%sp)\n\t"
    "move.l  64(%sp),-(%sp)\n\t"
    "move.l  64(%sp),-(%sp)\n\t"
    "move.l  #0x2D2D2D2D,%d2\n\t"
    "move.l  #0x3D3D3D3D,%d3\n\t"
    "move.l  #0x4D4D4D4D,%d4\n\t"
    "move.l  #0x5D5D5D5D,%d5\n\t"
    "move.l  #0x6D6D6D6D,%d6\n\t"
    "move.l  #0x7D7D7D7D,%d7\n\t"
    "move.l  #0x2A2A2A2A,%a2\n\t"
    "move.l  #0x3A3A3A3A,%a3\n\t"
    "move.l  #0x4A4A4A4A,%a4\n\t"
    "move.l  #0x5A5A5A5A,%a5\n\t"
    "move.l  #0x6A6A6A6A,%a6\n\t"
    "jsr     (%a0)\n\t"
    "lea     16(%sp),%sp\n\t"
    "moveq   #0,%d0\n\t"
    AC_KEPT("d2", "0x2D2D2D2D", "0") AC_KEPT("d3", "0x3D3D3D3D", "1")
    AC_KEPT("d4", "0x4D4D4D4D", "2") AC_KEPT("d5", "0x5D5D5D5D", "3")
    AC_KEPT("d6", "0x6D6D6D6D", "4") AC_KEPT("d7", "0x7D7D7D7D", "5")
    AC_KEPT("a2", "0x2A2A2A2A", "6") AC_KEPT("a3", "0x3A3A3A3A", "7")
    AC_KEPT("a4", "0x4A4A4A4A", "8") AC_KEPT("a5", "0x5A5A5A5A", "9")
    AC_KEPT("a6", "0x6A6A6A6A", "10")
    "movem.l (%sp)+,%d2-%d7/%a2-%a6\n\t"
    "rts\n"
);
extern void ac_zdiv_handler(void);

/* ---------------------------------------------------------------- */

static long checks, wrong;

/* a shift-register generator: no multiply, which would be the thing
 * under test */
static uint32_t rs = 2463534242UL;
static uint32_t rnd(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 17;
    rs ^= rs << 5;
    return rs;
}

static const uint32_t edge32[] = {
    0, 1, 0xFFFFFFFFUL, 2, 0xFFFFFFFEUL, 3, 7, 100000,
    0x7FFF, 0x8000, 0xFFFF, 0x10000, 0x10001, 0xFFFF0000UL,
    0x7FFFFFFFUL, 0x80000000UL, 0x80000001UL,
    0x12345678UL, 0xEDCBA988UL, 0xFFFF8000UL
};
#define N32 (int)(sizeof(edge32) / sizeof(edge32[0]))

static const u64 edge64[] = {
    0, 1, 0xFFFFFFFFFFFFFFFFULL, 2, 0xFFFFFFFFFFFFFFFEULL, 7,
    0x7FFFFFFFULL, 0x80000000ULL, 0xFFFFFFFF80000000ULL,
    0xFFFFFFFF7FFFFFFFULL, 0xFFFFFFFFULL, 0x100000000ULL,
    0xFFFFFFFF00000000ULL, 0x7FFFFFFFFFFFFFFFULL,
    0x8000000000000000ULL, 0x8000000000000001ULL,
    0x7FFFFFFF00000000ULL, 0x123456789ABCDEF0ULL,
    0x4000000000000000ULL, 0x00000000FFFF0000ULL
};
#define N64 (int)(sizeof(edge64) / sizeof(edge64[0]))

/* magnitudes from 1 bit to 32, either sign */
static uint32_t val32(void)
{
    uint32_t r = rnd() >> (rnd() & 31);

    return (rnd() & 1) ? (uint32_t)-(int32_t)r : r;
}

/* a 32-bit value widened, one shifted up, or 64 random bits shifted
 * down: quotients that fit 32 bits and quotients that do not */
static i64 val64(void)
{
    uint32_t k = rnd();
    i64 v;

    switch (k % 3) {
    case 0:
        return (i64)(int32_t)val32();
    case 1:
        return (i64)(int32_t)val32() << ((k >> 8) & 31);
    default:
        v = (i64)(((u64)rnd() << 32) | rnd());
        return v >> ((k >> 8) & 63);
    }
}

static void bad(const char *what, u64 a, u64 b, u64 got, u64 want)
{
    wrong++;
    if (wrong <= 40) {
        fprintf(out, "AC: WRONG %s path %d: %s(%08lx%08lx, %08lx%08lx)"
                " = %08lx%08lx, libgcc %08lx%08lx\n", what, m32,
                what, (unsigned long)(a >> 32), (unsigned long)a,
                (unsigned long)(b >> 32), (unsigned long)b,
                (unsigned long)(got >> 32), (unsigned long)got,
                (unsigned long)(want >> 32), (unsigned long)want);
    }
}

static long divdi3_native;      /* cases divs.l could take */

static void check32(uint32_t a, uint32_t b)
{
    uint32_t x, y;

#define CHECK32(n, t) \
    x = (uint32_t)__wrap___##n((t)a, (t)b); \
    set_path(0, 0); \
    y = (uint32_t)__real___##n((t)a, (t)b); \
    set_path(m32, m64); \
    checks++; \
    if (x != y) bad(#n, a, b, x, y);

    CHECK32(mulsi3, long)
    if (b == 0) {
        return;
    }
    CHECK32(udivsi3, unsigned long)
    CHECK32(umodsi3, unsigned long)
    CHECK32(divsi3, long)
    CHECK32(modsi3, long)
}

static void check64(i64 a, i64 b)
{
    i64 x, y;

    x = __wrap___muldi3(a, b);
    set_path(0, 0);
    y = __real___muldi3(a, b);
    set_path(m32, m64);
    checks++;
    if (x != y) {
        bad("muldi3", a, b, x, y);
    }
    if (b == 0) {
        return;
    }
    x = __wrap___divdi3(a, b);
    set_path(0, 0);
    y = __real___divdi3(a, b);
    set_path(m32, m64);
    checks++;
    if (x != y) {
        bad("divdi3", a, b, x, y);
    }
    if (b == (int32_t)b && y == (int32_t)y
        && !(a == (i64)0x8000000000000000ULL && b == -1)) {
        divdi3_native++;
    }
}

static void values(const char *path, long n)
{
    long before = wrong, c0 = checks, i;
    int p, q;

    divdi3_native = 0;
    set_path(m32, m64);
    for (p = 0; p < N32; p++) {
        for (q = 0; q < N32; q++) {
            check32(edge32[p], edge32[q]);
        }
    }
    for (p = 0; p < N64; p++) {
        for (q = 0; q < N64; q++) {
            check64((i64)edge64[p], (i64)edge64[q]);
        }
    }
    for (i = 0; i < n; i++) {
        check32(val32(), val32());
        /* a 32-bit divisor half the time, as a fixed-point scale has */
        check64(val64(), (rnd() & 1) ? (i64)(int32_t)val32() : val64());
    }
    fprintf(out, "AC: values, %s path: %ld checks, %ld wrong"
            " (divdi3: %ld of them a 32-bit divisor and quotient)\n",
            path, checks - c0, wrong - before, divdi3_native);
}

/* ---------------------------------------------------------------- */

typedef long (*fn_t)(long, long);

static const struct {
    const char *name;
    void *fn;
    int wide;
} wrappers[] = {
    { "mulsi3", (void *)__wrap___mulsi3, 0 },
    { "udivsi3", (void *)__wrap___udivsi3, 0 },
    { "divsi3", (void *)__wrap___divsi3, 0 },
    { "umodsi3", (void *)__wrap___umodsi3, 0 },
    { "modsi3", (void *)__wrap___modsi3, 0 },
    { "muldi3", (void *)__wrap___muldi3, 1 },
    { "divdi3", (void *)__wrap___divdi3, 1 },
};
#define NWRAP (int)(sizeof(wrappers) / sizeof(wrappers[0]))

/* the registers kept: with the CPU not asked yet, then each path */
static void registers(void)
{
    static const long args[][4] = {
        /* 32 bits: a, b; 64: a and b, each its high long first;
         * no divisor 0, which TOS would answer with bombs */
        { 950000, 77, 0, 77 },
        { 0x80000000L, -1, -1, -1 },    /* the overflows */
        { 0, 0x12345678L, 0, 0x98765L },
        { 0x7FFFFFFFL, -1, 0, 3 },      /* a quotient past 32 bits */
        { -1, -0x2A51F3L, -1, -0x13C77L },
    };
    int w, k, mode;
    long r;

    for (mode = -1; mode <= 1; mode++) {
        for (w = 0; w < NWRAP; w++) {
            for (k = 0; k < 5; k++) {
                if (mode < 0) {
                    set_path(-1, -1);
                } else {
                    set_path(mode && m32, mode && m64);
                }
                r = ac_regs(wrappers[w].fn, args[k][0], args[k][1],
                            args[k][2], args[k][3]);
                checks++;
                if (r != 0) {
                    wrong++;
                    fprintf(out, "AC: WRONG %s lost registers %03lx"
                            " (bytes %d)\n", wrappers[w].name, r, mode);
                }
            }
        }
    }
    fprintf(out, "AC: registers kept by every wrapper, the CPU not asked"
            " yet and on each path\n");
}

/* ---------------------------------------------------------------- */

static int asked(int want32, int want64, const char *when)
{
    int ok = stdl_arith_l32 == want32 && stdl_arith_l64 == want64;

    checks++;
    if (!ok) {
        wrong++;
    }
    fprintf(out, "AC: %s: l32 %d l64 %d, want %d %d%s\n", when,
            stdl_arith_l32, stdl_arith_l64, want32, want64,
            ok ? "" : "  WRONG");
    return ok;
}

static void cpu_asked(void)
{
    int w32 = cpu >= 20, w64 = cpu >= 20 && cpu < 60;
    volatile long a = 3, b = 5;
    volatile i64 c = 7;
    void *ssp;

    set_path(-1, -1);
    checks++;
    if (__wrap___mulsi3(a, b) != 15) {
        wrong++;
    }
    asked(w32, -1, "after a 32-bit call, not asked before");

    set_path(-1, -1);
    checks++;
    if (__wrap___muldi3(c, c) != 49) {
        wrong++;
    }
    asked(w32, w64, "after a 64-bit call, not asked before");

    set_path(-1, -1);
    ssp = (void *)Super(0L);
    c = __wrap___divdi3(c * 1000, 7);
    Super(ssp);
    checks++;
    if (c != 1000) {
        wrong++;
    }
    asked(w32, w64, "after a 64-bit call in supervisor mode");
}

/* every divide by 0 must trap, as libgcc's does */
static void zero_divides(void)
{
    static const long tops[] = { 0, 1, -1, 0x7FFFFFFFL, 0x80000000L };
    void (*old)(void) = Setexc(5, ac_zdiv_handler);
    long traps_w = 0, traps_g = 0, calls = 0;
    int k, missed = 0;

/* a divide by 0, its traps added to acc */
#define ZD(call, acc) do { \
        long t; \
        ac_zdiv = 0; \
        (void)(call); \
        t = ac_zdiv; \
        calls++; \
        missed += t == 0; \
        acc += t; \
    } while (0)

    set_path(m32, m64);
    for (k = 0; k < 5; k++) {
        long a = tops[k];

        ZD(__wrap___udivsi3(a, 0), traps_w);
        ZD(__wrap___umodsi3(a, 0), traps_w);
        ZD(__wrap___divsi3(a, 0), traps_w);
        ZD(__wrap___modsi3(a, 0), traps_w);
        ZD(__wrap___divdi3(a, 0), traps_w);
        set_path(0, 0);
        ZD(__real___udivsi3(a, 0), traps_g);
        ZD(__real___umodsi3(a, 0), traps_g);
        ZD(__real___divsi3(a, 0), traps_g);
        ZD(__real___modsi3(a, 0), traps_g);
        ZD(__real___divdi3(a, 0), traps_g);
        set_path(m32, m64);
    }
    (void)Setexc(5, old);
    checks += calls;
    wrong += missed;
    fprintf(out, "AC: dividing by 0, path %d: %ld calls, %d without a"
            " trap; %ld traps in the wrappers, %ld in libgcc's%s\n",
            m32, calls, missed, traps_w, traps_g, missed ? "  WRONG" : "");
}

#endif /* !ARITHCHK_PLAIN */

/* ---------------------------------------------------------------- */
/* Time a call: calls made in 100 ticks of the 200Hz clock, in
 * supervisor mode, read here. One loop calls every routine, through a
 * pointer, so that its own place in a 68030's 256-byte instruction
 * cache is the same for all of them: a loop of its own for each one
 * measured the empty call slower than a multiply, the loops' and the
 * routines' cache lines falling on one another as the link placed
 * them. On a 68030 the caches are then turned off for a second pass,
 * where the place no longer matters, and put back. */

#define HZ200 (*(volatile uint32_t *)0x4BAUL)
#define TICKS 100

static volatile long va, vb;
static volatile i64 wa, wb;
static volatile long sink;
static volatile i64 wsink;

static long empty(long a, long b) __attribute__((noinline));
static long empty(long a, long b)
{
    __asm__ __volatile__("" : : "r"(a), "r"(b));
    return a;
}
static i64 empty64(i64 a, i64 b) __attribute__((noinline));
static i64 empty64(i64 a, i64 b)
{
    __asm__ __volatile__("" : : "r"(a), "r"(b));
    return a;
}

static void tick(void)
{
    uint32_t t0 = HZ200;

    while (HZ200 == t0) {
    }
}

static uint32_t rate32(long (*f)(long, long))
{
    uint32_t n = 0, t0;
    int i;

    tick();
    t0 = HZ200;
    while (HZ200 - t0 < TICKS) {
        for (i = 0; i < 4; i++) {
            sink = f(va, vb);
        }
        n += 4;
    }
    return n;
}

static uint32_t rate64(i64 (*f)(i64, i64))
{
    uint32_t n = 0, t0;
    int i;

    tick();
    t0 = HZ200;
    while (HZ200 - t0 < TICKS) {
        for (i = 0; i < 4; i++) {
            wsink = f(wa, wb);
        }
        n += 4;
    }
    return n;
}

static long mhz;

/* ps a call, the empty call's taken off: TICKS / 200 seconds over n */
static long ps(uint32_t n, uint32_t floor)
{
    return (long)(500000000000ULL * TICKS / 100 / n
                  - 500000000000ULL * TICKS / 100 / floor);
}

/* ns to one place, for "%ld.%01ld" */
#define NS(t) (t) / 1000, ((t) < 0 ? -(t) : (t)) % 1000 / 100

static long cyc(long t)
{
    return (long)((i64)t * mhz / 1000000);
}

static uint32_t rate(int wide, void *f)
{
    return wide ? rate64((i64 (*)(i64, i64))f)
                : rate32((long (*)(long, long))f);
}

static void timed(const char *what, void *g, void *w, int wide,
                  long a_hi, long a, long b_hi, long b)
{
    uint32_t floor;
    long tg, t0, t2 = -1;

    va = a; vb = b;
    wa = (i64)(((u64)a_hi << 32) | (uint32_t)a);
    wb = (i64)(((u64)b_hi << 32) | (uint32_t)b);
    set_path(0, 0);
    floor = wide ? rate64(empty64) : rate32(empty);
    tg = ps(rate(wide, g), floor);
    if (w == NULL) {
        fprintf(out, "AC: %-22s libgcc, no flags %6ld.%01ld ns %5ld cyc\n",
                what, NS(tg), cyc(tg));
        return;
    }
    t0 = ps(rate(wide, w), floor);
    if (wide ? m64 : m32) {
        set_path(m32, m64);
        t2 = ps(rate(wide, w), floor);
    }
    /* ns, and cycles at the machine's clock */
    fprintf(out, "AC: %-22s libgcc %6ld.%01ld ns %5ld cyc"
            " | 68000 path %6ld.%01ld ns %5ld cyc", what,
            NS(tg), cyc(tg), NS(t0), cyc(t0));
    if (t2 != -1) {
        long x10 = t2 > 0 ? (long)((i64)tg * 10 / t2) : 0;

        fprintf(out, " | 68020 %5ld.%01ld ns %4ld cyc, %ld.%ldx",
                NS(t2), cyc(t2), x10 / 10, x10 % 10);
    }
    fprintf(out, "\n");
}

#ifdef ARITHCHK_PLAIN
#define G(n) (void *)REAL(n), NULL
#else
#define G(n) (void *)REAL(n), (void *)__wrap___##n
#endif

static void timing_pass(void)
{
    timed("mulsi3 12345*-321", G(mulsi3), 0, 0, 12345, 0, -321);
    timed("udivsi3 950000/77", G(udivsi3), 0, 0, 950000, 0, 77);
    timed("udivsi3 2^31-1/100000", G(udivsi3), 0, 0, 0x7FFFFFFFL,
          0, 100000);
    timed("divsi3 -950000/77", G(divsi3), 0, 0, -950000, 0, 77);
    timed("umodsi3 950000%77", G(umodsi3), 0, 0, 950000, 0, 77);
    timed("modsi3 -950000%77", G(modsi3), 0, 0, -950000, 0, 77);
    timed("muldi3 32x32 bits", G(muldi3), 1, -1, -0x2A51F3L, 0, 0x13C77L);
    /* 123456789 * 1000 / 98765 */
    timed("divdi3 64/32 bits", G(divdi3), 1, 0x1CL, (long)0xBE991A08UL,
          0, 98765);
}

static uint32_t read_cacr(void)
{
    uint32_t v;

    __asm__ volatile(".word 0x4e7a,0x0002\n\tmove.l %%d0,%0"
                     : "=d"(v) : : "d0");          /* movec cacr,d0 */
    return v;
}

static void write_cacr(uint32_t v)
{
    __asm__ volatile("move.l %0,%%d0\n\t.word 0x4e7b,0x0002"
                     : : "d"(v) : "d0");           /* movec d0,cacr */
}

static void timing(void)
{
    void *ssp = (void *)Super(0L);

    fprintf(out, "AC: a call, the empty call's taken off; caches as"
            " found\n");
    timing_pass();
    if (cpu == 30) {
        uint32_t cacr = read_cacr();

        write_cacr(0x0808);             /* both off, and emptied */
        fprintf(out, "AC: caches off (CACR was %08lx)\n",
                (unsigned long)cacr);
        timing_pass();
        write_cacr(cacr | 0x0808);
    }
    set_path(m32, m64);
    Super(ssp);
}

/* ---------------------------------------------------------------- */

int main(void)
{
    long mch = 0;
#ifndef ARITHCHK_PLAIN
    int a32 = stdl_arith_l32, a64 = stdl_arith_l64;
#endif

    hold_on_exit("ARITHCHK");
    out = stdout;
    if (Getcookie(C__CPU, &cpu) != C_FOUND) {
        cpu = 0;
    }
    if (Getcookie(C__MCH, &mch) != C_FOUND) {
        mch = 0;
    }
    mhz = (mch >> 16) == 2 ? 32 : (mch >> 16) == 3 ? 16 : 8;
#ifdef ARITHCHK_PLAIN
    fprintf(out, "AC: _CPU %ld, _MCH %08lx (cycles at %ldMHz);"
            " built without the flags\n", cpu, mch, mhz);
    timing();
    fprintf(out, "AC:done\n");
    return 0;
#else
    fprintf(out, "AC: _CPU %ld, _MCH %08lx (cycles at %ldMHz);"
            " at main l32 %d l64 %d\n", cpu, mch, mhz, a32, a64);

    cpu_asked();
    m32 = (signed char)(cpu >= 20);
    m64 = (signed char)(cpu >= 20 && cpu < 60);

    values(m32 ? "68020" : "68000", 5000);
    registers();
    zero_divides();
    if (m32) {
        signed char s32 = m32, s64 = m64;

        m32 = m64 = 0;
        values("68000", 5000);
        registers();
        zero_divides();
        m32 = s32;
        m64 = s64;
    }
    set_path(m32, m64);
    timing();
    fprintf(out, "AC: %ld checks, %ld wrong\n", checks, wrong);
    fprintf(out, "AC:done\n");
    return wrong != 0;
#endif
}
