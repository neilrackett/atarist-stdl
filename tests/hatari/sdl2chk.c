/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * On-target check of the SDL2 add-on's conversion (addons/sdl2/src/
 * c2p.c), on a TT or a Falcon. Every variant's assembler against the C
 * twin, which the host checks pixel by pixel: X = 0's, each fixed X's
 * (0x10 to 0xF0) and the run-time XOR's at every X, one line and two,
 * boxes of 1 to 20 groups (under 4 the dispatch takes the C, so both
 * are the twin there), odd sources, and a pattern round the
 * destination that a stray write would break. Then the routine the
 * add-on's came from, which inverted planes 4-6 with a fixed not.l and
 * not.w, against the add-on's at X = 0x70; and the time of a whole
 * 320x240 frame each way, into ST-RAM as a page is, a TT's rows
 * doubled.
 *
 *   STCMD_NO_TTY=1 stcmd make sdl2
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -m68020-60 -msoft-float -Iinclude -Iaddons/sdl2/include/SDL2 \
 *       -Iaddons/sdl2/src -o dist/SDL2CHK.TOS tests/hatari/sdl2chk.c \
 *       libstdl-sdl2.a libstdl.a
 *   TOS="<TOS 3.06>" MACHINE=tt LATE="--mmu off" \
 *       EXTRA="--cpu-exact on --compatible on --memsize 4" \
 *       tests/hatari/run.sh s2c dist/SDL2CHK.TOS 8 "waitfor S2C:done"
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <osbind.h>
#include <stdl/stdl.h>
#include "c2p.h"
#include "../../examples/hold.h"

/* the routine before, for the comparison: X fixed at 0x70 */
void s2c_old1(const uint8_t *src, long sp, uint8_t *dst, long dp,
              long groups, long h);
void s2c_old2(const uint8_t *src, long sp, uint8_t *dst, long dp,
              long groups, long h);

#define O_M(sh, back, ra, rb, mask) \
    "	move.l	" rb ",%d7\n" \
    "	lsr.l	#" #sh ",%d7\n" \
    "	eor.l	" ra ",%d7\n" \
    "	and.l	" mask ",%d7\n" \
    "	eor.l	%d7," ra "\n" \
    "	" back "\n" \
    "	eor.l	%d7," rb "\n"
#define O_W(ra, rb) \
    "	swap	" rb "\n" \
    "	move.w	" rb ",%d7\n" \
    "	move.w	" ra "," rb "\n" \
    "	move.w	%d7," ra "\n" \
    "	swap	" rb "\n"
#define O_GROUP(S1, S2, S3, S4) \
    "	move.l	(%a0)+,%d0\n" \
    "	move.l	(%a0)+,%d1\n" \
    "	move.l	(%a0)+,%d2\n" \
    "	move.l	(%a0)+,%d3\n" \
    O_M(4, "lsl.l	#4,%d7", "%d0", "%d1", "%d4") \
    O_M(4, "lsl.l	#4,%d7", "%d2", "%d3", "%d4") \
    S1 \
    O_M(8, "lsl.l	#8,%d7", "%d0", "%d2", "%d5") \
    O_M(8, "lsl.l	#8,%d7", "%d1", "%d3", "%d5") \
    S2 \
    O_M(1, "add.l	%d7,%d7", "%d0", "%d2", "#0x55555555") \
    O_M(1, "add.l	%d7,%d7", "%d1", "%d3", "#0x55555555") \
    S3 \
    O_W("%d0", "%d2") \
    O_W("%d1", "%d3") \
    S4 \
    O_M(2, "lsl.l	#2,%d7", "%d0", "%d2", "#0x33333333") \
    O_M(2, "lsl.l	#2,%d7", "%d1", "%d3", "#0x33333333") \
    "	not.l	%d2\n" \
    "	not.w	%d0\n" \
    "	swap	%d3\n" \
    "	swap	%d1\n" \
    "	swap	%d2\n" \
    "	swap	%d0\n" \
    "	move.l	%d3,%a3\n" \
    "	move.l	%d1,%a4\n" \
    "	move.l	%d2,%a5\n" \
    "	move.l	%d0,%a6\n"
