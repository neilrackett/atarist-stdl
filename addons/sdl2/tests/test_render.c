/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The renderer against a model of its screen: a chunky 320x240 that
 * each copy and clear is drawn into a pixel at a time, as the screen
 * should then hold it, with none of the renderer's own arithmetic -
 * no groups, batches, masks or catching up. After every present that
 * flips, the page shown must be the model converted to planes byte
 * for byte, a pixel at a time, margins and all, and the registers the
 * palette permuted by the border index; a present with nothing drawn
 * must not flip, and a clear of the screen on show must be there at
 * once.
 *
 * A seeded run of the API: textures of the renderer's own and over a
 * program's buffer, odd sizes and pitches, copies with their sources
 * and destinations anywhere (off the texture, off the screen, a pixel
 * wide, unaligned, the size of the screen, scaled and so refused),
 * updates and locks, clears, palette changes, logical sizes, the
 * screen cleared at once. Before it, the sequences one port's screen
 * code relies on: boxes rounded to groups from a buffer of its own,
 * two buffers in one frame, a buffer pushed black and then revealed
 * two rows a step, the screen cleared at once, a fade's palette
 * staged without a present, and a copy clipped to nothing.
 *
 * On the host (fake_stdl.c) each of a TT, a Falcon on VGA and on RGB,
 * with two pages and with three, and border indices 0, 0x70 (its own
 * loops) and 0x30 (the run-time one), runs in a process of its own,
 * as the screen is set up once a program. On the machine, the one
 * screen the machine has, against the real STDL; the screen check is
 * on in both. It prints S2R:done; SDL2RCH2 is the same with two pages:
 *
 *   STCMD_NO_TTY=1 stcmd make sdl2
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -m68020-60 -msoft-float -Iinclude -Iaddons/sdl2/include/SDL2 \
 *       '-DSTDL_SDL2_TEST_PAGES="3"' -c -o rchk.o \
 *       addons/sdl2/tests/test_render.c
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -m68000 \
 *       -o dist/SDL2RCHK.TOS rchk.o libstdl-sdl2.a libstdl.a
 *   TOS="<TOS 3.06>" MACHINE=tt LATE="--mmu off" \
 *       EXTRA="--cpu-exact on --compatible on --memsize 4" \
 *       tests/hatari/run.sh s2r dist/SDL2RCHK.TOS 8 "waitfor S2R:done"
 *
 * Built with STDL_SDL2_ST it is the ST build's test (test_render_st on
 * the host: the tall screen's 240 rows, 200 where the border did not
 * open and 216 for one cut short, with two pages and three and border
 * indices 0 and 0x70): the page shown must be the model's middle rows put
 * through the renderer's map of the palette onto 16 colours, a pixel
 * at a time, and a present flips when that map has moved though
 * nothing was drawn. The map and the registers must be what the
 * reduction (st.c) makes of the palette, and the reduction is checked
 * for what it promises before the runs. On an ST:
 *
 *   STCMD_NO_TTY=1 stcmd make sdl2st
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 -m68000 \
 *       -DSTDL_SDL2_ST -Iinclude -Iaddons/sdl2/include/SDL2 \
 *       -Iaddons/sdl2/src -c -o rchk.o addons/sdl2/tests/test_render.c
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -m68000 \
 *       -o dist/SDL2RCST.TOS rchk.o libstdl-sdl2-st.a libstdl.a
 */

#include "SDL.h"
#include "SDL_stdl.h"
#ifdef STDL_SDL2_ST
#include "st.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef __m68k__
#include "fake_stdl.h"
#include <unistd.h>
#include <sys/wait.h>
#endif

static int failures;

#define CHECK(c) do { \
        if (!(c)) { \
            if (failures < 8) { \
                printf("FAIL %s:%d: %s (step %ld)\n", __FILE__, __LINE__, \
                       #c, step); \
            } \
            failures++; \
        } \
    } while (0)

static long step;
static unsigned long seed;

static unsigned rnd(void)
{
    seed = seed * 1103515245UL + 12345UL;
    return (unsigned)(seed >> 16) & 0x7FFF;
}

static int rndn(int n)
{
    return n > 0 ? (int)(rnd() % (unsigned)n) : 0;
}

/* ------------------------------------------------------------------ */
/* the model                                                           */

static Uint8 model[240][320];
static unsigned X;
static int lw, lh, xoff, yoff;
static int drawn;               /* a flip is due at the next present */

static SDL_Window *win;
static SDL_Renderer *R;
static SDL_Palette *pal;

typedef struct {
    SDL_Texture *t;
    Uint8 *px;                  /* where its pixels are */
    int w, h, pitch;
    int borrowed;
} Tex;

static Tex tex[8];
static int ntex;

