/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * On-target cost sweep for the drawing a game does every frame:
 * sprites, the restores that erase them, unaligned blits and fills.
 * It is the baseline every pixel-path change is measured against, so
 * it prints what each number depends on as well as the number.
 *
 * Each case runs until it has taken at least MINTICKS of the 200Hz
 * clock, doubling its iteration count, and reports microseconds per
 * operation. With a BLiTTER it reports the library's own choice
 * ("lib") beside the CPU path forced ("cpu"); without one only the
 * CPU figure exists. The whole sweep runs twice in one process -
 * pass A and pass B - so every run carries its own same-binary
 * noise floor: a difference between two builds smaller than the
 * A/B spread is not a difference.
 *
 * Build by hand; keep the stem to eight characters (Hatari's GEMDOS
 * drive maps names to 8.3):
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Iinclude -Ilib/xpad/src \
 *       -o dist/SPRCOST.TOS tests/hatari/sprcost.c libstdl.a
 *   TOS=$PWD/tmp/tos/etos256uk.img MACHINE=st EXTRA="--memsize 4" \
 *       tests/hatari/run.sh sc dist/SPRCOST.TOS 8 "waitfor SC:done"
 *
 * MACHINE=st first - it is the machine the library is for - then
 * ste (both columns) and megaste. For a comparison between two
 * builds, also link the same objects in reverse order and run that
 * too: the spread it shows is the layout floor, and a claimed win
 * has to clear both floors (see "Measuring" in the stdl skill).
 * Summarise with the geometric mean over every case, never a
 * single line.
 */
#include <stdio.h>
#include <string.h>
#include <stdl/stdl.h>

#define MINTICKS 120            /* 0.6s of the 200Hz clock per case */

static STDL_Surface *dst, *bg, *src_plain, *src_keyed;
static int have_blitter;

static uint32_t rng = 0x2468ACE1UL;
static uint32_t rnd(void)
{
    rng = rng * 1103515245UL + 12345UL;
    return (rng >> 16) & 0x7FFF;
}

static void randomise(STDL_Surface *s, int colours)
{
    int x, y;

    for (y = 0; y < s->h; y++) {
        for (x = 0; x < s->w; x++) {
            STDL_PutPixel(s, x, y, (uint8_t)(rnd() % colours));
        }
    }
}

/* --- the operation under test ------------------------------------ */

typedef void (*op_fn)(int i);

/* per-case parameters, set before each measurement */
static STDL_Sprite *c_spr;
static int c_x, c_y, c_w, c_h, c_sx, c_phasewalk;
static uint8_t c_col;
static STDL_Surface *c_src;

static void op_sprite(int i)
{
    STDL_BlitSprite(c_spr, 0, dst,
                    c_x + (c_phasewalk ? (i & 15) : 0), c_y);
}

static void op_restore(int i)
{
    STDL_Rect r;

    (void)i;
    r.x = (int16_t)c_x;
    r.y = (int16_t)c_y;
    r.w = (uint16_t)c_w;
    r.h = (uint16_t)c_h;
    {
        STDL_Rect d = r;
        STDL_BlitSurface(bg, &r, dst, &d);
    }
}

static void op_dirty(int i)
{
    int k;

    (void)i;
    for (k = 0; k < 16; k++) {
        STDL_Rect r;
        r.x = (int16_t)(21 + (k & 3) * 72 + k);
        r.y = (int16_t)(13 + (k >> 2) * 44);
        r.w = 16;
        r.h = 16;
        STDL_DirtyPush(&r);
    }
    STDL_DirtyRestore(dst);
}

static void op_blit(int i)
{
    STDL_Rect s, d;

    (void)i;
    s.x = (int16_t)c_sx;
    s.y = 0;
    s.w = (uint16_t)c_w;
    s.h = (uint16_t)c_h;
    d.x = (int16_t)c_x;
    d.y = (int16_t)c_y;
    d.w = 0;
    d.h = 0;
    STDL_BlitSurface(c_src, &s, dst, &d);
}

