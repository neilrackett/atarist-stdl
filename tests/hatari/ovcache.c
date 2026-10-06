/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Does the Mega STE's cache move the bottom border's flick?
 *
 * The Mega STE's cache is 8192 one-word lines, direct-mapped, and
 * the whole of it is invalidated whenever another bus master takes
 * the bus - every BLiTTER operation (Christian Zietz's analysis of
 * the schematics, which Hatari models). A word the CPU reads or
 * writes replaces whatever line it maps to. So by the time the
 * bottom ISR runs, what the frame drew has decided which of the
 * flick's instruction and table words are still cached, and a miss
 * on the 8MHz bus costs a 16MHz CPU up to a bus slot. The flick has
 * a window of a few cycles.
 *
 * One binary, one disturbance at a time, each run with the borders
 * open for FRAMES frames:
 *
 *   idle    nothing drawn: the reference
 *   flush   the cache bit cleared and set again, once a frame: a
 *           flush with no BLiTTER and no drawing at all
 *   blit1   one BLiTTER operation of one word, once a frame: a
 *           flush by bus grant, with no bus time to speak of
 *   fill    a full-width 16-row STDL_FillRect into the back page,
 *           BLiTTER allowed
 *   fillcpu the same fill with STDL_UseBlitter(0)
 *   copy    24K copied by the CPU, once a frame: no flush, but
 *           every line of the cache replaced by a word that maps
 *           to it
 *
 * each twice, with stdl_ovsc_prime off (p0: the warm-up as it was
 * before 2026-10-06, the tail of the flick from slot 15) and on (p1:
 * the table read in, the whole path warmed), at 16MHz with the cache
 * (mode 1) and then 16MHz without it (mode 2), the control: a
 * machine with no cache cannot have a cold one. Bottom only, then
 * both borders.
 *
 * Per run: frames the bottom ISR saw line 263 display (bot), the
 * same for the top in combined mode, the miss counter, and the
 * diag histogram - polls from the 50Hz write until line 263's fetch
 * began. Within one binary a late restore shows as a histogram moved
 * towards zero (a restore inside line 263 slips the Shifter's plane
 * phase, which no counter sees); a missed test as a frame in 16+.
 * Between binaries the bins are not comparable - the diag's own path
 * moves with the build.
 *
 * Measured on a real Mega STE (TOS 2.06), 150 frames, bottom only,
 * 16MHz with the cache, p0 then p1: idle 102/150, flush 0/150,
 * blit1 0/150, fill 0/150, fillcpu 115/150, copy 0/150 - and p1 150
 * in every case but copy, 149. Mode 2: 150 everywhere, both ways.
 * p0 is not the library before the fix, which opened idle 146,
 * flush 30, blit1 25, fill 43, copy 53 in a build of its own: what
 * the old warm-up left cold depended on which words the rest of the
 * frame happened to evict, and that moves with the link layout -
 * which is the fault. Hatari's Mega STE takes the Timer B path,
 * which reads no table, and cannot show any of this.
 *
 * Results go to the console and OVCACHE.TXT after STDL_Quit, so no
 * GEMDOS call runs while the borders are open. -DFORCE_CACHED reads
 * the table in on any machine: its cost at 8MHz, where the ISR's
 * budget is tightest (150/150 on Hatari st and ste).
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Iinclude -o OVCACHE.TOS tests/hatari/ovcache.c libstdl.a
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <osbind.h>
#include <stdl/stdl.h>

#ifndef FRAMES
#define FRAMES 150
#endif

extern uint8_t  stdl_ovsc_botok, stdl_ovsc_topok;
extern uint8_t  stdl_ovsc_diag, stdl_ovsc_dpolls;
extern uint8_t  stdl_ovsc_tick, stdl_ovsc_measured;
extern uint8_t  stdl_ovsc_prime, stdl_ovsc_cached;
extern uint16_t stdl_ovsc_found, stdl_ovsc_test;
extern uint16_t stdl_ovsc_mF, stdl_ovsc_mTURN, stdl_ovsc_mNOP;
extern uint16_t stdl_ovsc_cal_c;

enum { C_IDLE, C_FLUSH, C_BLIT1, C_FILL, C_FILLCPU, C_COPY, NCOND };
static const char *const cname[NCOND] = {
    "idle", "flush", "blit1", "fill", "fillcpu", "copy"
};

#define MSTE_CTL (*(volatile uint8_t *)0xFFFF8E21UL)

static uint8_t *copy_a, *copy_b;
#define COPY_BYTES 24576
static volatile uint16_t blit_word;

static char outbuf[8192];
static int  outlen;

static void say(const char *fmt, ...)
{
    va_list ap;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(outbuf + outlen, sizeof outbuf - (size_t)outlen, fmt, ap);
    va_end(ap);
    if (n > 0) {
        outlen += n;
        if (outlen > (int)sizeof outbuf - 1) {
            outlen = (int)sizeof outbuf - 1;
        }
    }
}

static void flush_out(void)
{
    long h;

    outbuf[outlen] = '\0';
    printf("%s", outbuf);
    fflush(stdout);
    h = Fcreate("OVCACHE.TXT", 0);
    if (h >= 0) {
        Fwrite((short)h, (long)outlen, outbuf);
        Fclose((short)h);
    }
}

/* one hog-mode BLiTTER operation, one word, one line: zeros into a
 * word of our own */
