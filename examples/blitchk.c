/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: CC0-1.0
 *
 * STDL example program, dedicated to the public domain so it can
 * be used as a starting point without licence concerns.
 */
/*
 * blitchk - on-target BLiTTER correctness harness.
 *
 * The host-side unit tests cannot cover the blitter, so this runs
 * on the machine: every randomised fill and blit is performed
 * twice, once with STDL_UseBlitter(0) and once with (1), into two
 * destination surfaces that started identical, and the pixel and
 * mask contents are compared byte-for-byte after each operation.
 * Any divergence between the CPU and blitter paths is a bug in one
 * of them. Finishes with a fill/blit timing comparison.
 *
 * It takes about two minutes on a Mega STE (114s in Hatari; 302s
 * while the comparison was memcmp's byte loop), printing a dot every
 * 30 operations, about a second, so a hang shows as the dots
 * stopping, and its time at the end.
 *
 * On machines without a blitter it degenerates to comparing the
 * CPU path with itself and reports that nothing was accelerated.
 */

#include <stdio.h>
#include <string.h>
#include <stdl/stdl.h>
#include "hold.h"

#define ITERATIONS 300
#define LONGROWS   40

static uint32_t rng = 0x12345678;
static uint32_t rnd(void)
{
    rng = rng * 1103515245UL + 12345UL;
    return (rng >> 16) & 0x7FFF;
}

/* rnd() % n, for n up to 65535: rnd() is at most 0x7FFF, so a 16-bit
 * divide gives the values the 32-bit % did, without the library call
 * a 32-bit % is on a 68000 */
static unsigned rndmod(unsigned n)
{
    return (uint16_t)rnd() % (uint16_t)n;
}

static int maxcol = 16;         /* colours the plane budget allows */

static STDL_Surface *make_src(int keyed)
{
    STDL_Surface *s = STDL_CreateSurface(160, 64);
    int lim = keyed ? (maxcol < 6 ? maxcol : 6) : maxcol;
    int x, y;

    for (y = 0; y < 64; y++) {
        for (x = 0; x < 160; x++) {
            STDL_PutPixel(s, x, y, (uint8_t)(rndmod(lim)));
        }
    }
    if (keyed) {
        STDL_SetColourKey(s, 1, (uint8_t)(maxcol > 3 ? 3 : maxcol - 1));
    }
    return s;
}

/*
 * Sprites for the comparison: widths that land differently on groups
 * (5, 16, 23, 32, 48, 64, 80), a three-frame strip, and each both
 * unshifted - the BLiTTER shifts those itself - and pre-shifted.
 */
#define NSPR 16
static STDL_Sprite *sprs[NSPR];
static int nspr;

static void make_sprites(void)
{
    static const int ws[8] = { 5, 16, 23, 32, 48, 64, 80, 16 };
    int k, pre;

    nspr = 0;
    for (k = 0; k < 8; k++) {
        int nframes = (k == 7) ? 3 : 1;
        int w = ws[k], h = 3 + (int)(rndmod(28));
        STDL_Surface *s = STDL_CreateSurface(w * nframes, h);
        int x, y;

        for (y = 0; y < h; y++) {
            for (x = 0; x < w * nframes; x++) {
                STDL_PutPixel(s, x, y, (uint8_t)(rndmod(maxcol)));
            }
        }
        STDL_SetColourKey(s, 1, 0);
        for (pre = 0; pre <= 1; pre++) {
            sprs[nspr++] = STDL_SpriteFromSurface(s, w,
                                                  pre ? STDL_PRESHIFT : 0);
        }
        STDL_FreeSurface(s);
    }
}

static void free_sprites(void)
{
    int k;
    for (k = 0; k < nspr; k++) {
        STDL_FreeSprite(sprs[k]);
    }
    nspr = 0;
}

/*
 * Whether n bytes at a and b differ. The check after every operation
 * is most of the run: memcmp goes a byte at a time, and took 36-41s of
 * each 45-50s pass on a Mega STE (Hatari). cmpm.l compares a long at
 * a time; the surfaces come from malloc, so their addresses are even,
 * which is all a 68000's long reads need.
 */