static void model_place(int w, int h)
{
    lw = w;
    lh = h;
    xoff = ((320 - w) / 2) & ~15;
    yoff = (240 - h) / 2;
}

/* -1 when the copy would scale, else whether it drew anything */
static int model_copy(const Tex *t, const SDL_Rect *src, const SDL_Rect *dst)
{
    SDL_Rect s, d;
    int i, j, any = 0;
    if (src) {
        s = *src;
    } else {
        s.x = s.y = 0;
        s.w = t->w;
        s.h = t->h;
    }
    if (dst) {
        d = *dst;
    } else {
        d.x = d.y = 0;
        d.w = lw;
        d.h = lh;
    }
    if (s.w != d.w || s.h != d.h) {
        return -1;
    }
    for (j = 0; j < s.h; j++) {
        for (i = 0; i < s.w; i++) {
            const int tx = s.x + i, ty = s.y + j;
            const int lx = d.x + i, ly = d.y + j;
            if (tx < 0 || ty < 0 || tx >= t->w || ty >= t->h ||
                lx < 0 || ly < 0 || lx >= lw || ly >= lh) {
                continue;
            }
            model[ly + yoff][lx + xoff] = t->px[ty * t->pitch + tx];
            any = 1;
        }
    }
    return any;
}

static void model_fill(Uint8 c)
{
    memset(model, c, sizeof(model));
}

/* ------------------------------------------------------------------ */
/* what the screen should hold                                         */

static Uint8 *want;
static long want_bytes;

#ifdef STDL_SDL2_ST
/* the ST build: an ST's 16-colour page, or on a TT or a Falcon the
 * 256-colour one, as the other build has it */
static int is_st;
static int page_rows = 240;
#define PAGE_ROWS page_rows

/* the page's rows, the screen's middle ones, through the renderer's
 * map, four planes */
static void expect_page_st(long stride)
{
    const Uint8 *map = stdl_sdl2_st_map(R);
    int x, y, p;
    memset(want, 0, (size_t)want_bytes);
    for (y = 0; y < page_rows; y++) {
        for (x = 0; x < 320; x++) {
            const unsigned v = map[model[y + (240 - page_rows) / 2][x]];
            Uint8 *grp = want + y * stride + (x >> 4) * 8;
            for (p = 0; p < 4; p++) {
                if (v >> p & 1) {
                    grp[2 * p + ((x & 15) >> 3)] |= (Uint8)(0x80 >> (x & 7));
                }
            }
        }
    }
}
#else
#define is_st 0
#define PAGE_ROWS 240
#endif

static void expect_page(long stride, long margin, int dbl)
{
    const long dp = dbl ? stride / 2 : stride;
    int x, y, p;
    memset(want, 0, (size_t)want_bytes);
    for (y = 0; y < 240; y++) {
        for (x = 0; x < 320; x++) {
            const unsigned v = model[y][x] ^ X;
            Uint8 *grp = want + margin + y * stride + (x >> 4) * 16;
            const Uint8 bit = (Uint8)(0x80 >> (x & 7));
            for (p = 0; p < 8; p++) {
                if (v >> p & 1) {
                    grp[2 * p + ((x & 15) >> 3)] |= bit;
                    if (dbl) {
                        grp[dp + 2 * p + ((x & 15) >> 3)] |= bit;
                    }
                }
            }
        }
    }
}

static long stride_, margin_;
static int dbl_;
static const Uint8 *last_base;

/* the layout from the page's size: a TT's 640 bytes a row, a Falcon's
 * 320 on VGA and 384 on RGB, the picture in the middle */
static void check_shown(void)
{
    const Uint8 *base;
    long bytes;
    STDL_SDL2_GetShownPage(R, &base, &bytes);
    CHECK(base != NULL);
    if (!base) {
        return;
    }
    if (bytes != want_bytes) {
        free(want);
        want_bytes = bytes;
        want = (Uint8 *)malloc((size_t)bytes);
    }
#ifdef STDL_SDL2_ST
    page_rows = is_st ? (int)(bytes / 160) : 240;
#endif
    stride_ = bytes / PAGE_ROWS;
    dbl_ = stride_ == 640;
    margin_ = ((dbl_ ? stride_ / 2 : stride_) - 320) / 2;
#ifdef STDL_SDL2_ST
    if (is_st) {
        expect_page_st(stride_);
    } else
#endif
    expect_page(stride_, margin_, dbl_);
    if (memcmp(base, want, (size_t)bytes) != 0) {
        long i = 0;
        while (base[i] == want[i]) {
            i++;
        }
        if (failures < 8) {
            printf("FAIL the page shown, row %ld byte %ld (step %ld)\n",
                   i / stride_, i % stride_, step);
        }
        failures++;
    }
}