#define O_S1(r) "	move.l	" r ",(%a1)+\n"
#define O_S2(r) "	move.l	" r ",(%a1)+\n	move.l	" r ",(%a2)+\n"
#define O_FUNC(name, S, DST2, DSTSTEP) \
    "	.even\n" \
    "	.globl	_" name "\n" \
    "_" name ":\n" \
    "	movem.l	%d2-%d7/%a2-%a6,-(%sp)\n" \
    "	move.l	#0x0F0F0F0F,%d4\n" \
    "	move.l	#0x00FF00FF,%d5\n" \
    "3:	move.l	48(%sp),%a0\n" \
    "	move.l	56(%sp),%a1\n" \
    DST2 \
    "	move.l	64(%sp),%d6\n" \
    "	subq.l	#1,%d6\n" \
    O_GROUP("", "", "", "") \
    "	bra.w	2f\n" \
    "1:\n" \
    O_GROUP(S("%a3"), S("%a4"), S("%a5"), S("%a6")) \
    "2:	dbra	%d6,1b\n" \
    S("%a3") S("%a4") S("%a5") S("%a6") \
    "	move.l	52(%sp),%d7\n" \
    "	add.l	%d7,48(%sp)\n" \
    "	move.l	60(%sp),%d7\n" \
    DSTSTEP \
    "	add.l	%d7,56(%sp)\n" \
    "	subq.l	#1,68(%sp)\n" \
    "	bne.w	3b\n" \
    "	movem.l	(%sp)+,%d2-%d7/%a2-%a6\n" \
    "	rts\n"

__asm__(
"	.text\n"
O_FUNC("s2c_old1", O_S1, "", "")
O_FUNC("s2c_old2", O_S2,
       "	move.l	60(%sp),%d7\n	lea	0(%a1,%d7.l),%a2\n",
       "	add.l	%d7,%d7\n")
);

static unsigned long seed = 0x12345678UL;

static unsigned rnd(void)
{
    seed = seed * 1103515245UL + 12345UL;
    return (unsigned)(seed >> 16) & 0x7FFF;
}

/* a page's worth of ST-RAM, as the screen's pages are */
static uint8_t *stram(long n)
{
    long p = Mxalloc(n, 0);
    if (p == -32) {
        p = Malloc(n);
    }
    return p > 0 ? (uint8_t *)p : NULL;
}

enum { W = 320, H = 240, PAGE = 640L * 240 + 64 };

static void ms(char *out, uint32_t ticks, int frames)
{
    /* 38400 ticks a second */
    unsigned long us = (unsigned long)ticks * 1000UL / 384UL * 10UL / frames;
    sprintf(out, "%lu.%02lu", us / 1000UL, (us % 1000UL) / 10UL);
}

static const struct stdl_sdl2_c2pv *const fixed[16] = {
    &stdl_sdl2_c2p_plain, &stdl_sdl2_c2p_x10, &stdl_sdl2_c2p_x20,
    &stdl_sdl2_c2p_x30, &stdl_sdl2_c2p_x40, &stdl_sdl2_c2p_x50,
    &stdl_sdl2_c2p_x60, &stdl_sdl2_c2p_x70, &stdl_sdl2_c2p_x80,
    &stdl_sdl2_c2p_x90, &stdl_sdl2_c2p_xa0, &stdl_sdl2_c2p_xb0,
    &stdl_sdl2_c2p_xc0, &stdl_sdl2_c2p_xd0, &stdl_sdl2_c2p_xe0,
    &stdl_sdl2_c2p_xf0
};

