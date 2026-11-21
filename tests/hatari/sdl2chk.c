/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * On-target check of the SDL2 add-on's conversion (addons/sdl2/src/
 * c2p.c), on a TT or a Falcon. Every variant's assembler against the C
 * twin, which the host checks pixel by pixel: X = 0's, each fixed X's
 * (0x10 to 0xF0) and the run-time XOR's at every X, one line and two,
 * boxes of 1 to 20 groups, odd sources, and a pattern round the
 * destination that a stray write would break. Then the routine the
 * add-on's came from, which inverted planes 4-6 with a fixed not.l and
 * not.w, against the add-on's at X = 0x70; and the time of a whole
 * 320x240 frame each way, into ST-RAM as a page is, a TT's rows
 * doubled, and of a frame's worth of boxes two groups wide, a sprite's
 * box, against the C twin and the routine before. Then the pixel
 * pairs' loops (c2pp.c) against their twin, on pixels not in pairs
 * so that every one counts, and a whole frame's time as pairs. Then
 * the public entries a program converting its own screen calls
 * (SDL_stdl_c2p.h's STDL_SDL2_ConvertBox and _ConvertBoxPairs), X
 * constant and from a variable, against the same; and their times for
 * a whole frame, a frame a row a call and a frame a group a call,
 * whole pixels against pairs.
 *
 *   STCMD_NO_TTY=1 stcmd make sdl2
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -m68020-60 -msoft-float -Iinclude -Iaddons/sdl2/include/SDL2 \
 *       -Iaddons/sdl2/src -c -o obj/sdl2chk.o tests/hatari/sdl2chk.c
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -m68000 \
 *       -o dist/SDL2CHK.TOS obj/sdl2chk.o libstdl-sdl2.a libstdl.a
 *   TOS="<TOS 3.06>" MACHINE=tt \
 *       EXTRA="--cpu-exact on --compatible on --memsize 4" \
 *       tests/hatari/run.sh s2c dist/SDL2CHK.TOS 8 \
 *       "waitfor S2C:done;waitfor S2C:done"
 *
 * linked against the 68000 libraries, as a program using the add-on
 * is, so that it starts on a Falcon with no FPU (MACHINE=falcon and
 * TOS 4.04 or EmuTOS). Its cases take about three minutes of the
 * host's time in Hatari, past one waitfor's two.
 *
 * Built -m68000 instead, it is a 68000 program converting on a TT or a
 * Falcon, as docs/sdl2.md's "Converting a program's own screen" has
 * one do: linked against libstdl-sdl2.a (SDL2CK00.TOS), the archive
 * that page says to link, or the experimental libstdl-sdl2-st.a
 * (SDL2CKST.TOS, after `make sdl2st`), whose conversion is the same
 * assembler built for the 68000:
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 -m68000 \
 *       -Iinclude -Iaddons/sdl2/include/SDL2 -Iaddons/sdl2/src \
 *       -c -o obj/sdl2chk0.o tests/hatari/sdl2chk.c
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -m68000 \
 *       -o dist/SDL2CK00.TOS obj/sdl2chk0.o libstdl-sdl2.a libstdl.a
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <osbind.h>
#include <stdl/stdl.h>
#include "SDL_stdl_c2p.h"
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

/* the pixel pairs' loops (c2pp.c), called as stdl_sdl2_c2pp calls
 * them for STDL_SDL2_RenderCopyPairs and STDL_SDL2_ConvertBoxPairs */
extern stdl_sdl2_c2p_fn stdl_sdl2_c2pp_plain1, stdl_sdl2_c2pp_plain2,
                        stdl_sdl2_c2pp_any1, stdl_sdl2_c2pp_any2;

/* their twin: the pixels they take of a group (the first of each pair
 * in its first half, the second in its second), converted */
static void pairs_twin(const uint8_t *src, long sp, uint8_t *dst, long dp,
                       long groups, long h, int dbl, uint32_t x2,
                       uint32_t x0)
{
    for (; h > 0; h--, src += sp, dst += dbl ? dp << 1 : dp) {
        long g;
        for (g = 0; g < groups; g++) {
            uint8_t in[16];
            int i;
            for (i = 0; i < 16; i++) {
                in[i] = src[g * 16 + (i < 8 ? (i & ~1) : (i | 1))];
            }
            stdl_sdl2_c2p_c(in, 16, dst + g * 16, dp, 1, 1, dbl, x2, x0);
        }
    }
}

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

/* the whole page a long at a time: mintlib's memcmp goes a byte at a
 * time, and with it the cases took a TT more than run.sh's two minutes
 * of waiting in Hatari */
static int differ(const uint8_t *a, const uint8_t *b)
{
    const uint32_t *p = (const uint32_t *)(const void *)a;
    const uint32_t *q = (const uint32_t *)(const void *)b;
    long n = PAGE / 4;
    while (n-- > 0) {
        if (*p++ != *q++) {
            return 1;
        }
    }
    return 0;
}

static void ms(char *out, uint32_t ticks, int frames)
{
    /* 38400 ticks a second */
    unsigned long us = (unsigned long)ticks * 1000UL / 384UL * 10UL / frames;
    sprintf(out, "%lu.%02lu", us / 1000UL, (us % 1000UL) / 10UL);
}

/* the public entries: kind 0-2 ConvertBox with X the constant 0, 0x70
 * or 0xF0 (so that each names its own loops), 3 ConvertBox with X from
 * a variable, 4 ConvertBoxPairs with X the constant 0, 5 with X from a
 * variable */
static volatile unsigned runtime_x;

static void public_box(int kind, const uint8_t *src, long sp, uint8_t *dst,
                       long dp, long g, long h, int dbl, unsigned x)
{
    switch (kind) {
    case 0: STDL_SDL2_ConvertBox(src, sp, dst, dp, g, h, dbl, 0); break;
    case 1: STDL_SDL2_ConvertBox(src, sp, dst, dp, g, h, dbl, 0x70); break;
    case 2: STDL_SDL2_ConvertBox(src, sp, dst, dp, g, h, dbl, 0xF0); break;
    case 3:
        runtime_x = x;
        STDL_SDL2_ConvertBox(src, sp, dst, dp, g, h, dbl, runtime_x);
        break;
    case 4: STDL_SDL2_ConvertBoxPairs(src, sp, dst, dp, g, h, dbl, 0); break;
    default:
        runtime_x = x;
        STDL_SDL2_ConvertBoxPairs(src, sp, dst, dp, g, h, dbl, runtime_x);
        break;
    }
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
    static const long gs[] = { 1, 2, 3, 4, 5, 20 };
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
    for (gi = 0; gi < 6; gi++)
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
        if (differ(a, b)) {
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
            if (differ(a, b)) {
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

    /* the pair loops: X = 0's at X = 0, the run-time XOR's at every X */
    cases = bad = 0;
    for (any = 0; any < 2; any++)
    for (dbl = 0; dbl < 2; dbl++)
    for (xi = 0; xi < (any ? 16 : 1); xi++)
    for (gi = 0; gi < 6; gi++)
    for (hi = 0; hi < 2; hi++)
    for (oi = 0; oi < 3; oi++) {
        uint32_t x2, x0;
        const unsigned x = (unsigned)xi << 4;
        long g = gs[gi], h = hs[hi];
        stdl_sdl2_c2p_fn *f = any ? (dbl ? stdl_sdl2_c2pp_any2
                                         : stdl_sdl2_c2pp_any1)
                                  : (dbl ? stdl_sdl2_c2pp_plain2
                                         : stdl_sdl2_c2pp_plain1);
        stdl_sdl2_c2p_xor(x, &x2, &x0);
        memset(a, 0x5A, PAGE);
        memset(b, 0x5A, PAGE);
        f(src + offs[oi], W + 7, a + 32, W, g, h, x2, x0);
        pairs_twin(src + offs[oi], W + 7, b + 32, W, g, h, dbl, x2, x0);
        cases++;
        if (differ(a, b)) {
            if (bad < 4) {
                printf("S2C: PAIRS MISMATCH %s dbl %d X %u groups %ld"
                       " rows %ld source +%ld\n", any ? "any" : "plain",
                       dbl, x, g, h, offs[oi]);
            }
            bad++;
        }
    }
    if (bad) {
        printf("S2C: the pair loops differ from their twin in %ld of %ld"
               " cases\n", bad, cases);
    } else {
        printf("S2C: the pair loops match their twin, %ld cases\n", cases);
    }

    /* the public entries: each kind at its X (every X for those from a
     * variable), against the C twin or the pairs' twin */
    cases = bad = 0;
    for (any = 0; any < 6; any++)
    for (dbl = 0; dbl < 2; dbl++)
    for (xi = 0; xi < 16; xi++)
    for (gi = 0; gi < 6; gi += (gi < 2 ? 1 : 3))    /* 1, 2, 3, 20 */
    for (hi = 0; hi < 2; hi++)
    for (oi = 0; oi < 2; oi++) {
        static const unsigned constant[6] = { 0, 0x70, 0xF0, 0, 0, 0 };
        uint32_t x2, x0;
        const unsigned x = (any == 3 || any == 5) ? (unsigned)xi << 4
                                                  : constant[any];
        long g = gs[gi], h = hs[hi];
        if (xi > 0 && !(any == 3 || any == 5)) {
            continue;           /* a constant X once */
        }
        stdl_sdl2_c2p_xor(x, &x2, &x0);
        memset(a, 0x5A, PAGE);
        memset(b, 0x5A, PAGE);
        public_box(any, src + offs[oi], W + 7, a + 32, W, g, h, dbl, x);
        if (any >= 4) {
            pairs_twin(src + offs[oi], W + 7, b + 32, W, g, h, dbl, x2, x0);
        } else {
            stdl_sdl2_c2p_c(src + offs[oi], W + 7, b + 32, W, g, h, dbl,
                            x2, x0);
        }
        cases++;
        if (differ(a, b)) {
            if (bad < 4) {
                printf("S2C: PUBLIC MISMATCH kind %d dbl %d X %u groups %ld"
                       " rows %ld source +%ld\n", any, dbl, x, g, h,
                       offs[oi]);
            }
            bad++;
        }
    }
    if (bad) {
        printf("S2C: the public entries differ from the twins in %ld of"
               " %ld cases\n", bad, cases);
    } else {
        printf("S2C: the public entries match the twins, %ld cases\n",
               cases);
    }

    /* their times, X = 0: a whole frame in one call, a call a row (20
     * groups by 1) and a call a group (1 by 1), whole and as pairs; then
     * a call a row with the rows taking turns, one whole and one pairs,
     * as a program sending each row the way its contents say does,
     * against every row whole */
    for (dbl = 0; dbl < 2; dbl++) {
        enum { FRAMES = 10 };
        uint32_t t[9];
        char m[8][16];
        int f, y, c, k;
        t[0] = STDL_GetHz38400();
        for (f = 0; f < FRAMES; f++) {
            STDL_SDL2_ConvertBox(src, W, a, W, W / 16, H, dbl, 0);
        }
        t[1] = STDL_GetHz38400();
        for (f = 0; f < FRAMES; f++) {
            STDL_SDL2_ConvertBoxPairs(src, W, a, W, W / 16, H, dbl, 0);
        }
        t[2] = STDL_GetHz38400();
        for (f = 0; f < FRAMES; f++) {
            for (y = 0; y < H; y++) {
                STDL_SDL2_ConvertBox(src + y * W, W, a + y * W * (dbl + 1),
                                     W, W / 16, 1, dbl, 0);
            }
        }
        t[3] = STDL_GetHz38400();
        for (f = 0; f < FRAMES; f++) {
            for (y = 0; y < H; y++) {
                STDL_SDL2_ConvertBoxPairs(src + y * W, W,
                                          a + y * W * (dbl + 1), W, W / 16,
                                          1, dbl, 0);
            }
        }
        t[4] = STDL_GetHz38400();
        for (f = 0; f < FRAMES; f++) {
            for (y = 0; y < H; y++) {
                for (c = 0; c < W; c += 16) {
                    STDL_SDL2_ConvertBox(src + y * W + c, W,
                                         a + y * W * (dbl + 1) + c, W, 1, 1,
                                         dbl, 0);
                }
            }
        }
        t[5] = STDL_GetHz38400();
        for (f = 0; f < FRAMES; f++) {
            for (y = 0; y < H; y++) {
                for (c = 0; c < W; c += 16) {
                    STDL_SDL2_ConvertBoxPairs(src + y * W + c, W,
                                              a + y * W * (dbl + 1) + c, W,
                                              1, 1, dbl, 0);
                }
            }
        }
        t[6] = STDL_GetHz38400();
        for (f = 0; f < FRAMES; f++) {
            for (y = 0; y < H; y++) {
                STDL_SDL2_ConvertBox(src + y * W, W, a + y * W * (dbl + 1),
                                     W, W / 16, 1, dbl, 0);
            }
        }
        t[7] = STDL_GetHz38400();
        for (f = 0; f < FRAMES; f++) {
            for (y = 0; y < H; y++) {
                if (y & 1) {
                    STDL_SDL2_ConvertBox(src + y * W, W,
                                         a + y * W * (dbl + 1), W, W / 16,
                                         1, dbl, 0);
                } else {
                    STDL_SDL2_ConvertBoxPairs(src + y * W, W,
                                              a + y * W * (dbl + 1), W,
                                              W / 16, 1, dbl, 0);
                }
            }
        }
        t[8] = STDL_GetHz38400();
        for (k = 0; k < 8; k++) {
            ms(m[k], t[k + 1] - t[k], FRAMES);
        }
        printf("S2C: the public entries%s, ms a 320x240 frame, whole and as"
               " pairs: one call %s %s, a call a row %s %s, a call a group"
               " %s %s; a call a row, every row whole %s, rows taking"
               " turns %s\n", dbl ? " on two lines" : "", m[0], m[1], m[2],
               m[3], m[4], m[5], m[6], m[7]);
    }

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
    /* a whole frame as pairs, against the same frame whole */
    for (dbl = 0; dbl < 2; dbl++) {
        enum { FRAMES = 10 };
        uint32_t x2, x0, t0, t1, t2, t3;
        char mw[16], mp[16], many[16];
        int f;
        stdl_sdl2_c2p_xor(0, &x2, &x0);
        t0 = STDL_GetHz38400();
        for (f = 0; f < FRAMES; f++) {
            stdl_sdl2_c2p(src, W, a, W, W / 16, H, dbl,
                          &stdl_sdl2_c2p_plain, x2, x0);
        }
        t1 = STDL_GetHz38400();
        for (f = 0; f < FRAMES; f++) {
            (dbl ? stdl_sdl2_c2pp_plain2 : stdl_sdl2_c2pp_plain1)
                (src, W, a, W, W / 16, H, x2, x0);
        }
        t2 = STDL_GetHz38400();
        stdl_sdl2_c2p_xor(0x70, &x2, &x0);
        for (f = 0; f < FRAMES; f++) {
            (dbl ? stdl_sdl2_c2pp_any2 : stdl_sdl2_c2pp_any1)
                (src, W, a, W, W / 16, H, x2, x0);
        }
        t3 = STDL_GetHz38400();
        ms(mw, t1 - t0, FRAMES);
        ms(mp, t2 - t1, FRAMES);
        ms(many, t3 - t2, FRAMES);
        printf("S2C: a 320x240 frame%s, ms: whole %s, as pairs %s, pairs"
               " at run time %s\n", dbl ? " on two lines" : "", mw, mp,
               many);
    }
    /* the same frame as boxes two groups wide and 240 rows high */
    for (dbl = 0; dbl < 2; dbl++) {
        enum { FRAMES = 10, BW = 2 };
        uint32_t x2, x0, t0, t1, t2, t3;
        char m70[16], mc[16], mold[16];
        int f, c;
        stdl_sdl2_c2p_xor(0x70, &x2, &x0);
        t0 = STDL_GetHz38400();
        for (f = 0; f < FRAMES; f++) {
            for (c = 0; c < W / 16; c += BW) {
                stdl_sdl2_c2p(src + c * 16, W, a + c * 16, W, BW, H, dbl,
                              &stdl_sdl2_c2p_x70, x2, x0);
            }
        }
        t1 = STDL_GetHz38400();
        for (f = 0; f < FRAMES; f++) {
            for (c = 0; c < W / 16; c += BW) {
                stdl_sdl2_c2p_c(src + c * 16, W, a + c * 16, W, BW, H, dbl,
                                x2, x0);
            }
        }
        t2 = STDL_GetHz38400();
        for (f = 0; f < FRAMES; f++) {
            for (c = 0; c < W / 16; c += BW) {
                (dbl ? s2c_old2 : s2c_old1)(src + c * 16, W, b + c * 16, W,
                                            BW, H);
            }
        }
        t3 = STDL_GetHz38400();
        ms(m70, t1 - t0, FRAMES);
        ms(mc, t2 - t1, FRAMES);
        ms(mold, t3 - t2, FRAMES);
        printf("S2C: the frame in boxes two groups wide%s, ms: X = 0x70"
               " fixed %s, the C twin %s, the routine before %s\n",
               dbl ? " on two lines" : "", m70, mc, mold);
    }
    printf("S2C:done\n");
    STDL_Quit();
    return 0;
}