#ifdef STDL_SDL2_ST
/* the map the palette has now, and whether it is the renderer's */
static Uint8 now_map[256];
static SDL_Color now_regs[16];

static int map_moved(void)
{
    if (!is_st) {
        return 0;
    }
    memcpy(now_map, stdl_sdl2_st_map(R), 256);
    return stdl_sdl2_st_update(pal->colors, pal->ncolors, X, now_map,
                               now_regs);
}

/* registers and map: what the palette as last staged makes of them */
static void check_registers_st(void)
{
    const SDL_Color *regs = STDL_SDL2_GetRegisterColours(R);
    int i, bad = 0;
    bad += map_moved();
    for (i = 0; i < 16; i++) {
        if (regs[i].r != now_regs[i].r || regs[i].g != now_regs[i].g ||
            regs[i].b != now_regs[i].b) {
            bad++;
        }
#ifndef __m68k__
        if (fake_regs[i].r != now_regs[i].r ||
            fake_regs[i].g != now_regs[i].g ||
            fake_regs[i].b != now_regs[i].b) {
            bad++;
        }
#endif
    }
    CHECK(bad == 0);
}
#else
static int map_moved(void)
{
    return 0;
}
#endif

/* registers: colour i ^ X, register 0 black when X is set */
static void check_registers(void)
{
    const SDL_Color *regs = STDL_SDL2_GetRegisterColours(R);
    int i, bad = 0;
#ifdef STDL_SDL2_ST
    if (is_st) {
        check_registers_st();
        return;
    }
#endif
    for (i = 0; i < 256; i++) {
        const unsigned j = (unsigned)i ^ X;
        SDL_Color c = { 0, 0, 0, 255 };
        if (!(X && i == 0) && (int)j < pal->ncolors) {
            c = pal->colors[j];
        }
        if (regs[i].r != c.r || regs[i].g != c.g || regs[i].b != c.b) {
            bad++;
        }
#ifndef __m68k__
        if (fake_regs[i].r != c.r || fake_regs[i].g != c.g ||
            fake_regs[i].b != c.b) {
            bad++;
        }
#endif
    }
    CHECK(bad == 0);
}

static void present(void)
{
    const Uint8 *base;
    long bytes;
#ifndef __m68k__
    const int flips = fake_flips;
#endif
    /* the ST build flips for a new map alone */
    drawn |= map_moved();
    SDL_RenderPresent(R);
    STDL_SDL2_GetShownPage(R, &base, &bytes);
    if (drawn) {
        CHECK(base != last_base);
#ifndef __m68k__
        CHECK(fake_flips == flips + 1);
#endif
        check_shown();
    } else {
        CHECK(base == last_base);
#ifndef __m68k__
        CHECK(fake_flips == flips);
#endif
    }
    check_registers();
    last_base = base;
    drawn = 0;
}

static void copy(const Tex *t, const SDL_Rect *src, const SDL_Rect *dst)
{
    const int m = model_copy(t, src, dst);
    const int got = SDL_RenderCopy(R, t->t, src, dst);
    if (m < 0) {
        CHECK(got == -1);
    } else {
        CHECK(got == 0);
        drawn |= m;
    }
}

static void clear_index(Uint8 c)
{
    STDL_SDL2_SetRenderDrawIndex(R, c);
    CHECK(SDL_RenderClear(R) == 0);
    model_fill(c);
    drawn = 1;
}

/* the screen on show cleared there and then: all zeroes, X's colour */
static void clear_now(void)
{
    const Uint8 *base;
    long bytes, i, nonzero = 0;
    CHECK(STDL_SDL2_ClearScreenNow(R) == 0);
    model_fill((Uint8)X);
    drawn = 0;
    STDL_SDL2_GetShownPage(R, &base, &bytes);
    /* on an ST X's colour as the map has it: zeroes only when X is
     * set */
    if (is_st) {
        if (base) {
            check_shown();
        }
        return;
    }
    for (i = 0; base && i < bytes; i++) {
        nonzero += base[i] != 0;
    }
    CHECK(nonzero == 0);
}

/* a fade's step: the palette staged with no present. The ST build
 * presents all the same when the map has moved. */
static void stage_now(void)
{
    const int moved = map_moved();
#ifndef __m68k__
    const int flips = fake_flips;
#endif
    CHECK(STDL_SDL2_StagePalette(R) == 0);
#ifndef __m68k__
    CHECK(fake_flips == flips + moved);
#endif
    if (moved) {
        const Uint8 *base;
        long bytes;
        STDL_SDL2_GetShownPage(R, &base, &bytes);
        check_shown();
        last_base = base;
        drawn = 0;
    }
    check_registers();
}

