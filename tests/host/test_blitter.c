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

    /* skew 4, no FXSR: the image moves right four pixels; the first
     * word's top four bits are whatever the buffer last held, so
     * endmask 1 trims them */
    src[0] = 0x1234; src[1] = 0x5678;
    dst[0] = dst[1] = 0;
    run_model(src, 2, 2, dst, 2, 2, 0x0FFF, 0xFFFF, 0xFFFF, 2, 1, 2, 3, 4);
    CHECK(dst[0] == 0x0123 && dst[1] == 0x4567,
          "skew right: %04x %04x", dst[0], dst[1]);
    /* ...and they are there: open endmask 1 and the last word fetched
     * (0x5678) shows its low four bits */
    dst[0] = dst[1] = 0;
    run_model(src, 2, 2, dst, 2, 2, 0xFFFF, 0xFFFF, 0xFFFF, 2, 1, 2, 3, 4);
    CHECK(dst[0] == 0x8123 && dst[1] == 0x4567,
          "skew carry: %04x %04x", dst[0], dst[1]);

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
            /* half the blits at independent phases: the skewed
             * BLiTTER passes, both shift directions */
            int dphase = (rnd() & 1) ? phase : (int)(rnd() & 15);
            STDL_Rect sr;

            sr.x = (int16_t)(phase + 16 * (int)(rnd() % 3));
            sr.y = (int16_t)(rnd() % 20);
            sr.w = (uint16_t)(rnd() % 160);
            sr.h = (uint16_t)(rnd() % 48);
            ra.x = (int16_t)(dphase + 16 * ((int)(rnd() % 22) - 1));
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

/*
 * Sprites: the CPU loops against the BLiTTER's two passes a plane,
 * unshifted (the chip shifts) and pre-shifted, widths 1-80 with
 * multi-frame strips, random positions and clip rectangles. Under
 * ASan this is also the check that a skewed pass reading one word
 * past a sprite's last row stays inside the data's slack.
 */
/* wide_art: the sprite is built at budget 4 from all sixteen colours
 * and drawn at `budget` - bank art is not normalised when it loads, so
 * the BLiTTER must draw the planes the CPU does, not just the budget */
static void sprite_pass(int budget, int wide_art)
{
    const int maxcol = 1 << budget;
    unsigned long ops0 = stdl_host_blit_ops;
    int i, bad = 0;

    STDL_SetPlaneBudget(budget);
    for (i = 0; i < 150 && bad < 4; i++) {
        int w = 1 + (int)(rnd() % 80), h = 1 + (int)(rnd() % 24);
        int nframes = ((w & 15) == 0 && (rnd() & 1)) ? 3 : 1;
        int frame = (int)(rnd() % nframes);
        int pre = (int)(rnd() & 1);
        STDL_Surface *img = STDL_CreateSurface(w * nframes, h);
        STDL_Surface *da = STDL_CreateSurface(160, 64);
        STDL_Surface *db = STDL_CreateSurface(160, 64);
        STDL_Sprite *spr;
        STDL_Rect clip;
        int x = (int)(rnd() % (160 + w + 16)) - w - 8;
        int y = (int)(rnd() % (64 + h + 8)) - h - 4;

        if (wide_art) {
            STDL_SetPlaneBudget(4);
            randomise(img, 16);
            STDL_SetColourKey(img, 1, 14);
        } else {
            randomise(img, maxcol);
            STDL_SetColourKey(img, 1, (uint8_t)(maxcol - 2));
        }
        randomise(da, maxcol);
        copy_into(db, da);
        clip.x = (int16_t)(rnd() % 40);
        clip.y = (int16_t)(rnd() % 20);
        clip.w = (uint16_t)(10 + rnd() % 120);
        clip.h = (uint16_t)(5 + rnd() % 44);
        STDL_SetClipRect(da, &clip);
        STDL_SetClipRect(db, &clip);
        spr = STDL_SpriteFromSurface(img, w, pre ? STDL_PRESHIFT : 0);
        STDL_SetPlaneBudget(budget);
        if (spr == NULL) {
            CHECK(0, "sprite build");
            break;
        }
        STDL_UseBlitter(0);
        STDL_BlitSprite(spr, frame, da, x, y);
        STDL_UseBlitter(1);
        STDL_BlitSprite(spr, frame, db, x, y);
        if (!same(da, db)) {
            bad++;
            CHECK(0, "sprite budget %d%s w=%d h=%d f=%d/%d pre=%d x=%d "
                  "y=%d clip=%d,%d %ux%u differs", budget,
                  wide_art ? " (art past it)" : "", w, h, frame,
                  nframes, pre, x, y, clip.x, clip.y, clip.w, clip.h);
        }
        STDL_FreeSprite(spr);
        STDL_FreeSurface(img);
        STDL_FreeSurface(da);
        STDL_FreeSurface(db);
    }
    CHECK(stdl_host_blit_ops > ops0 + 40,
          "sprites budget %d: only %lu BLiTTER operations ran", budget,
          stdl_host_blit_ops - ops0);
    STDL_SetPlaneBudget(4);
}