static int differ(const void *a, const void *b, uint32_t n)
{
#ifdef __m68k__
    const uint8_t *p = a, *q = b;
    uint16_t n8 = (uint16_t)(n >> 5), n1 = (uint16_t)((n >> 2) & 7);
    int d;

    if ((((uintptr_t)a | (uintptr_t)b) & 1) != 0 || n >= 0x200000UL) {
        return memcmp(a, b, n) != 0;
    }
    __asm__ volatile(
        "	moveq	#1,%0\n"
        "	bra.s	2f\n"
        "1:\n"
        "	.rept	8\n"
        "	cmpm.l	(%1)+,(%2)+\n"
        "	bne.s	9f\n"
        "	.endr\n"
        "2:	dbra	%3,1b\n"
        "	bra.s	4f\n"
        "3:	cmpm.l	(%1)+,(%2)+\n"
        "	bne.s	9f\n"
        "4:	dbra	%4,3b\n"
        "	moveq	#0,%0\n"
        "9:\n"
        : "=&d"(d), "+a"(p), "+a"(q), "+d"(n8), "+d"(n1)
        :
        : "cc", "memory");
    return d != 0 || memcmp(p, q, n & 3) != 0;
#else
    return memcmp(a, b, n) != 0;
#endif
}

static int compare(const STDL_Surface *a, const STDL_Surface *b)
{
    if (differ(a->pixels, b->pixels, (uint32_t)a->stride * a->h)) {
        return 1;
    }
    if (a->mask != NULL && b->mask != NULL
        && differ(a->mask, b->mask, (uint32_t)a->maskstride * a->h)) {
        return 2;
    }
    return 0;
}

/*
 * One randomised CPU-vs-BLiTTER pass at the current plane budget.
 * Every surface is built inside the pass so it starts zeroed - a
 * reduced budget only licenses skipping writes to planes that are
 * already zero, and reusing a 16-colour surface would break that.
 * Returns the mismatch count.
 */