static void op_fill(int i)
{
    STDL_Rect r;

    (void)i;
    r.x = (int16_t)c_x;
    r.y = (int16_t)c_y;
    r.w = (uint16_t)c_w;
    r.h = (uint16_t)c_h;
    STDL_FillRect(dst, &r, c_col);
}

static void op_noop(int i)
{
    (void)i;
    __asm__ volatile("" ::: "memory");
}

/* tenths of a microsecond per operation */
static uint32_t measure(op_fn f)
{
    uint32_t n = 4, t;

    for (;;) {
        uint32_t i, t0 = STDL_GetHz200();

        for (i = 0; i < n; i++) {
            f((int)i);
        }
        t = STDL_GetHz200() - t0;
        if (t >= MINTICKS || n >= 0x40000000UL) {
            break;
        }
        n <<= 1;
    }
    return (t * 50000UL) / n;
}

static char pass_name;

static void report(const char *label, op_fn f)
{
    uint32_t cpu, lib;

    STDL_UseBlitter(0);
    cpu = measure(f);
    if (have_blitter) {
        STDL_UseBlitter(1);
        lib = measure(f);
        printf("SC:%c %-26s lib=%6lu.%lu cpu=%6lu.%lu\n", pass_name,
               label, (unsigned long)(lib / 10), (unsigned long)(lib % 10),
               (unsigned long)(cpu / 10), (unsigned long)(cpu % 10));
    } else {
        printf("SC:%c %-26s cpu=%6lu.%lu\n", pass_name, label,
               (unsigned long)(cpu / 10), (unsigned long)(cpu % 10));
    }
    fflush(stdout);
    STDL_UseBlitter(1);
}

/* --- sprites ------------------------------------------------------ */

typedef struct {
    int w, h;
    const char *shape;
    STDL_Sprite *plain, *pre;
} sprset_t;

static sprset_t sprites[6];
static int nsprites;

static void make_sprites(void)
{
    static const int sz[3][2] = { { 16, 16 }, { 32, 32 }, { 64, 32 } };
    int k, ring;

    for (k = 0; k < 3; k++) {
        for (ring = 0; ring <= 1; ring++) {
            int w = sz[k][0], h = sz[k][1];
            int r = (h < w ? h : w) / 2 - 1;
            STDL_Surface *s = STDL_CreateSurface(w, h);
            STDL_Rect all = { 0, 0, 0, 0 };
            sprset_t *e = &sprites[nsprites++];

            all.w = (uint16_t)w;
            all.h = (uint16_t)h;
            STDL_FillRect(s, &all, 0);
            if (ring) {
                /* mostly transparent: a two-pixel outline */
                STDL_Circle(s, w / 2, h / 2, r, 5);
                STDL_Circle(s, w / 2, h / 2, r - 1, 11);
            } else {
                /* mostly opaque: a disc with a second colour inside,
                 * widened to the frame for the 64x32 case */
                STDL_FillCircle(s, w / 2, h / 2, r, 9);
                if (w > h) {
                    STDL_Rect bar;
                    bar.x = (int16_t)(h / 2);
                    bar.y = 2;
                    bar.w = (uint16_t)(w - h);
                    bar.h = (uint16_t)(h - 4);
                    STDL_FillRect(s, &bar, 9);
                }
                STDL_FillCircle(s, w / 2, h / 2, r / 2, 12);
            }
            STDL_SetColourKey(s, 1, 0);
            e->w = w;
            e->h = h;
            e->shape = ring ? "ring" : "disc";
            e->plain = STDL_SpriteFromSurface(s, w, 0);
            e->pre = STDL_SpriteFromSurface(s, w, STDL_PRESHIFT);
            STDL_FreeSurface(s);
        }
    }
}