/*
 * A blit onto its own memory, every way and on every path: two surfaces
 * borrowed over one block, the source keyed by a mask of its own or
 * not, the destination with a mask of its own or not, so every row
 * path meets it - whole groups in short rows and long (a tail, and one
 * pass), partial edges, shifted, keyed, into a masked destination,
 * under UNDER and MARK, and one row whose overlap lies in a group
 * past the source's span - moved left, right, up, down and four ways
 * between, with the BLiTTER refused, with it forced, and on two planes
 * of four. The answer is
 * the same blit from a copy of the block, which overlaps nothing.
 */
enum { IW = 288, IH = 40, IS = IW / 2, IMS = IW / 8 };
static uint8_t ipix[IS * IH + 16], iref[IS * IH + 16], icpy[IS * IH + 16];
static uint16_t ism[IMS / 2 * IH], ismc[IMS / 2 * IH];
static uint16_t idm[IMS / 2 * IH], idmr[IMS / 2 * IH];

static void inplace_pass(int blitter)
{
    static const struct {
        int16_t sx, w;
        uint8_t h, keyed, dmask;
        unsigned flags;
    } cf[] = {
        { 16, 48, 16, 0, 0, 0 },        /* whole groups, short rows */
        { 16, 208, 16, 0, 0, 0 },       /* long rows and a tail */
        { 16, 160, 16, 0, 0, 0 },       /* long rows, one pass */
        { 16, 48, 16, 0, 1, 0 },        /* short rows, masked dst */
        { 16, 160, 16, 0, 1, 0 },       /* long rows, masked dst */
        { 21, 100, 16, 0, 0, 0 },       /* partial edges */
        { 16, 100, 16, 0, 0, 0 },       /* from a group, a ragged end */
        { 21, 100, 16, 1, 0, 0 },       /* keyed */
        { 16, 112, 16, 1, 0, 0 },       /* keyed, whole groups */
        { 16, 112, 16, 1, 1, 0 },       /* keyed into a masked dst */
        { 21, 100, 16, 0, 1, STDL_BLIT_UNDER },
        { 21, 100, 16, 1, 1, STDL_BLIT_MARK },
        /* one row keyed, 18 and 20 wide from a group's start: moved
         * left by 17 their last pixels land on the source's first, in a
         * group past the source's span */
        { 48, 18, 1, 1, 0, 0 },
        { 64, 20, 1, 1, 0, 0 },
    };
    static const int16_t mv[][2] = {
        { -16, 0 }, { 16, 0 }, { 0, -3 }, { 0, 3 }, { -5, 0 }, { 5, 0 },
        { -5, -2 }, { 5, 2 }, { 32, -1 }, { -3, 1 }, { 48, 0 }, { 1, 0 },
        { 0, 1 }, { 16, 16 }, { -17, 0 }
    };
    const unsigned long ops0 = stdl_host_blit_ops;
    int c, m;
    unsigned i;

    for (c = 0; c < (int)(sizeof cf / sizeof cf[0]); c++) {
        for (m = 0; m < (int)(sizeof mv / sizeof mv[0]); m++) {
            STDL_Surface *src, *dst, *cs, *rd;
            STDL_Rect sr, at, at2;

            for (i = 0; i < sizeof ipix; i++) {
                ipix[i] = (uint8_t)rnd();
            }
            for (i = 0; i < sizeof ism / 2; i++) {
                ism[i] = (uint16_t)(rnd() * 7);
                idm[i] = (uint16_t)(rnd() * 5);
            }
            memcpy(iref, ipix, sizeof ipix);
            memcpy(icpy, ipix, sizeof ipix);
            memcpy(ismc, ism, sizeof ism);
            memcpy(idmr, idm, sizeof idm);
            src = STDL_CreateSurfaceFrom(ipix, IW, IH, IS,
                                         cf[c].keyed ? (uint8_t *)ism : NULL,
                                         IMS);
            dst = STDL_CreateSurfaceFrom(ipix, IW, IH, IS,
                                         cf[c].dmask ? (uint8_t *)idm : NULL,
                                         IMS);
            cs = STDL_CreateSurfaceFrom(icpy, IW, IH, IS,
                                        cf[c].keyed ? (uint8_t *)ismc : NULL,
                                        IMS);
            rd = STDL_CreateSurfaceFrom(iref, IW, IH, IS,
                                        cf[c].dmask ? (uint8_t *)idmr : NULL,
                                        IMS);
            sr.x = cf[c].sx;
            sr.y = 12;
            sr.w = (uint16_t)cf[c].w;
            sr.h = cf[c].h;
            at.x = (int16_t)(cf[c].sx + mv[m][0]);
            at.y = (int16_t)(12 + mv[m][1]);
            at2 = at;
            STDL_UseBlitter(0);
            STDL_BlitSurfaceEx(cs, &sr, rd, &at2, cf[c].flags);
            STDL_UseBlitter(blitter);
            STDL_BlitSurfaceEx(src, &sr, dst, &at, cf[c].flags);
            CHECK(memcmp(ipix, iref, sizeof ipix) == 0
                  && memcmp(idm, idmr, sizeof idm) == 0,
                  "onto itself, BLiTTER %s: %d wide from x %d, keyed %d, "
                  "masked dst %d, flags %u, by %d,%d", blitter ? "forced"
                  : "refused", cf[c].w, cf[c].sx, cf[c].keyed, cf[c].dmask,
                  cf[c].flags, mv[m][0], mv[m][1]);
            STDL_FreeSurface(src);
            STDL_FreeSurface(dst);
            STDL_FreeSurface(cs);
            STDL_FreeSurface(rd);
            if (failures > 8) {
                return;
            }
        }
    }
    CHECK(!blitter || stdl_host_blit_ops > ops0 + 20,
          "onto itself: only %lu BLiTTER operations ran",
          stdl_host_blit_ops - ops0);
    STDL_UseBlitter(1);
}