static int compare_pass(void)
{
    STDL_Surface *src_plain, *src_keyed, *da, *db;
    int i, dot = 0, failures = 0;

    rng = 0x12345678;
    src_plain = make_src(0);
    src_keyed = make_src(1);
    da = STDL_CreateSurface(320, 160);
    db = STDL_CreateSurface(320, 160);
    STDL_CreateMask(da, 1);
    STDL_CreateMask(db, 1);

#ifdef STDL_BLIT_STATS
    stdl_blit_blitter = 0;
    stdl_spr_blitter = 0;
#endif
    make_sprites();
    for (i = 0; i < ITERATIONS; i++) {
        int op = (int)(rndmod(5));
        int phase = (int)(rnd() & 15);
        STDL_Rect r, r2;

        if (op == 4) {
            /* a sprite, anywhere near the surface, through a random
             * clip rectangle */
            STDL_Sprite *spr = sprs[rndmod((unsigned)nspr)];
            int frame = (int)(rndmod(spr->nframes));
            int x = (int)(rndmod(420)) - 90;
            int y = (int)(rndmod(200)) - 30;
            STDL_Rect clip, all = { 0, 0, 320, 160 };

            clip.x = (int16_t)(rndmod(60));
            clip.y = (int16_t)(rndmod(30));
            clip.w = (uint16_t)(20 + rndmod(260));
            clip.h = (uint16_t)(20 + rndmod(130));
            STDL_SetClipRect(da, &clip);
            STDL_SetClipRect(db, &clip);
            STDL_UseBlitter(0);
            STDL_BlitSprite(spr, frame, da, x, y);
            STDL_UseBlitter(1);
            STDL_BlitSprite(spr, frame, db, x, y);
            STDL_SetClipRect(da, &all);
            STDL_SetClipRect(db, &all);
        } else if (op == 3) {
            /* whole groups: the fills whose plane words are all the
             * same (0, 15) take their own routes on both paths */
            static const uint8_t cols[4] = { 0, 15, 0, 5 };
            uint8_t col = cols[rnd() & 3];
            if (col >= maxcol) {
                col = 0;
            }
            r.x = (int16_t)(16 * (int)(rndmod(22)) - 16);
            r.y = (int16_t)((int)(rndmod(180)) - 10);
            r.w = (uint16_t)(16 * (1 + rndmod(21)));
            r.h = (uint16_t)(1 + rndmod(100));
            r2 = r;
            STDL_UseBlitter(0);
            STDL_FillRect(da, &r, col);
            STDL_UseBlitter(1);
            STDL_FillRect(db, &r2, col);
        } else if (op == 0) {
            /* fill: random rect, random colour or transparent */
            uint8_t col = (uint8_t)(rndmod((maxcol + 1)));
            if (col == maxcol) {
                col = STDL_TRANSPARENT;
            }
            r.x = (int16_t)((int)(rndmod(360)) - 20);
            r.y = (int16_t)((int)(rndmod(180)) - 10);
            r.w = (uint16_t)(rndmod(200));
            r.h = (uint16_t)(rndmod(100));
            r2 = r;
            STDL_UseBlitter(0);
            STDL_FillRect(da, &r, col);
            STDL_UseBlitter(1);
            STDL_FillRect(db, &r2, col);
        } else {
            /* blit: half at the same phase, half at independent ones -
             * the BLiTTER shifts those itself, left and right */
            STDL_Surface *src = (op == 1) ? src_plain : src_keyed;
            int dphase = (rnd() & 1) ? phase : (int)(rnd() & 15);
            STDL_Rect sr;
            sr.x = (int16_t)(phase + 16 * (int)(rndmod(3)));
            sr.y = (int16_t)(rndmod(20));
            sr.w = (uint16_t)(rndmod(160));
            sr.h = (uint16_t)(rndmod(64));
            r.x = (int16_t)(dphase + 16 * ((int)(rndmod(14)) - 1));
            r.y = (int16_t)((int)(rndmod(170)) - 10);
            r2 = r;
            STDL_UseBlitter(0);
            STDL_BlitSurface(src, &sr, da, &r);
            STDL_UseBlitter(1);
            STDL_BlitSurface(src, &sr, db, &r2);
        }

        {
            int diff = compare(da, db);
            if (diff != 0) {
                failures++;
                printf("\nMISMATCH iter %d op %d (%s)\n", i, op,
                       diff == 1 ? "pixels" : "mask");
                if (failures > 5) {
                    break;
                }
                /* resync so later iterations stay meaningful */
                memcpy(db->pixels, da->pixels,
                       (uint32_t)da->stride * da->h);
                memcpy(db->mask, da->mask,
                       (uint32_t)da->maskstride * da->h);
            }
        }
        /* a dot every 30 operations, about a second on a Mega STE,
         * so that a slow pass and a hung one look different */
        if (++dot == 30) {
            dot = 0;
            putchar('.');
            fflush(stdout);
        }
    }
    printf("\n");
    printf(failures == 0 ? "PASS: %d operations identical\n"
                         : "FAIL: %d mismatches in %d ops\n",
           failures == 0 ? ITERATIONS : failures, ITERATIONS);

    /*
     * Rows of whole groups longer than 64 bytes, which the random run
     * above all but never makes (its sources are 160 wide): a
     * scroller's window, 9 to 20 groups, which the CPU copies with
     * movem in passes of 40-byte units and the rows' tails, into the
     * masked destinations. After the run, from its own generator
     * state, so the run is the operations it always was.
     */
    if (failures == 0) {
        STDL_Surface *wide = STDL_CreateSurface(320, 64);
        int bad = 0, x, y;

        for (y = 0; wide != NULL && y < 64; y++) {
            for (x = 0; x < 320; x++) {
                STDL_PutPixel(wide, x, y, (uint8_t)(rndmod(maxcol)));
            }
        }
        for (i = 0; wide != NULL && i < LONGROWS; i++) {
            int ng = 9 + (int)(rndmod(12));
            STDL_Rect sr, r, r2;

            sr.x = (int16_t)(16 * (int)(rndmod((unsigned)(21 - ng))));
            sr.y = (int16_t)(rndmod(32));
            sr.w = (uint16_t)(16 * ng);
            sr.h = (uint16_t)(1 + rndmod(32));
            r.x = (int16_t)(16 * (int)(rndmod((unsigned)(21 - ng))));
            r.y = (int16_t)(rndmod(128));
            r2 = r;
            STDL_UseBlitter(0);
            STDL_BlitSurface(wide, &sr, da, &r);
            STDL_UseBlitter(1);
            STDL_BlitSurface(wide, &sr, db, &r2);
            if (compare(da, db) != 0) {
                printf("MISMATCH long rows %d: %d groups\n", i, ng);
                bad++;
                break;
            }
        }
        printf(bad == 0 ? "PASS: %d long-row copies identical\n"
                        : "FAIL: long-row copies differ\n", LONGROWS);
        failures += bad;
        STDL_FreeSurface(wide);
    }
#ifdef STDL_BLIT_STATS
    /*
     * A comparison the BLiTTER never took part in compares the CPU
     * with itself and passes whatever the BLiTTER code does. Say how
     * much of the work it did, and fail a forced pass on a machine
     * that has one if it did none.
     */
    printf("  BLiTTER blit rows: %lu, sprite rows: %lu\n",
           stdl_blit_blitter, stdl_spr_blitter);
    if (stdl_blit_force && STDL_GetMachineInfo()->has_blitter
        && (stdl_blit_blitter == 0 || stdl_spr_blitter == 0)) {
        printf("FAIL: forced pass, but the BLiTTER was never used\n");
        failures++;
    }
#endif
    free_sprites();

    STDL_FreeSurface(src_plain);
    STDL_FreeSurface(src_keyed);
    STDL_FreeSurface(da);
    STDL_FreeSurface(db);
    return failures;
}