static void random_colours(int n)
{
    SDL_Color c[256];
    const int first = rndn(256 - n);
    int i;
    for (i = 0; i < n; i++) {
        c[i].r = (Uint8)rnd();
        c[i].g = (Uint8)rnd();
        c[i].b = (Uint8)rnd();
        c[i].a = 255;
    }
    SDL_SetPaletteColors(pal, c, first, n);
}

/* ------------------------------------------------------------------ */
/* textures                                                            */

static void fill_random(Uint8 *p, long n)
{
    long i;
    for (i = 0; i < n; i++) {
        p[i] = (Uint8)rnd();
    }
}

static Tex *own(int w, int h)
{
    Tex *t = &tex[ntex++];
    void *px;
    int pitch;
    t->t = SDL_CreateTexture(R, SDL_PIXELFORMAT_INDEX8,
                             SDL_TEXTUREACCESS_STREAMING, w, h);
    CHECK(t->t != NULL);
    t->w = w;
    t->h = h;
    CHECK(SDL_LockTexture(t->t, NULL, &px, &pitch) == 0);
    t->px = (Uint8 *)px;
    t->pitch = pitch;
    SDL_UnlockTexture(t->t);
    fill_random(t->px, (long)pitch * h);
    t->borrowed = 0;
    return t;
}

static Tex *borrowed(int w, int h, int pitch, int off)
{
    Tex *t = &tex[ntex++];
    Uint8 *buf = (Uint8 *)malloc((size_t)pitch * h + off);
    t->px = buf + off;
    t->w = w;
    t->h = h;
    t->pitch = pitch;
    t->borrowed = 1;
    fill_random(t->px, (long)pitch * h);
    t->t = STDL_SDL2_CreateTextureFrom(R, t->px, w, h, pitch);
    CHECK(t->t != NULL);
    return t;
}

static void update(Tex *t)
{
    SDL_Rect r;
    Uint8 buf[64 * 64];
    int y;
    r.w = 1 + rndn(t->w < 64 ? t->w : 64);
    r.h = 1 + rndn(t->h < 64 ? t->h : 64);
    r.x = rndn(t->w - r.w + 1);
    r.y = rndn(t->h - r.h + 1);
    fill_random(buf, (long)r.w * r.h);
    CHECK(SDL_UpdateTexture(t->t, &r, buf, r.w) == 0);
    for (y = 0; y < r.h; y++) {
        memcpy(t->px + (r.y + y) * t->pitch + r.x, buf + y * r.w,
               (size_t)r.w);
    }
}

static void lock_and_write(Tex *t)
{
    SDL_Rect r;
    void *px;
    int pitch, y;
    r.w = 1 + rndn(t->w);
    r.h = 1 + rndn(t->h < 40 ? t->h : 40);
    r.x = rndn(t->w - r.w + 1);
    r.y = rndn(t->h - r.h + 1);
    CHECK(SDL_LockTexture(t->t, &r, &px, &pitch) == 0);
    for (y = 0; y < r.h; y++) {
        fill_random((Uint8 *)px + y * pitch, r.w);
    }
    SDL_UnlockTexture(t->t);
}

static void random_rect(SDL_Rect *r, int maxw, int maxh)
{
    switch (rndn(4)) {
    case 0:                     /* anywhere, any size */
        r->w = 1 + rndn(maxw);
        r->h = 1 + rndn(maxh);
        r->x = rndn(maxw + 40) - 20;
        r->y = rndn(maxh + 40) - 20;
        break;
    case 1:                     /* a pixel or a sliver */
        r->w = 1 + rndn(3);
        r->h = 1 + rndn(60);
        r->x = rndn(maxw);
        r->y = rndn(maxh);
        break;
    case 2:                     /* group-aligned, as one port pushes */
        r->x = 16 * rndn(maxw / 16 + 1);
        r->w = 16 * (1 + rndn(5));
        r->y = rndn(maxh);
        r->h = 1 + rndn(40);
        break;
    default:                    /* small and odd */
        r->w = 1 + rndn(40);
        r->h = 1 + rndn(30);
        r->x = rndn(maxw) - 5;
        r->y = rndn(maxh) - 5;
        break;
    }
}

/* ------------------------------------------------------------------ */
/* the run                                                             */