static void sprite_cases(int subset)
{
    int k;
    char label[40];

    for (k = 0; k < nsprites; k++) {
        sprset_t *e = &sprites[k];

        if (e->plain == NULL) {
            continue;
        }
        if (subset && !(e->w == 32 && e->shape[0] == 'd')) {
            continue;
        }
        c_spr = e->plain;
        c_y = 60;
        c_phasewalk = 0;
        if (!subset) {
            static const int ph[4] = { 0, 1, 8, 15 };
            int p;
            for (p = 0; p < 4; p++) {
                c_x = 64 + ph[p];
                sprintf(label, "spr %dx%d %s ph%d", e->w, e->h,
                        e->shape, ph[p]);
                report(label, op_sprite);
            }
        }
        c_x = 64;
        c_phasewalk = 1;
        sprintf(label, "spr %dx%d %s mean", e->w, e->h, e->shape);
        report(label, op_sprite);
        if (e->pre != NULL) {
            c_spr = e->pre;
            sprintf(label, "pre %dx%d %s mean", e->w, e->h, e->shape);
            report(label, op_sprite);
        }
        if (!subset) {
            c_spr = e->plain;
            c_phasewalk = 0;
            c_x = 5 - e->w / 2;
            sprintf(label, "spr %dx%d %s clipL", e->w, e->h, e->shape);
            report(label, op_sprite);
        }
    }
}

/* --- restores, blits, fills --------------------------------------- */

static void restore_cases(int subset)
{
    static const int sz[3][2] = { { 16, 16 }, { 32, 32 }, { 48, 24 } };
    char label[40];
    int k, odd;

    for (k = 0; k < 3; k++) {
        if (subset && k != 1) {
            continue;
        }
        for (odd = 0; odd <= 1; odd++) {
            c_w = sz[k][0];
            c_h = sz[k][1];
            c_x = odd ? 37 : 32;
            c_y = 40;
            sprintf(label, "restore %dx%d %s", c_w, c_h,
                    odd ? "x&15=5" : "aligned");
            report(label, op_restore);
        }
    }
    if (!subset && STDL_DirtyInit(bg, 32) == 0) {
        report("dirty 16 x 16x16", op_dirty);
        STDL_DirtyQuit();
    }
}

static void blit_cases(int subset)
{
    static const int sz[4][2] = { { 16, 16 }, { 32, 16 }, { 64, 32 },
                                  { 160, 64 } };
    char label[40];
    int k, keyed, left;

    for (k = 0; k < 4; k++) {
        if (subset && k != 2) {
            continue;
        }
        for (keyed = 0; keyed <= 1; keyed++) {
            for (left = 0; left <= 1; left++) {
                c_src = keyed ? src_keyed : src_plain;
                c_w = sz[k][0];
                c_h = sz[k][1];
                /* right: source phase 0 onto phase 5; left: 11 onto 3 */
                c_sx = left ? 11 : 0;
                c_x = left ? 35 : 37;
                c_y = 30;
                sprintf(label, "blit %dx%d %s %s", c_w, c_h,
                        keyed ? "keyed" : "plain",
                        left ? "left" : "right");
                report(label, op_blit);
            }
        }
    }
    if (!subset) {
        /* the fixed cost, taken apart: the probe's own call, a blit
         * clipped to nothing (entry and clipping only), and a blit
         * with one row of one group to move */
        report("noop (probe overhead)", op_noop);
        c_src = src_plain;
        c_w = 16;
        c_h = 1;
        c_sx = 0;
        c_x = 400;
        c_y = 30;
        report("blit clipped out (entry)", op_blit);
        /* the fixed cost: a blit with almost nothing to move */
        c_src = src_plain;
        c_w = 16;
        c_h = 1;
        c_sx = 0;
        c_x = 32;
        c_y = 30;
        report("blit 16x1 aligned (fixed)", op_blit);
    }
}