/*
 * rowcopy.c's own test of an overlap, for strides of either sign: rows
 * of 40 to 400 bytes, one to five of them, 160 to 400 apart, moved by
 * a row or less either way. Whatever it takes on must come out as a
 * copy from a snapshot would; what it declines must be untouched. The
 * bottom-up blit only ever hands it rows past their own source (the
 * classifier sees to that), so this is the one place its test for
 * negative strides is seen to decline.
 */
static uint8_t rcbuf[4096], rcref[4096], rcsnap[4096];

static void test_rows_checked(void)
{
    static const int16_t bytes[] = { 40, 80, 160, 320, 360, 400 };
    static const int16_t strides[] = { 160, 320, 400 };
    static const int16_t shifts[] = { 0, 8, 40, 152, 160, 168, 320 };
    int b, h, s, t, sign, side, took = 0, declined = 0;
    unsigned i;

    for (b = 0; b < (int)(sizeof bytes / sizeof bytes[0]); b++)
    for (h = 1; h <= 5; h += 2)
    for (s = 0; s < (int)(sizeof strides / sizeof strides[0]); s++)
    for (t = 0; t < (int)(sizeof shifts / sizeof shifts[0]); t++)
    for (sign = -1; sign <= 1; sign += 2)
    for (side = -1; side <= 1; side += 2) {
        const int st = strides[s] * sign;
        const int base = sign > 0 ? 1200 : 1200 + 4 * strides[s];
        uint8_t *sp = rcbuf + base;
        uint8_t *dp = sp + shifts[t] * side;
        int r;

        if (bytes[b] > strides[s] || (shifts[t] == 0 && side > 0)) {
            continue;
        }
        for (i = 0; i < sizeof rcbuf; i++) {
            rcbuf[i] = (uint8_t)(i * 7 + 3 + (i >> 8));
        }
        memcpy(rcsnap, rcbuf, sizeof rcbuf);
        memcpy(rcref, rcbuf, sizeof rcbuf);
        if (stdl_copy_rows_checked(dp, sp, bytes[b], h, st, st) != 0) {
            CHECK(memcmp(rcbuf, rcsnap, sizeof rcbuf) == 0,
                  "rows checked: %d bytes, %d rows, stride %d, by %d: "
                  "declined, and wrote", bytes[b], h, st,
                  shifts[t] * side);
            declined++;
            continue;
        }
        for (r = 0; r < h; r++) {
            memcpy(rcref + (dp - rcbuf) + r * st,
                   rcsnap + (sp - rcbuf) + r * st, (size_t)bytes[b]);
        }
        CHECK(memcmp(rcbuf, rcref, sizeof rcbuf) == 0,
              "rows checked: %d bytes, %d rows, stride %d, by %d: wrong",
              bytes[b], h, st, shifts[t] * side);
        took++;
    }
    CHECK(took > 100 && declined > 50,
          "rows checked: took %d, declined %d", took, declined);
}

int main(void)
{
    test_model();
    test_rows_checked();

    stdl.mach.has_blitter = 1;
    inplace_pass(0);
    STDL_SetPlaneBudget(2);         /* two planes moved, two left */
    inplace_pass(0);
    STDL_SetPlaneBudget(4);
    stdl_blit_force = 1;
    inplace_pass(1);
    library_pass(4, 0);
    library_pass(2, 0);
    library_pass(3, 0);
    library_pass(4, 1);
    sprite_pass(4, 0);
    sprite_pass(2, 0);
    sprite_pass(3, 0);
    sprite_pass(3, 1);
    sprite_pass(1, 1);
    stdl_blit_force = 0;
    library_pass(4, 0);         /* the thresholds' own choices */

    if (failures == 0) {
        printf("BLiTTER model and paths: OK\n");
        return 0;
    }
    printf("%d failure(s)\n", failures);
    return 1;
}