static void port_sequences(void)
{
    Tex *layer, *title;
    SDL_Rect box;
    int i, y;

    /* the play's picture, 256x224, in a buffer of the port's own */
    CHECK(SDL_RenderSetLogicalSize(R, 256, 224) == 0);
    model_place(256, 224);
    layer = borrowed(256, 224, 256, 0);
    copy(layer, NULL, NULL);
    present();
    for (i = 0; i < 40; i++) {
        int k, n = 1 + rndn(6);
        for (k = 0; k < n; k++) {
            /* a box drawn on, then pushed rounded out to its groups */
            int x0 = rndn(256), w = 1 + rndn(60);
            box.y = rndn(224);
            box.h = 1 + rndn(50);
            if (box.y + box.h > 224) {
                box.h = 224 - box.y;
            }
            if (x0 + w > 256) {
                w = 256 - x0;
            }
            for (y = box.y; y < box.y + box.h; y++) {
                fill_random(layer->px + y * 256 + x0, w);
            }
            box.x = x0 & ~15;
            box.w = ((x0 + w + 15) & ~15) - box.x;
            copy(layer, &box, &box);
        }
        present();
        step++;
    }

    /* boxes pushed again as they were, with a pixel changed, and after
     * another buffer drew over them: the ST build converts only the
     * groups that differ from what the screen has */
    {
        Tex *other = borrowed(256, 224, 256, 0);
        for (i = 0; i < 30; i++) {
            box.x = 16 * rndn(8);
            box.w = 16 * (1 + rndn(8));
            box.y = rndn(120);
            box.h = 1 + rndn(100);
            switch (rndn(4)) {
            case 0:             /* one pixel of it */
                layer->px[(box.y + rndn(box.h)) * 256 + box.x + rndn(box.w)]++;
                break;
            case 1:             /* a row of it */
                fill_random(layer->px + (box.y + rndn(box.h)) * 256 + box.x,
                            box.w);
                break;
            case 2:             /* the other buffer over a part first */
                {
                    SDL_Rect o = box;
                    o.w = 16;
                    o.h = 1 + rndn(box.h);
                    copy(other, &o, &o);
                }
                break;
            default:            /* nothing: as it was */
                break;
            }
            copy(layer, &box, &box);
            present();
            step++;
        }
    }

    /* two buffers in one frame, the second over the first */
    title = borrowed(256, 224, 256, 0);
    for (i = 0; i < 10; i++) {
        box.x = 16 * rndn(12);
        box.w = 16 * (1 + rndn(4));
        box.y = rndn(150);
        box.h = 1 + rndn(60);
        copy(layer, &box, &box);
        box.x += 16 * rndn(3);
        copy(title, &box, &box);
        present();
        step++;
    }

    /* pushed black, then the picture revealed two rows a step */
    CHECK(SDL_RenderSetLogicalSize(R, 320, 224) == 0);
    model_place(320, 224);
    {
        Tex *wipe = borrowed(320, 224, 320, 0);
        memset(wipe->px, 0, 320 * 224);
        copy(wipe, NULL, NULL);
        present();
        fill_random(wipe->px, 320 * 224);
        for (y = 112; y > 0; y -= 2) {
            box.x = 0;
            box.w = 320;
            box.y = y - 2;
            box.h = 2;
            copy(wipe, &box, &box);
            box.y = 224 - y;
            copy(wipe, &box, &box);
            present();
            step++;
        }
    }

    /* the screen cleared at once, and a present with nothing drawn */
    clear_now();
    present();
    box.x = 16;
    box.y = 8;
    box.w = 64;
    box.h = 20;
    copy(&tex[0], &box, &box);
    present();

    /* a fade: colours staged, no present */
    for (i = 7; i >= 0; i--) {
        random_colours(256);
        stage_now();
    }

    /* a copy clipped to nothing draws nothing, and does not flip */
    box.x = 400;
    box.y = 10;
    box.w = 32;
    box.h = 32;
    copy(layer, &box, &box);
    present();
    CHECK(SDL_RenderSetLogicalSize(R, 0, 0) == 0);
    model_place(320, 240);
}

