/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The conversion's C (src/c2p.c) against a pixel-by-pixel reference:
 * every border index X, one line and two, odd source addresses and
 * pitches, and a page filled with a pattern beforehand, so a byte
 * written outside the groups asked for fails as surely as a wrong one.
 */

#include "c2p.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long seed = 12345;

static unsigned rnd(void)
{
    seed = seed * 1103515245UL + 12345UL;
    return (unsigned)(seed >> 16) & 0x7FFF;
}

/* pixel i of a group into its 16 bytes: plane p is the big-endian
 * word at 2p, pixel 0 its top bit */
static void put(uint8_t *grp, int i, unsigned v)
{
    int p;
    for (p = 0; p < 8; p++) {
        uint8_t *w = grp + 2 * p + (i >> 3);
        uint8_t bit = (uint8_t)(0x80 >> (i & 7));
        if (v & (1u << p)) {
            *w |= bit;
        } else {
            *w &= (uint8_t)~bit;
        }
    }
}

static void reference(const uint8_t *src, long sp, uint8_t *dst, long dp,
                      long groups, long h, int dbl, unsigned x)
{
    long r, g;
    int i;
    for (r = 0; r < h; r++) {
        uint8_t *line = dst + r * (dbl ? 2 * dp : dp);
        for (g = 0; g < groups; g++) {
            for (i = 0; i < 16; i++) {
                unsigned v = src[r * sp + g * 16 + i] ^ x;
                put(line + g * 16, i, v);
                if (dbl) {
                    put(line + dp + g * 16, i, v);
                }
            }
        }
    }
}

int main(void)
{
    enum { SRCMAX = 16 * 1024, DSTMAX = 32 * 1024 };
    uint8_t *src = malloc(SRCMAX);
    uint8_t *got = malloc(DSTMAX);
    uint8_t *want = malloc(DSTMAX);
    int runs = 0, failures = 0;
    unsigned x;
    int t;

    for (x = 0; x < 256; x += 16) {
        for (t = 0; t < 200; t++) {
            uint32_t x2, x0;
            long groups = 1 + rnd() % 9;
            long h = 1 + rnd() % 6;
            long off = rnd() % 7;                 /* odd sources */
            long sp = groups * 16 + rnd() % 23;   /* odd pitches */
            long dp = groups * 16 + 16 * (rnd() % 3) + 2 * (rnd() % 2);
            long dsto = 16 * (rnd() % 4);
            int dbl = rnd() & 1;
            long i;
            for (i = 0; i < SRCMAX; i++) {
                src[i] = (uint8_t)rnd();
            }
            memset(got, 0xA5, DSTMAX);
            memset(want, 0xA5, DSTMAX);
            stdl_sdl2_c2p_xor(x, &x2, &x0);
            stdl_sdl2_c2p_c(src + off, sp, got + dsto, dp, groups, h, dbl,
                            x2, x0);
            reference(src + off, sp, want + dsto, dp, groups, h, dbl, x);
            runs++;
            if (memcmp(got, want, DSTMAX) != 0) {
                long at = 0;
                while (got[at] == want[at]) {
                    at++;
                }
                if (failures < 5) {
                    printf("FAIL X=%u groups=%ld h=%ld sp=%ld dp=%ld dbl=%d:"
                           " first byte %ld\n", x, groups, h, sp, dp, dbl,
                           at - dsto);
                }
                failures++;
            }
        }
    }
    /* the dispatcher is the C on the host, whatever the width */
    {
        uint32_t x2, x0;
        memset(got, 0, DSTMAX);
        memset(want, 0, DSTMAX);
        stdl_sdl2_c2p_xor(0x70, &x2, &x0);
        stdl_sdl2_c2p(src, 320, got, 320, 20, 8, 0, &stdl_sdl2_c2p_plain,
                      x2, x0);
        reference(src, 320, want, 320, 20, 8, 0, 0x70);
        runs++;
        if (memcmp(got, want, DSTMAX) != 0) {
            printf("FAIL the dispatcher, 20 groups\n");
            failures++;
        }
    }
    free(src);
    free(got);
    free(want);
    if (failures) {
        printf("test_c2p: %d of %d runs failed\n", failures, runs);
        return 1;
    }
    printf("test_c2p: ok (%d runs, every X)\n", runs);
    return 0;
}
