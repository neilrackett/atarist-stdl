/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The BLiTTER paths, natively, under AddressSanitizer.
 *
 * Two layers. First the model itself (blitmodel.c) against
 * operations worked by hand from the chip's documentation - skew,
 * FXSR, endmasks, address stepping - so the model is pinned before
 * anything is judged by it. Then the library: randomised fills and
 * blits run once on the CPU and once with the BLiTTER forced (every
 * eligible operation, whatever its size), compared byte for byte,
 * pixels and masks, at plane budgets 4, 2 and 3. BLITCHK does the
 * same on the machine; this finds an overread or a wrong register
 * before anything is built for it.
 *
 * The overscan blit policy's split path - an operation cut into
 * pieces around a border window - runs here too, with a policy that
 * grants a random number of lines each time.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stdl_internal.h"

extern unsigned long stdl_host_blit_ops;

static int failures;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        failures++; \
        printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } \
} while (0)

static uint32_t rng = 0x9E3779B9u;
static unsigned rnd(void)
{
    rng = rng * 1103515245u + 12345u;
    return (rng >> 16) & 0x7FFF;
}

/* --- the model against hand-worked operations -------------------- */

static void run_model(const uint16_t *src, int16_t sxinc, int16_t syinc,
                      uint16_t *dst, int16_t dxinc, int16_t dyinc,
                      uint16_t em1, uint16_t em2, uint16_t em3,
                      uint16_t xc, uint16_t yc, uint8_t hop, uint8_t op,
                      uint8_t skew)
{
    volatile stdl_host_blitregs_t *b = &stdl_host_blit;

    b->src_xinc = sxinc;
    b->src_yinc = syinc;
    b->src_addr = (uintptr_t)src;
    b->endmask1 = em1;
    b->endmask2 = em2;
    b->endmask3 = em3;
    b->dst_xinc = dxinc;
    b->dst_yinc = dyinc;
    b->dst_addr = (uintptr_t)dst;
    b->xcount = xc;
    b->ycount = yc;
    b->hop = hop;
    b->op = op;
    b->skew = skew;
    b->ctrl = 0xC0;
    stdl_host_blitter_exec();
}

