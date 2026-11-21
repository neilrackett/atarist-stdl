/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: CC0-1.0
 *
 * MULDIV: a 68000 program's 32-bit multiplies and divides on a 68020's
 * own instructions where the machine has one, from the link alone
 * (arith020.mk's STDL_ARITH020, docs/building.md): the C is ordinary
 * x * fov / z on longs, so a port runs as it did on every machine and
 * faster on the ones that can. It projects 64 points over and over for
 * a second, then for another through libgcc's own routines by name
 * (__real___mulsi3 and __real___divsi3, which only a program linked
 * with the flags has), and prints the two rates.
 */

#include <stdio.h>
#include <time.h>
#define HOLD_NO_STDL
#include "hold.h"

/* libgcc's own: only a program linked with the flags has these names */
long __real___mulsi3(long a, long b);
long __real___divsi3(long a, long b);

#define POINTS 64

static long px[POINTS], py[POINTS], pz[POINTS];
static volatile long fov = 160, sink;

static long project(void)
{
    long sum = 0;
    int i;

    for (i = 0; i < POINTS; i++) {
        sum += px[i] * fov / pz[i] + py[i] * fov / pz[i];
    }
    return sum;
}

static long project_libgcc(void)
{
    long sum = 0;
    int i;

    for (i = 0; i < POINTS; i++) {
        sum += __real___divsi3(__real___mulsi3(px[i], fov), pz[i])
             + __real___divsi3(__real___mulsi3(py[i], fov), pz[i]);
    }
    return sum;
}

/* points projected in a second */
static long rate(long (*f)(void))
{
    clock_t t0 = clock();
    long n = 0;

    while (clock() == t0) {
    }
    t0 = clock();
    while ((long)(clock() - t0) < (long)CLOCKS_PER_SEC) {
        sink = f();
        n += POINTS * 2;
    }
    return n;
}

int main(void)
{
    unsigned long r = 2463534242UL;
    int i;

    hold_on_exit(NULL);
    for (i = 0; i < POINTS; i++) {      /* a shift-register generator */
        r ^= r << 13;
        r ^= r >> 17;
        r ^= r << 5;
        px[i] = (long)(r & 0x3FFFF) - 0x20000;
        py[i] = (long)((r >> 8) & 0x3FFFF) - 0x20000;
        pz[i] = (long)((r >> 14) & 0x3FFFF) + 256;
    }
    if (project() != project_libgcc()) {
        printf("The two ways disagree!\n");
        return 1;
    }
    printf("Projections a second (x * fov / z, 32 bits):\n");
    printf("  %7ld as compiled, linked with STDL_ARITH020\n",
           rate(project));
    printf("  %7ld through libgcc's own routines\n",
           rate(project_libgcc));
    return 0;
}