static void blit_one(void)
{
    volatile uint8_t *b = (volatile uint8_t *)0xFFFF8A00UL;

    *(volatile uint16_t *)(b + 0x28) = 0xFFFF;      /* endmask 1-3  */
    *(volatile uint16_t *)(b + 0x2A) = 0xFFFF;
    *(volatile uint16_t *)(b + 0x2C) = 0xFFFF;
    *(volatile uint16_t *)(b + 0x2E) = 2;           /* dst x inc    */
    *(volatile uint16_t *)(b + 0x30) = 2;           /* dst y inc    */
    *(volatile uint32_t *)(b + 0x32) = (uint32_t)(uintptr_t)&blit_word;
    *(volatile uint16_t *)(b + 0x36) = 1;           /* x count      */
    *(volatile uint16_t *)(b + 0x38) = 1;           /* y count      */
    b[0x3A] = 0;                                    /* HOP ones     */
    b[0x3B] = 0;                                    /* OP zero      */
    b[0x3D] = 0;                                    /* skew         */
    b[0x3C] = 0xC0;                                 /* start, hog   */
    while (b[0x3C] & 0x80) {
    }
}

static void disturb(int c, int f)
{
    STDL_Surface *s = STDL_GetVideoSurface();
    STDL_Rect r;
    uint8_t v;

    switch (c) {
    case C_FLUSH:
        v = MSTE_CTL;
        MSTE_CTL = (uint8_t)(v & ~1);
        MSTE_CTL = v;
        break;
    case C_BLIT1:
        blit_one();
        break;
    case C_FILL:
    case C_FILLCPU:
        r.x = 0; r.y = 40; r.w = 320; r.h = 16;
        STDL_FillRect(s, &r, (uint8_t)(1 + (f & 7)));
        break;
    case C_COPY:
        memcpy(copy_b, copy_a, COPY_BYTES);
        break;
    default:
        break;
    }
}

static void run(char mode, int speed, int c, int prime)
{
    int f, bot = 0, top = 0, hist[17], i;
    uint32_t m0;

    stdl_ovsc_prime = (uint8_t)prime;
    STDL_UseBlitter(c != C_FILLCPU);
    for (i = 0; i < 17; i++) {
        hist[i] = 0;
    }
    STDL_Flip();
    STDL_Flip();
    m0 = STDL_OverscanMisses();
    for (f = 0; f < FRAMES; f++) {
        disturb(c, f);
        STDL_Flip();
        bot += stdl_ovsc_botok ? 1 : 0;
        top += stdl_ovsc_topok ? 1 : 0;
        hist[stdl_ovsc_dpolls < 16 ? stdl_ovsc_dpolls : 16]++;
    }
    STDL_UseBlitter(1);
    stdl_ovsc_prime = 1;
    say("%c s%d %-7s p%d bot=%3d/%d top=%3d miss=%3lu polls=",
        mode, speed, cname[c], prime, bot, FRAMES,
        mode == 'c' ? top : -1,
        (unsigned long)(STDL_OverscanMisses() - m0));
    for (i = 0; i < 17; i++) {
        say("%d%s", hist[i], i < 16 ? "," : "\r\n");
    }
}

int main(void)
{
    static const int speeds[] = { 1, 2 };
    static const char modes[] = { 'b', 'c' };
    const STDL_MachineInfo *mi;
    int si, m, c, p;

    if (STDL_Init(STDL_INIT_VIDEO) < 0) {
        return 1;
    }
    if (STDL_SetVideoMode(320, 200, 4, STDL_DOUBLEBUF) == NULL) {
        STDL_Quit();
        return 1;
    }
    mi = STDL_GetMachineInfo();
    copy_a = malloc(COPY_BYTES);
    copy_b = malloc(COPY_BYTES);
    if (copy_a == NULL || copy_b == NULL) {
        STDL_Quit();
        return 1;
    }
    memset(copy_a, 0x55, COPY_BYTES);
    say("OVCACHE frames=%d machine=%08lx megaste=%d\r\n", FRAMES,
        (unsigned long)mi->mch_cookie, mi->is_megaste);
    stdl_ovsc_diag = 1;
    for (si = 0; si < 2; si++) {
        if (!mi->is_megaste && si > 0) {
            break;
        }
        STDL_UseMegaSteSpeedup(speeds[si]);
        for (m = 0; m < 2; m++) {
            if (modes[m] == 'c') {
                STDL_OpenTopBorder();
            }
            if (STDL_OpenBottomBorder() == 0) {
                say("open failed: %s\r\n", STDL_GetError());
                break;
            }
#ifdef FORCE_CACHED
            stdl_ovsc_cached = 1;
#endif
            if (m == 0) {
                say("s%d cal c16=%u tick=%u measured=%u F=%u turn=%u "
                    "nop=%u found=%u test=%u cached=%u\r\n",
                    speeds[si], stdl_ovsc_cal_c, stdl_ovsc_tick,
                    stdl_ovsc_measured, stdl_ovsc_mF, stdl_ovsc_mTURN,
                    stdl_ovsc_mNOP, stdl_ovsc_found, stdl_ovsc_test,
                    stdl_ovsc_cached);
            }
            for (c = 0; c < NCOND; c++) {
                if ((c == C_FLUSH && !mi->is_megaste)
                    || (c == C_BLIT1 && !mi->has_blitter)) {
                    continue;
                }
                for (p = 0; p < 2; p++) {
                    run(modes[m], speeds[si], c, p);
                }
            }
            STDL_CloseBottomBorder();
            STDL_CloseTopBorder();
        }
    }
    STDL_UseMegaSteSpeedup(1);
    say("OVCACHE END\r\n");
    STDL_Quit();
    flush_out();
    return 0;
}