static void random_run(long steps)
{
    long n;
    for (n = 0; n < steps; n++, step++) {
        const int what = rndn(100);
        Tex *t = &tex[rndn(ntex)];
        SDL_Rect s, d;
        if (what < 40) {
            random_rect(&s, t->w, t->h);
            d = s;
            switch (rndn(5)) {
            case 0:
                copy(t, NULL, NULL);
                break;
            case 1:
                d.x = rndn(lw + 20) - 10;
                d.y = rndn(lh + 20) - 10;
                copy(t, &s, &d);
                break;
            case 2:             /* scaled: refused */
                d.w = s.w + 1;
                copy(t, &s, &d);
                break;
            default:
                d.x = rndn(lw) - 3;
                d.y = rndn(lh) - 3;
                copy(t, &s, &d);
                break;
            }
        } else if (what < 50) {
            if (!t->borrowed) {
                update(t);
            }
        } else if (what < 55) {
            lock_and_write(t);
        } else if (what < 72) {
            present();
            /* a program's own pixels change between frames */
            if (rndn(3) == 0) {
                int i;
                for (i = 0; i < ntex; i++) {
                    if (tex[i].borrowed) {
                        fill_random(tex[i].px + tex[i].pitch * rndn(tex[i].h),
                                    tex[i].w);
                    }
                }
            }
        } else if (what < 77) {
            if (rndn(2)) {
                clear_index((Uint8)rnd());
            } else {
                /* the nearest colour, the lowest of equals */
                SDL_Color want_c = pal->colors[rndn(pal->ncolors)];
                int i, bi = 0;
                long best = -1;
                SDL_SetRenderDrawColor(R, want_c.r, want_c.g, want_c.b, 255);
                for (i = 0; i < pal->ncolors; i++) {
                    const long dr = (long)pal->colors[i].r - want_c.r;
                    const long dg = (long)pal->colors[i].g - want_c.g;
                    const long db = (long)pal->colors[i].b - want_c.b;
                    const long dd = dr * dr + dg * dg + db * db;
                    if (best < 0 || dd < best) {
                        best = dd;
                        bi = i;
                    }
                }
                CHECK(SDL_RenderClear(R) == 0);
                model_fill((Uint8)bi);
                drawn = 1;
            }
        } else if (what < 82) {
            random_colours(1 + rndn(40));
        } else if (what < 84) {
            stage_now();
        } else if (what < 86) {
            clear_now();
        } else if (what < 89) {
            int w = 16 + rndn(305), h = 1 + rndn(240);
            if (rndn(4) == 0) {
                CHECK(SDL_RenderSetLogicalSize(R, 0, 0) == 0);
                model_place(320, 240);
            } else {
                CHECK(SDL_RenderSetLogicalSize(R, w, h) == 0);
                model_place(w, h);
            }
        } else {
            present();
        }
    }
    present();
}

static void make_textures(void)
{
    ntex = 0;
    own(320, 240);
    own(37, 29);
    borrowed(250, 200, 263, 3);   /* odd pitch, odd address */
    borrowed(320, 240, 320, 0);
}

/* one screen: returns the failures */
static int run(unsigned long s, unsigned x, int xconst)
{
    seed = s;
    step = 0;
    failures = 0;
    SDL_SetHint(STDL_SDL2_HINT_CHECK, "1");
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
#ifdef STDL_SDL2_ST
    is_st = STDL_SDL2_NO_256();
#endif
    win = SDL_CreateWindow("test", SDL_WINDOWPOS_CENTERED,
                           SDL_WINDOWPOS_CENTERED, 320, 240, 0);
    CHECK(win != NULL);
    CHECK(SDL_CreateWindow("again", 0, 0, 320, 240, 0) == NULL);
    R = SDL_CreateRenderer(win, -1, 0);
    CHECK(R != NULL);
    if (!R) {
        return failures;
    }
    /* the border index: a constant links its own loops, a variable the
     * run-time XOR's */
    if (xconst && x == 0x70) {
        CHECK(STDL_SDL2_SetBorderIndex(R, 0x70) == 0);
    } else {
        volatile unsigned vx = x;
        CHECK(STDL_SDL2_SetBorderIndex(R, vx) == 0);
    }
    X = x;
    CHECK(STDL_SDL2_SetBorderIndex(R, 0x07) == -1);
    pal = SDL_AllocPalette(256);
    random_colours(256);
    CHECK(STDL_SDL2_SetRenderPalette(R, pal) == 0);
    model_fill((Uint8)X);
    model_place(320, 240);
    {
        const Uint8 *base;
        long bytes;
        STDL_SDL2_GetShownPage(R, &base, &bytes);
        CHECK(base == NULL);
        last_base = NULL;
    }
    make_textures();
    CHECK(SDL_RenderSetLogicalSize(R, 321, 200) == -1);
    copy(&tex[0], NULL, NULL);
    CHECK(STDL_SDL2_SetBorderIndex(R, 0x10) == -1);   /* drawn on now */
    present();
    port_sequences();
    random_run(1500);
    {
        STDL_SDL2_Check chk;
        CHECK(STDL_SDL2_GetCheck(R, &chk) == 0);
        CHECK(chk.bad == 0);
        CHECK(chk.flips > 100);
    }
    return failures;
}