int main(void)
{
    static const long gs[] = { 1, 3, 4, 5, 20 };
    static const long hs[] = { 1, 7 };
    static const long offs[] = { 0, 1, 3 };
    uint8_t *src, *a, *b;
    long cases = 0, bad = 0, oldcases = 0, oldbad = 0, i;
    int dbl, xi, gi, hi, oi, any;

    hold_on_exit("SDL2CHK");

    if (STDL_Init(STDL_INIT_VIDEO) < 0) {
        printf("S2C: STDL_Init: %s\nS2C:done\n", STDL_GetError());
        return 1;
    }
    src = malloc(W * H + 64);
    a = stram(PAGE);
    b = stram(PAGE);
    if (!src || !a || !b) {
        printf("S2C: out of memory\nS2C:done\n");
        return 1;
    }
    for (i = 0; i < W * H + 64; i++) {
        src[i] = (uint8_t)rnd();
    }

    /* xi is X / 16; any: the run-time XOR's loops rather than X's own */
    for (any = 0; any < 2; any++)
    for (dbl = 0; dbl < 2; dbl++)
    for (xi = 0; xi < 16; xi++)
    for (gi = 0; gi < 5; gi++)
    for (hi = 0; hi < 2; hi++)
    for (oi = 0; oi < 3; oi++) {
        uint32_t x2, x0;
        const unsigned x = (unsigned)xi << 4;
        long g = gs[gi], h = hs[hi];
        stdl_sdl2_c2p_xor(x, &x2, &x0);
        memset(a, 0x5A, PAGE);
        memset(b, 0x5A, PAGE);
        stdl_sdl2_c2p(src + offs[oi], W + 7, a + 32, W, g, h, dbl,
                      any ? &stdl_sdl2_c2p_any : fixed[xi], x2, x0);
        stdl_sdl2_c2p_c(src + offs[oi], W + 7, b + 32, W, g, h, dbl, x2, x0);
        cases++;
        if (memcmp(a, b, PAGE) != 0) {
            if (bad < 4) {
                printf("S2C: MISMATCH %s dbl %d X %u groups %ld rows %ld"
                       " source +%ld\n", any ? "any" : "fixed", dbl, x, g,
                       h, offs[oi]);
            }
            bad++;
        }
        if (x == 0x70 && !any) {
            memset(b, 0x5A, PAGE);
            (dbl ? s2c_old2 : s2c_old1)(src + offs[oi], W + 7, b + 32, W,
                                        g, h);
            oldcases++;
            if (memcmp(a, b, PAGE) != 0) {
                oldbad++;
            }
        }
    }
    if (bad) {
        printf("S2C: the assembler differs from its C twin in %ld of %ld"
               " cases\n", bad, cases);
    } else {
        printf("S2C: the assembler matches its C twin, %ld cases\n", cases);
    }
    printf("S2C: the routine before %s X = 0x70's, %ld cases%s\n",
           oldbad ? "differs from" : "matches", oldcases,
           oldbad ? " (see above)" : "");

    /* a whole frame, 20 groups by 240 rows, each way */
    for (dbl = 0; dbl < 2; dbl++) {
        enum { FRAMES = 10 };
        uint32_t x2, x0, t0, t1, t2, t3, t4;
        char m0[16], m70[16], many[16], mold[16];
        int f;
        stdl_sdl2_c2p_xor(0, &x2, &x0);
        t0 = STDL_GetHz38400();
        for (f = 0; f < FRAMES; f++) {
            stdl_sdl2_c2p(src, W, a, W, W / 16, H, dbl,
                          &stdl_sdl2_c2p_plain, x2, x0);
        }
        t1 = STDL_GetHz38400();
        stdl_sdl2_c2p_xor(0x70, &x2, &x0);
        for (f = 0; f < FRAMES; f++) {
            stdl_sdl2_c2p(src, W, a, W, W / 16, H, dbl,
                          &stdl_sdl2_c2p_x70, x2, x0);
        }
        t2 = STDL_GetHz38400();
        for (f = 0; f < FRAMES; f++) {
            stdl_sdl2_c2p(src, W, a, W, W / 16, H, dbl,
                          &stdl_sdl2_c2p_any, x2, x0);
        }
        t3 = STDL_GetHz38400();
        for (f = 0; f < FRAMES; f++) {
            (dbl ? s2c_old2 : s2c_old1)(src, W, b, W, W / 16, H);
        }
        t4 = STDL_GetHz38400();
        ms(m0, t1 - t0, FRAMES);
        ms(m70, t2 - t1, FRAMES);
        ms(many, t3 - t2, FRAMES);
        ms(mold, t4 - t3, FRAMES);
        printf("S2C: a 320x240 frame%s, ms: X = 0 %s, X = 0x70 fixed %s,"
               " at run time %s, the routine before %s\n",
               dbl ? " on two lines" : "", m0, m70, many, mold);
    }
    printf("S2C:done\n");
    STDL_Quit();
    return 0;
}
