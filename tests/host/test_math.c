/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/*
 * stdl_math.h against plain wide arithmetic: the 16-bit multiplies
 * and divides at their edges and over random operands, and
 * STDL_FixDiv's two-step division against a 64-bit (a << 16) / b.
 * Natively this checks the C twins; tests/hatari/mathchk.c runs the
 * same cases on the target, where the functions are the 68000's own
 * instructions.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdl/stdl_math.h>

#ifndef MATH_N
#define MATH_N 1000000L
#endif

static int failures;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        if (failures++ < 10) { \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__); \
            printf("\n"); \
        } \
    } \
} while (0)

/* shifts and xors only: no multiply in the checker on the target */
static uint32_t rs = 0x2545F491u;
static uint32_t rnd32(void)
{
    rs ^= rs << 13;
    rs ^= rs >> 17;
    rs ^= rs << 5;
    return rs;
}

static void check_mul(int a, int b)
{
    int32_t s = STDL_MulS16(a, b);
    uint32_t u = STDL_MulU16((unsigned)a, (unsigned)b);
    int32_t ws = (int32_t)(int16_t)a * (int32_t)(int16_t)b;
    uint32_t wu = (uint32_t)(uint16_t)a * (uint32_t)(uint16_t)b;

    CHECK(s == ws, "MulS16(%d, %d) %ld, want %ld", a, b, (long)s, (long)ws);
    CHECK(u == wu, "MulU16(%d, %d) %lu, want %lu", a, b,
          (unsigned long)u, (unsigned long)wu);
}

/* only quotients that fit are the functions' business */
static void check_div(int32_t n, int d)
{
    int32_t wq = n / (int16_t)d;

    if (wq >= -32768 && wq <= 32767) {
        int16_t q = STDL_DivS16(n, d);

        CHECK(q == wq, "DivS16(%ld, %d) %d, want %ld", (long)n, d, q,
              (long)wq);
    }
}

static void check_divu(uint32_t n, unsigned d)
{
    uint32_t wq = n / (uint16_t)d;

    if (wq <= 0xFFFF) {
        uint16_t q = STDL_DivU16(n, d);

        CHECK(q == wq, "DivU16(%lu, %u) %u, want %lu", (unsigned long)n,
              d, q, (unsigned long)wq);
    }
}

static void check_fix(int a, int b)
{
    int32_t f = STDL_FixDiv(a, b);
    int64_t w = ((int64_t)a * 65536) / b;

    CHECK(f == (int32_t)w, "FixDiv(%d, %d) %ld, want %ld", a, b,
          (long)f, (long)w);
}

static int16_t rnd_nz16(void)
{
    int16_t d;

    do {
        d = (int16_t)rnd32();
    } while (d == 0);
    return d;
}

int main(void)
{
    static const int edge[] = { 0, 1, -1, 2, -2, 255, 256, 32767,
                                -32767, -32768, 0x7FFF, 12345 };
    const int ne = (int)(sizeof edge / sizeof edge[0]);
    long i;
    int x, y;

    for (x = 0; x < ne; x++) {
        for (y = 0; y < ne; y++) {
            check_mul(edge[x], edge[y]);
            if (edge[y] != 0) {
                check_div(edge[x], edge[y]);
                check_divu((uint32_t)(uint16_t)edge[x],
                           (unsigned)(uint16_t)edge[y]);
                if (edge[x] != -32768 && edge[y] != -32768) {
                    check_fix(edge[x], edge[y]);
                }
            }
        }
    }
    /* operands passed as int but wider than 16 bits: truncated */
    CHECK(STDL_MulS16(0x10002, 3) == 6, "MulS16 does not truncate");
    CHECK(STDL_MulU16(0x1FFFF, 2) == 0x1FFFE, "MulU16 does not truncate");

    for (i = 0; i < MATH_N; i++) {
        uint32_t r = rnd32();
        int16_t a = (int16_t)r, b = (int16_t)(r >> 16);
        int16_t d = rnd_nz16();
        /* dividends of every size, most with quotients that fit */
        uint32_t n = rnd32() >> (8 + (r & 15));

        check_mul(a, b);
        check_div((r & 0x80) ? -(int32_t)n : (int32_t)n, d);
        check_divu(n, (uint16_t)d);
        if (a != -32768 && d != -32768) {
            check_fix(a, d);
        }
    }

    if (failures) {
        printf("test_math: %d failures\n", failures);
        return 1;
    }
    printf("test_math: ok\n");
    return 0;
}