static void fill_cases(int subset)
{
    static const int sz[3][2] = { { 16, 16 }, { 64, 32 }, { 320, 200 } };
    static const uint8_t cols[3] = { 0, 15, 5 };
    char label[40];
    int k, c, odd;

    for (k = 0; k < 3; k++) {
        if (subset && k != 1) {
            continue;
        }
        for (c = 0; c < 3; c++) {
            for (odd = 0; odd <= 1; odd++) {
                if (k == 2 && odd) {
                    continue;       /* a full screen cannot be offset */
                }
                c_w = sz[k][0];
                c_h = sz[k][1];
                c_x = (k == 2) ? 0 : (odd ? 35 : 32);
                c_y = (k == 2) ? 0 : 20;
                c_col = cols[c];
                sprintf(label, "fill %dx%d c%d %s", c_w, c_h, c_col,
                        odd ? "x&15=3" : "aligned");
                report(label, op_fill);
            }
        }
    }
    if (!subset) {
        c_w = 1;
        c_h = 1;
        c_x = 33;
        c_y = 20;
        c_col = 5;
        report("fill 1x1 (fixed)", op_fill);
        c_x = 400;
        report("fill clipped out (entry)", op_fill);
    }
}

static void sweep(int subset)
{
    sprite_cases(subset);
    restore_cases(subset);
    blit_cases(subset);
    fill_cases(subset);
}

int main(void)
{
    const STDL_MachineInfo *mi;
    int k;

    if (STDL_Init(STDL_INIT_VIDEO) < 0
        || STDL_SetVideoMode(320, 200, 4, 0) == NULL) {
        fprintf(stderr, "init failed: %s\n", STDL_GetError());
        return 1;
    }
    mi = STDL_GetMachineInfo();
    have_blitter = mi->has_blitter;

    dst = STDL_CreateSurface(320, 200);
    bg = STDL_CreateSurface(320, 200);
    src_plain = STDL_CreateSurface(176, 72);
    src_keyed = STDL_CreateSurface(176, 72);
    if (dst == NULL || bg == NULL || src_plain == NULL
        || src_keyed == NULL) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    randomise(bg, 16);
    randomise(dst, 16);
    randomise(src_plain, 16);
    randomise(src_keyed, 6);
    STDL_SetColourKey(src_keyed, 1, 3);
    make_sprites();

    /* everything a number here depends on and no picture shows */
    printf("SC: machine ste=%d megaste=%d blitter=%d mch=%08lx\n",
           mi->is_ste, mi->is_megaste, mi->has_blitter,
           (unsigned long)mi->mch_cookie);
    printf("SC: rows long aligned dst=%d bg=%d src=%d\n",
           ((uintptr_t)dst->pixels & 3) == 0,
           ((uintptr_t)bg->pixels & 3) == 0,
           ((uintptr_t)src_plain->pixels & 3) == 0);
    for (k = 0; k < nsprites; k++) {
        printf("SC: sprite %dx%d %s data %s, preshift %s\n",
               sprites[k].w, sprites[k].h, sprites[k].shape,
               sprites[k].plain == NULL ? "MISSING"
               : (((uintptr_t)sprites[k].plain->data & 3) == 0
                  ? "long" : "word"),
               sprites[k].pre == NULL ? "MISSING" : "ok");
    }
    printf("SC: units: microseconds per operation\n");
    fflush(stdout);

    for (pass_name = 'A'; pass_name <= 'B'; pass_name++) {
        sweep(0);
    }

    /*
     * The same subset with the bottom border open, where every
     * BLiTTER operation is placed against the beam first. Whether the
     * BLiTTER still pays there is a measurement, not a given.
     */
    {
        int h = STDL_OpenBottomBorder();
        printf("SC: bottom border %s (%d rows)\n",
               h > 200 ? "open" : "NOT open", h);
        if (h > 200) {
            pass_name = 'O';
            sweep(1);
            printf("SC: border misses %lu\n",
                   (unsigned long)STDL_OverscanMisses());
            STDL_CloseBottomBorder();
        }
    }

    printf("SC:done\n");
    fflush(stdout);
    STDL_Quit();
    return 0;
}