/* fill + keyed blit throughput, CPU path against BLiTTER path */
static void timing_pass(const char *label)
{
    STDL_Surface *da = STDL_CreateSurface(320, 160);
    STDL_Surface *src = make_src(1);
    STDL_Rect big = { 0, 0, 320, 160 };
    STDL_Rect d = { 0, 0, 0, 0 };
    uint32_t t0, t1, cpu_ms = 0, blit_ms = 0;
    int use, n;

    for (use = 0; use <= 1; use++) {
        STDL_UseBlitter(use);
        t0 = STDL_GetTicks();
        for (n = 0; n < 50; n++) {
            STDL_Rect f = big;
            STDL_FillRect(da, &f, (uint8_t)(n % maxcol));
            d.x = 0; d.y = 0;
            STDL_BlitSurface(src, NULL, da, &d);
        }
        t1 = STDL_GetTicks();
        if (use) {
            blit_ms = t1 - t0;
        } else {
            cpu_ms = t1 - t0;
        }
    }
    printf("%s 50x (fill 320x160 + keyed blit 160x64): "
           "cpu %lums, blitter %lums\n", label,
           (unsigned long)cpu_ms, (unsigned long)blit_ms);
    STDL_FreeSurface(da);
    STDL_FreeSurface(src);
}

int main(void)
{
    int failures, blit_avail;
    uint32_t start;

    hold_on_exit(NULL);

    if (STDL_Init(STDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "init failed: %s\n", STDL_GetError());
        return 1;
    }
    start = STDL_GetTicks();
    blit_avail = STDL_GetMachineInfo()->has_blitter;
    printf("blitter %s\n", blit_avail ? "present" : "ABSENT");

    /*
     * Print what the blit's fast paths depend on, because none of it
     * is visible in a picture: rows must start on a long boundary or
     * the short-row copy takes the slower word loop. The library
     * guarantees this for surfaces it allocates, and the guarantee
     * is the kind a compiler has been seen to optimise away, so it
     * is checked here on the machine rather than asserted in C.
     */
    {
        STDL_Surface *a = STDL_CreateSurface(336, 224);
        if (a != NULL) {
            printf("rows long aligned: %s (adjust %d)\n",
                   ((uintptr_t)a->pixels & 3) ? "NO - short rows will "
                   "take the word path" : "yes", (int)a->pix_adj);
            STDL_FreeSurface(a);
        }
    }

    /*
     * The default budget first, then a reduced one. The CPU and
     * BLiTTER paths have to agree at every budget: the blitter runs
     * one pass per plane, so a stale loop count there would show up
     * as a mismatch against the CPU path immediately. The timing
     * lines show what the budget is worth on this machine.
     */
    /*
     * Each budget twice: once as the library chooses, and once with
     * every operation the BLiTTER could do sent to it whatever its
     * size (stdl_blit_force, a stats-build variable), so the corner
     * cases a size threshold keeps on the CPU - one-word rows, clipped
     * edges - are compared too. Budget 3 as well as 2: the CPU rounds
     * it up to four planes while the BLiTTER loops over three.
     */
    failures = 0;
    {
        static const int budgets[3] = { 4, 2, 3 };
        int b, forced;

        for (b = 0; b < 3; b++) {
            STDL_SetPlaneBudget(budgets[b]);
            maxcol = 1 << budgets[b];
            for (forced = 0; forced <= 1; forced++) {
                printf("plane budget %d%s:\n", budgets[b],
                       forced ? ", BLiTTER forced" : "");
#ifdef STDL_BLIT_STATS
                stdl_blit_force = forced;
#else
                if (forced) {
                    continue;
                }
#endif
                failures += compare_pass();
            }
#ifdef STDL_BLIT_STATS
            stdl_blit_force = 0;
#endif
            if (budgets[b] != 3) {
                char label[16];
                sprintf(label, "budget %d:", budgets[b]);
                timing_pass(label);
            }
        }
        STDL_SetPlaneBudget(4);
        maxcol = 16;
    }

    /*
     * On a Mega STE, what the 16MHz clock and its cache are worth
     * for pixel work - measured inside one binary, because frame
     * times there move with code layout and two builds cannot be
     * compared. STDL_UseMegaSteSpeedup(0) puts the machine back to
     * whatever the user had set, which is also how a port opts out
     * of the switch STDL_Init makes.
     */
    if (STDL_GetMachineInfo()->is_megaste) {
        STDL_UseMegaSteSpeedup(3);
        timing_pass("16MHz+cache:");
        STDL_UseMegaSteSpeedup(2);
        timing_pass("16MHz no cache:");
        STDL_UseMegaSteSpeedup(0);
        timing_pass("as the user set it:");
        STDL_UseMegaSteSpeedup(1);      /* back to the default */
    }

    printf("BLITCHK done: %s, in %lus\n", failures != 0 ? "FAIL" : "PASS",
           (unsigned long)((STDL_GetTicks() - start) / 1000));
    fflush(stdout);
    STDL_Quit();
    return failures != 0;
}