#ifdef STDL_SDL2_ST
/* what the reduction promises (st.h) */
static int reduce_checks(void)
{
    SDL_Color c[256], few[16], out[16];
    Uint8 map[256];
    int i, k, ch, used[16];
    failures = 0;
    seed = 0x5EED;
    /* 15 colours and black, anywhere among the 256: each shown
     * exactly, whichever one channel tells them apart */
    for (ch = 0; ch < 4; ch++) {
        memset(few, 0, sizeof(few));
        for (k = 1; k < 16; k++) {
            const Uint8 apart = (Uint8)(k * 16), same = 200;
            few[k].r = ch == 0 ? apart : ch == 3 ? (Uint8)(1 + rndn(255))
                                                 : same;
            few[k].g = ch == 1 ? apart : ch == 3 ? (Uint8)rnd() : same;
            few[k].b = ch == 2 || ch == 3 ? apart : same;
        }
        for (i = 0; i < 256; i++) {
            c[i] = few[rndn(16)];
        }
        stdl_sdl2_st_reduce(c, 256, 0, map, out);
        for (i = 0; i < 256; i++) {
            const SDL_Color o = out[map[i]];
            CHECK(map[i] < 16);
            CHECK(o.r == c[i].r && o.g == c[i].g && o.b == c[i].b);
            CHECK((map[i] == 0) == !(c[i].r | c[i].g | c[i].b));
        }
    }
    /* any 256: slot 0 black and only black's and X's, every other slot
     * used and the mean of its colours; a short palette's rest slot 0 */
    for (k = 0; k < 20; k++) {
        const unsigned x = (k & 1) ? 0x70 : 0;
        const int n = k < 10 ? 256 : 1 + rndn(256);
        unsigned sum[16][3], cnt[16];
        for (i = 0; i < 256; i++) {
            c[i].r = (Uint8)rnd();
            c[i].g = (Uint8)rnd();
            c[i].b = (Uint8)rnd();
        }
        c[5].r = c[5].g = c[5].b = 0;
        stdl_sdl2_st_reduce(c, n, x, map, out);
        memset(used, 0, sizeof(used));
        memset(sum, 0, sizeof(sum));
        memset(cnt, 0, sizeof(cnt));
        CHECK(!(out[0].r | out[0].g | out[0].b));
        for (i = 0; i < 256; i++) {
            const int black = i >= n || !(c[i].r | c[i].g | c[i].b) ||
                              (x && (unsigned)i == x);
            CHECK(map[i] < 16);
            CHECK((map[i] == 0) == black);
            used[map[i]] = 1;
            sum[map[i]][0] += c[i].r;
            sum[map[i]][1] += c[i].g;
            sum[map[i]][2] += c[i].b;
            cnt[map[i]]++;
        }
        for (i = 1; i < 16; i++) {
            if (n >= 64) {
                CHECK(used[i]);
            }
            if (cnt[i]) {
                CHECK(out[i].r == (sum[i][0] + cnt[i] / 2) / cnt[i]);
                CHECK(out[i].g == (sum[i][1] + cnt[i] / 2) / cnt[i]);
                CHECK(out[i].b == (sum[i][2] + cnt[i] / 2) / cnt[i]);
            }
        }
    }
    /* a palette change: the map stays for the same colours, for a
     * fade's steps, at black and on the way back, its colours the kept
     * map's means; it moves for other colours */
    {
        SDL_Color f[256], want_c[16];
        Uint8 kept[256];
        int moved = 0;
        for (i = 0; i < 256; i++) {
            c[i].r = (Uint8)i;
            c[i].g = (Uint8)(i * 7);
            c[i].b = (Uint8)(255 - i);
        }
        memset(map, 0, sizeof(map));
        CHECK(stdl_sdl2_st_update(c, 256, 0, map, out) == 1);
        CHECK(stdl_sdl2_st_update(c, 256, 0, map, out) == 0);
        memcpy(kept, map, sizeof(kept));
        for (k = 14; k >= 4; k--) {
            for (i = 0; i < 256; i++) {
                f[i].r = (Uint8)(c[i].r * k / 15);
                f[i].g = (Uint8)(c[i].g * k / 15);
                f[i].b = (Uint8)(c[i].b * k / 15);
            }
            moved += stdl_sdl2_st_update(f, 256, 0, map, out);
        }
        CHECK(moved == 0);
        CHECK(memcmp(kept, map, sizeof(kept)) == 0);
        stdl_sdl2_st_fit(f, 256, 0, kept, want_c);
        CHECK(memcmp(want_c, out, sizeof(want_c)) == 0);
        memset(f, 0, sizeof(f));
        CHECK(stdl_sdl2_st_update(f, 256, 0, map, out) == 0);
        CHECK(stdl_sdl2_st_update(c, 256, 0, map, out) == 0);
        CHECK(memcmp(kept, map, sizeof(kept)) == 0);
        for (i = 0; i < 256; i++) {
            f[i].r = (Uint8)rnd();
            f[i].g = (Uint8)rnd();
            f[i].b = (Uint8)rnd();
        }
        CHECK(stdl_sdl2_st_update(f, 256, 0, map, out) == 1);
    }
    if (failures) {
        printf("test_render_st: the reduction failed\n");
    }
    return failures;
}
#endif

#if !defined(__m68k__) && defined(STDL_SDL2_ST)
static int main256(void);

