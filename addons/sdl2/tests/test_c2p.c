/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The conversion's C (src/c2p.c) against a pixel-by-pixel reference:
 * every border index X, one line and two, odd source addresses and
 * pitches, and a page filled with a pattern beforehand, so a byte
 * written outside the groups asked for fails as surely as a wrong one.
 * Then the public entries (SDL_stdl_c2p.h), whole pixels and pairs,
 * with X a constant and from a variable, the same way: pairs against
 * the reference on pixels in pairs, and on pixels that are not against
 * the pixels the header says they take; boxes of no groups or no rows
 * write nothing.
 */

#include "SDL_stdl_c2p.h"
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

/* the pixels a pair conversion takes of a row: in each group the
 * first of each pair in its first half, the second in its second;
 * `paired` makes every pixel its pair's, so they are the row itself */
static void take_pairs(uint8_t *row, long n, int paired)
{
    long g;
    int i;
    for (g = 0; g + 16 <= n; g += 16) {
        uint8_t in[16];
        for (i = 0; i < 16; i++) {
            in[i] = row[g + (i < 8 || paired ? (i & ~1) : (i | 1))];
        }
        memcpy(row + g, in, 16);
    }
}

/* the public entries against the reference: 1 for a box unlike it.
 * kind 0 whole pixels with X a constant (each X written out, as a
 * program names its own), 1 with X from a variable, 2 pairs on pixels
 * in pairs, 3 pairs on pixels that are not */
static volatile unsigned runtime_x;

static void convert_box(int kind, const uint8_t *src, long sp, uint8_t *dst,
                        long dp, long groups, long h, int dbl, unsigned x)
{
    if (kind == 0) {
        switch (x) {
        case 0x00: STDL_SDL2_ConvertBox(src, sp, dst, dp, groups, h, dbl, 0x00); break;
        case 0x10: STDL_SDL2_ConvertBox(src, sp, dst, dp, groups, h, dbl, 0x10); break;
        case 0x20: STDL_SDL2_ConvertBox(src, sp, dst, dp, groups, h, dbl, 0x20); break;
        case 0x30: STDL_SDL2_ConvertBox(src, sp, dst, dp, groups, h, dbl, 0x30); break;
        case 0x40: STDL_SDL2_ConvertBox(src, sp, dst, dp, groups, h, dbl, 0x40); break;
        case 0x50: STDL_SDL2_ConvertBox(src, sp, dst, dp, groups, h, dbl, 0x50); break;
        case 0x60: STDL_SDL2_ConvertBox(src, sp, dst, dp, groups, h, dbl, 0x60); break;
        case 0x70: STDL_SDL2_ConvertBox(src, sp, dst, dp, groups, h, dbl, 0x70); break;
        case 0x80: STDL_SDL2_ConvertBox(src, sp, dst, dp, groups, h, dbl, 0x80); break;
        case 0x90: STDL_SDL2_ConvertBox(src, sp, dst, dp, groups, h, dbl, 0x90); break;
        case 0xA0: STDL_SDL2_ConvertBox(src, sp, dst, dp, groups, h, dbl, 0xA0); break;
        case 0xB0: STDL_SDL2_ConvertBox(src, sp, dst, dp, groups, h, dbl, 0xB0); break;
        case 0xC0: STDL_SDL2_ConvertBox(src, sp, dst, dp, groups, h, dbl, 0xC0); break;
        case 0xD0: STDL_SDL2_ConvertBox(src, sp, dst, dp, groups, h, dbl, 0xD0); break;
        case 0xE0: STDL_SDL2_ConvertBox(src, sp, dst, dp, groups, h, dbl, 0xE0); break;
        default:   STDL_SDL2_ConvertBox(src, sp, dst, dp, groups, h, dbl, 0xF0); break;
        }
    } else if (kind == 1) {
        runtime_x = x;
        STDL_SDL2_ConvertBox(src, sp, dst, dp, groups, h, dbl, runtime_x);
    } else {
        runtime_x = x;
        STDL_SDL2_ConvertBoxPairs(src, sp, dst, dp, groups, h, dbl,
                                  runtime_x);
    }
}

static int public_entries(uint8_t *src, uint8_t *got, uint8_t *want,
                          long srcmax, long dstmax, int *runs)
{
    static const char *const kinds[4] = {
        "ConvertBox, X constant", "ConvertBox, X a variable",
        "ConvertBoxPairs, pixels in pairs", "ConvertBoxPairs, not in pairs"
    };
    uint8_t *taken = malloc((size_t)srcmax);
    int kind, t, failures = 0;
    unsigned x;
    for (kind = 0; kind < 4; kind++)
    for (x = 0; x < 256; x += 16)
    for (t = 0; t < 40; t++) {
        long groups = 1 + rnd() % 9;
        long h = 1 + rnd() % 6;
        long off = rnd() % 7;
        long sp = groups * 16 + rnd() % 23;
        long dp = groups * 16 + 16 * (rnd() % 3) + 2 * (rnd() % 2);
        long dsto = 16 * (rnd() % 4);
        int dbl = rnd() & 1;
        long i, r;
        for (i = 0; i < srcmax; i++) {
            src[i] = (uint8_t)rnd();
        }
        memcpy(taken, src, (size_t)srcmax);
        if (kind >= 2) {
            for (r = 0; r < h; r++) {
                if (kind == 2) {
                    take_pairs(src + off + r * sp, groups * 16, 1);
                }
                memcpy(taken + off + r * sp, src + off + r * sp,
                       (size_t)groups * 16);
                take_pairs(taken + off + r * sp, groups * 16, 0);
            }
        }
        memset(got, 0xA5, (size_t)dstmax);
        memset(want, 0xA5, (size_t)dstmax);
        convert_box(kind, src + off, sp, got + dsto, dp, groups, h, dbl, x);
        reference(taken + off, sp, want + dsto, dp, groups, h, dbl, x);
        (*runs)++;
        if (memcmp(got, want, (size_t)dstmax) != 0) {
            if (failures < 5) {
                printf("FAIL %s X=%u groups=%ld h=%ld dbl=%d\n", kinds[kind],
                       x, groups, h, dbl);
            }
            failures++;
        }
    }
    /* nothing for no groups or no rows, either entry */
    for (t = 0; t < 4; t++) {
        const long groups = (t & 1) ? 0 : -1, h = (t & 2) ? 3 : 0;
        memset(got, 0xA5, (size_t)dstmax);
        memset(want, 0xA5, (size_t)dstmax);
        STDL_SDL2_ConvertBox(src, 320, got, 320, groups, 2, 0, 0);
        STDL_SDL2_ConvertBox(src, 320, got, 320, 2, h - 3, 1, 0x70);
        STDL_SDL2_ConvertBoxPairs(src, 320, got, 320, groups, h, 0, 0);
        STDL_SDL2_ConvertBoxPairs(src, 320, got, 320, 3, h - 3, 1, 0x30);
        (*runs)++;
        if (memcmp(got, want, (size_t)dstmax) != 0) {
            printf("FAIL an empty box wrote to the page\n");
            failures++;
        }
    }
    free(taken);
    return failures;
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
    failures += public_entries(src, got, want, SRCMAX, DSTMAX, &runs);
    free(src);
    free(got);
    free(want);
    if (failures) {
        printf("test_c2p: %d of %d runs failed\n", failures, runs);
        return 1;
    }
    printf("test_c2p: ok (%d runs, every X, the C and the public entries)\n",
           runs);
    return 0;
}