static void test_model(void)
{
    uint16_t src[8], dst[8];

    /* a plain copy, three words */
    src[0] = 0x1111; src[1] = 0x2222; src[2] = 0x3333;
    memset(dst, 0, sizeof(dst));
    run_model(src, 2, 2, dst, 2, 2, 0xFFFF, 0xFFFF, 0xFFFF, 3, 1, 2, 3, 0);
    CHECK(dst[0] == 0x1111 && dst[1] == 0x2222 && dst[2] == 0x3333,
          "copy: %04x %04x %04x", dst[0], dst[1], dst[2]);
    CHECK(stdl_host_blit.ctrl == 0x40 && stdl_host_blit.ycount == 0,
          "copy leaves busy clear and the line count spent");

    /* endmasks: first word keeps its left byte, last its right byte,
     * the middle one is replaced whole */
    dst[0] = 0xAAAA; dst[1] = 0xAAAA; dst[2] = 0xAAAA;
    run_model(src, 2, 2, dst, 2, 2, 0x00FF, 0xFFFF, 0xFF00, 3, 1, 2, 3, 0);
    CHECK(dst[0] == 0xAA11 && dst[1] == 0x2222 && dst[2] == 0x33AA,
          "endmasks: %04x %04x %04x", dst[0], dst[1], dst[2]);

    /* one word: endmask 1 alone applies */
    dst[0] = 0x0000;
    run_model(src, 2, 2, dst, 2, 2, 0x0F0F, 0xFFFF, 0xFFFF, 1, 1, 2, 3, 0);
    CHECK(dst[0] == 0x0101, "one word: %04x", dst[0]);

    /* skew 4, no FXSR: the image moves right four pixels, the first
     * word taking zeros from the empty buffer on its left */
    src[0] = 0x1234; src[1] = 0x5678;
    dst[0] = dst[1] = 0;
    run_model(src, 2, 2, dst, 2, 2, 0xFFFF, 0xFFFF, 0xFFFF, 2, 1, 2, 3, 4);
    CHECK(dst[0] == 0x0123 && dst[1] == 0x4567,
          "skew right: %04x %04x", dst[0], dst[1]);

    /* FXSR with skew 12 is a move four pixels left: the priming
     * fetch supplies the first word's high half */
    src[0] = 0x1234; src[1] = 0x5678; src[2] = 0x9ABC;
    dst[0] = dst[1] = 0;
    run_model(src, 2, 2, dst, 2, 2, 0xFFFF, 0xFFFF, 0xFFFF, 2, 1, 2, 3,
              0x80 | 12);
    CHECK(dst[0] == 0x2345 && dst[1] == 0x6789,
          "FXSR left: %04x %04x", dst[0], dst[1]);

    /* stepping: every fetch and write adds the x increment but the
     * last of a line, which adds the y increment; two lines of two
     * words at a stride of eight words, with FXSR's extra fetch */
    {
        static uint16_t s2[32], d2[32];
        const int16_t syi = (int16_t)(16 - 2 * 2);  /* 3 reads a line */
        const int16_t dyi = (int16_t)(16 - 1 * 2);  /* 2 writes       */
        run_model(s2, 2, syi, d2, 2, dyi, 0xFFFF, 0xFFFF, 0xFFFF,
                  2, 2, 2, 3, 0x80 | 8);
        CHECK(stdl_host_blit.src_addr == (uintptr_t)(s2 + 16),
              "source advances one stride a line with FXSR");
        CHECK(stdl_host_blit.dst_addr == (uintptr_t)(d2 + 16),
              "destination advances one stride a line");
    }

    /* HOP all-ones: OP 3 writes ones, OP 0 zeros, the source unread */
    dst[0] = 0x0000; dst[1] = 0xFFFF;
    run_model(NULL, 0, 0, dst, 2, 2, 0xFFFF, 0xFFFF, 0xFFFF, 1, 1, 0, 3, 0);
    run_model(NULL, 0, 0, dst + 1, 2, 2, 0xFFFF, 0xFFFF, 0xFFFF, 1, 1, 0, 0,
              0);
    CHECK(dst[0] == 0xFFFF && dst[1] == 0x0000, "fills: %04x %04x",
          dst[0], dst[1]);

    /* XOR-AND-XOR, the masked-blit sequence: d ^= s; d &= m; d ^= s
     * leaves d where m is set and s where it is clear */
    {
        uint16_t s = 0x1234, m = 0xF0F0, d = 0xABCD;
        run_model(&s, 2, 2, &d, 2, 2, 0xFFFF, 0xFFFF, 0xFFFF, 1, 1, 2, 6, 0);
        run_model(&m, 2, 2, &d, 2, 2, 0xFFFF, 0xFFFF, 0xFFFF, 1, 1, 2, 1, 0);
        run_model(&s, 2, 2, &d, 2, 2, 0xFFFF, 0xFFFF, 0xFFFF, 1, 1, 2, 6, 0);
        CHECK(d == ((0xABCD & 0xF0F0) | (0x1234 & 0x0F0F)),
              "xor-and-xor: %04x", d);
    }
}

/* --- the library, CPU against BLiTTER ---------------------------- */

static void randomise(STDL_Surface *s, int maxcol)
{
    int x, y;

    for (y = 0; y < s->h; y++) {
        for (x = 0; x < s->w; x++) {
            STDL_PutPixel(s, x, y, (uint8_t)(rnd() % maxcol));
        }
    }
}

static int same(const STDL_Surface *a, const STDL_Surface *b)
{
    if (memcmp(a->pixels, b->pixels, (size_t)a->stride * a->h) != 0) {
        return 0;
    }
    if (a->mask != NULL
        && memcmp(a->mask, b->mask, (size_t)a->maskstride * a->h) != 0) {
        return 0;
    }
    return 1;
}

static void copy_into(STDL_Surface *to, const STDL_Surface *from)
{
    memcpy(to->pixels, from->pixels, (size_t)from->stride * from->h);
    if (from->mask != NULL) {
        memcpy(to->mask, from->mask, (size_t)from->maskstride * from->h);
    }
}