/* an ST, which the ST build's SDL_Init takes */
int main(void)
{
    /* the tall screen's 240 rows; 200 where the border cannot open,
     * which is ST Low's page and may have three; a border cut short */
    static const unsigned xs[] = { 0, 0x70 };
    static const int rows[] = { 240, 200, 216, 0 };
    int ri, pages, xi, total = reduce_checks(), runs = 0;
    for (ri = 0; ri < 4; ri++) {
        for (pages = 2; pages <= 3; pages++) {
            for (xi = 0; xi < 2; xi++) {
                pid_t pid = fork();
                int status = 0;
                if (pid == 0) {
                    /* the last: the tall screen there, and the hint
                     * against it */
                    const int want_rows = rows[ri] ? rows[ri] : 200;
                    const int got = want_rows > 200 ? 2 : pages;
                    int f;
                    fake_third = 1;
                    fake_st_rows = rows[ri] ? rows[ri] : 240;
                    if (!rows[ri]) {
                        SDL_SetHint(STDL_SDL2_HINT_ST_TALL, "0");
                    }
                    fake_mach.mch_cookie = 0;
                    SDL_SetHint(STDL_SDL2_HINT_PAGES,
                                pages == 2 ? "2" : "3");
                    f = run(0xC0FFEEUL + (unsigned long)pages, xs[xi],
                            xi == 1);
                    if (fake_npages != got || fake_stride != 160 ||
                        fake_tall != (rows[ri] != 0) ||
                        page_rows != want_rows) {
                        printf("FAIL %d rows, %d pages asked for\n",
                               rows[ri], pages);
                        f++;
                    }
                    _exit(f ? 1 : 0);
                }
                waitpid(pid, &status, 0);
                runs++;
                if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                    printf("test_render_st: %d rows, %d pages, X 0x%X"
                           " failed\n", rows[ri], pages, xs[xi]);
                    total++;
                }
            }
        }
    }
    if (total) {
        printf("test_render_st: failed\n");
        return 1;
    }
    printf("test_render_st: ok (%d ST screens)\n", runs);
    /* then a TT's and a Falcon's, which this build must show as the
     * other does */
    return main256();
}
#endif

#if !defined(__m68k__)
static int child(int layout, int pages, unsigned x, int xconst)
{
    int f;
    fake_layout = layout;
    fake_third = 1;
    fake_mach.mch_cookie = layout == FAKE_TT ? 0x00020000UL : 0x00030000UL;
    SDL_SetHint(STDL_SDL2_HINT_PAGES, pages == 2 ? "2" : "3");
    f = run(0xC0FFEEUL + (unsigned long)layout * 31 + (unsigned long)pages,
            x, xconst);
    if (fake_npages != pages) {
        printf("FAIL %d pages asked for, %d given\n", pages, fake_npages);
        f++;
    }
    return f;
}

#ifdef STDL_SDL2_ST
static int main256(void)
#else
int main(void)
#endif
{
    static const char *names[] = { "TT", "Falcon VGA", "Falcon RGB" };
    static const unsigned xs[] = { 0, 0x70, 0x30 };
    int layout, pages, xi, total = 0, runs = 0;
    for (layout = 0; layout < 3; layout++) {
        for (pages = 2; pages <= 3; pages++) {
            for (xi = 0; xi < 3; xi++) {
                pid_t pid = fork();
                int status = 0;
                if (pid == 0) {
                    _exit(child(layout, pages, xs[xi], xi == 1) ? 1 : 0);
                }
                waitpid(pid, &status, 0);
                runs++;
                if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                    printf("test_render: %s, %d pages, X 0x%X failed\n",
                           names[layout], pages, xs[xi]);
                    total++;
                }
            }
        }
    }
    if (total) {
        printf("test_render: %d of %d screens failed\n", total, runs);
        return 1;
    }
    printf("test_render: ok (%d screens)\n", runs);
    return 0;
}
#else
#ifndef STDL_SDL2_TEST_PAGES
#define STDL_SDL2_TEST_PAGES "3"
#endif
#include "../../../examples/hold.h"

int main(void)
{
    int f;

    hold_on_exit("SDL2RCHK");             /* SDL2RCHK: the results stay on screen */
    /* the machine's own screen, its layout read back from the page */
    SDL_SetHint(STDL_SDL2_HINT_PAGES, STDL_SDL2_TEST_PAGES);
#ifdef STDL_SDL2_ST
    f = reduce_checks();
    f += run(0xC0FFEEUL, 0x70, 1);
#else
    f = run(0xC0FFEEUL, 0x70, 1);
#endif
    printf("S2R: %s pages, %d failures\n", STDL_SDL2_TEST_PAGES, f);
    printf("S2R:done\n");
    SDL_Quit();
    return f ? 1 : 0;
}
#endif
