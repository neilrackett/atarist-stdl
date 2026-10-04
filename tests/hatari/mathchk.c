/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * stdl_math.h on the machine: tests/host/test_math.c's cases against
 * the 68000 instructions the functions are on target, then each one
 * timed against the C that gcc 4.6 turns into a libgcc call. The
 * header's cycle figures come from here.
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Iinclude -o dist/MATHCHK.TOS tests/hatari/mathchk.c libstdl.a
 *   MACHINE=st tests/hatari/run.sh mc dist/MATHCHK.TOS 8 "waitfor MC:done"
 */
#include <stdio.h>
#include <stdl/stdl.h>

#define MATH_N 20000L
#define main math_suite
#include "../host/test_math.c"
#undef main

#define REPS 20000

/* the operands come from memory each time, so neither form can be
 * hoisted out of the loop; the empty loop is the floor */
static volatile int va = 12345, vb = -321, vd = 77;
static volatile int32_t vn = 950000L;
static volatile int32_t sink;

static uint32_t t_floor(void)
{
    uint32_t t = STDL_GetTicks();
    int i;

    for (i = 0; i < REPS; i++) {
        sink = va + vb;
    }
    return STDL_GetTicks() - t;
}

#define TIMED(name, expr) static uint32_t name(void) { \
        uint32_t t = STDL_GetTicks(); \
        int i; \
        for (i = 0; i < REPS; i++) { \
            sink = (expr); \
        } \
        return STDL_GetTicks() - t; \
    }

TIMED(t_muls,   STDL_MulS16(va, vb))
TIMED(t_mulsi3, va * vb)
TIMED(t_divs,   STDL_DivS16(vn, vd))
TIMED(t_divsi3, vn / vd)
TIMED(t_fix,    STDL_FixDiv(va, vd))
TIMED(t_fixc,   ((int32_t)va << 16) / vd)

/* cycles a call at 8MHz, less the loop: ms * 8000 / REPS */
static void report(const char *what, uint32_t ms, uint32_t floor)
{
    uint32_t net = ms > floor ? ms - floor : 0;

    printf("MC: %-10s %5lu cycles\n", what,
           (unsigned long)(net * 8000UL / REPS));
}

int main(void)
{
    uint32_t fl;
    int r;

    if (STDL_Init(0) < 0) {
        fprintf(stderr, "init failed: %s\n", STDL_GetError());
        return 1;
    }
    r = math_suite();
    printf("MC: %s\n", r == 0 ? "PASS" : "FAIL");
    fflush(stdout);

    fl = t_floor();
    report("muls.w", t_muls(), fl);
    report("__mulsi3", t_mulsi3(), fl);
    report("divs.w", t_divs(), fl);
    report("__divsi3", t_divsi3(), fl);
    report("FixDiv", t_fix(), fl);
    report("C fixdiv", t_fixc(), fl);
    printf("MC:done %s\n", r == 0 ? "PASS" : "FAIL");
    fflush(stdout);
    STDL_Quit();
    return r;
}