/* the overscan policy's contract: how many lines may run now; 0
 * means run the whole thing in shared mode */
static uint16_t random_policy(uint16_t nlines, uint32_t cpl)
{
    (void)cpl;
    return (uint16_t)(rnd() % (nlines + 1u));
}

static void library_pass(int budget, int split)
{
    const int maxcol = 1 << budget;
    STDL_Surface *src_plain = STDL_CreateSurface(160, 48);
    STDL_Surface *src_keyed = STDL_CreateSurface(160, 48);
    STDL_Surface *da = STDL_CreateSurface(320, 120);
    STDL_Surface *db = STDL_CreateSurface(320, 120);
    unsigned long ops0 = stdl_host_blit_ops;
    int i, bad = 0;

    STDL_SetPlaneBudget(budget);
    randomise(src_plain, maxcol);
    randomise(src_keyed, maxcol < 6 ? maxcol : 6);
    STDL_SetColourKey(src_keyed, 1, (uint8_t)(maxcol > 3 ? 3 : 1));
    randomise(da, maxcol);
    STDL_CreateMask(da, 1);
    STDL_CreateMask(db, 1);
    copy_into(db, da);
    stdl_blit_policy = split ? random_policy : NULL;

    for (i = 0; i < 400 && bad < 4; i++) {
        int op = (int)(rnd() % 3);
        STDL_Rect ra, rb;

        if (op == 0) {
            uint8_t col = (uint8_t)(rnd() % (maxcol + 1));
            if (col == maxcol) {
                col = STDL_TRANSPARENT;
            }
            ra.x = (int16_t)((int)(rnd() % 360) - 20);
            ra.y = (int16_t)((int)(rnd() % 140) - 10);
            ra.w = (uint16_t)(rnd() % 200);
            ra.h = (uint16_t)(rnd() % 80);
            rb = ra;
            STDL_UseBlitter(0);
            STDL_FillRect(da, &ra, col);
            STDL_UseBlitter(1);
            STDL_FillRect(db, &rb, col);
        } else {
            STDL_Surface *src = (op == 1) ? src_plain : src_keyed;
            int phase = (int)(rnd() & 15);
            STDL_Rect sr;

            sr.x = (int16_t)(phase + 16 * (int)(rnd() % 3));
            sr.y = (int16_t)(rnd() % 20);
            sr.w = (uint16_t)(rnd() % 160);
            sr.h = (uint16_t)(rnd() % 48);
            ra.x = (int16_t)(phase + 16 * ((int)(rnd() % 22) - 1));
            ra.y = (int16_t)((int)(rnd() % 130) - 10);
            rb = ra;
            STDL_UseBlitter(0);
            STDL_BlitSurface(src, &sr, da, &ra);
            STDL_UseBlitter(1);
            STDL_BlitSurface(src, &sr, db, &rb);
        }
        if (!same(da, db)) {
            bad++;
            CHECK(0, "budget %d%s: op %d at iteration %d differs",
                  budget, split ? " split" : "", op, i);
            copy_into(db, da);
        }
        CHECK(ra.x == rb.x && ra.y == rb.y && ra.w == rb.w
              && ra.h == rb.h, "clip writeback differs at %d", i);
    }
    /* a model that never ran would pass everything above */
    CHECK(stdl_host_blit_ops > ops0 + 100,
          "budget %d: only %lu BLiTTER operations ran", budget,
          stdl_host_blit_ops - ops0);

    stdl_blit_policy = NULL;
    STDL_SetPlaneBudget(4);
    STDL_FreeSurface(src_plain);
    STDL_FreeSurface(src_keyed);
    STDL_FreeSurface(da);
    STDL_FreeSurface(db);
}

int main(void)
{
    test_model();

    stdl.mach.has_blitter = 1;
    stdl_blit_force = 1;
    library_pass(4, 0);
    library_pass(2, 0);
    library_pass(3, 0);
    library_pass(4, 1);
    stdl_blit_force = 0;
    library_pass(4, 0);         /* the thresholds' own choices */

    if (failures == 0) {
        printf("BLiTTER model and paths: OK\n");
        return 0;
    }
    printf("%d failure(s)\n", failures);
    return 1;
}
