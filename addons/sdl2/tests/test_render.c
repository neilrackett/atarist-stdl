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
 */

#include "SDL.h"
#include "SDL_stdl.h"

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
    stride_ = bytes / 240;
    dbl_ = stride_ == 640;
    margin_ = ((dbl_ ? stride_ / 2 : stride_) - 320) / 2;
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

/* registers: colour i ^ X, register 0 black when X is set */
static void check_registers(void)
{
    const SDL_Color *regs = STDL_SDL2_GetRegisterColours(R);
    int i, bad = 0;
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
    for (i = 0; base && i < bytes; i++) {
        nonzero += base[i] != 0;
    }
    CHECK(nonzero == 0);
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
        CHECK(STDL_SDL2_StagePalette(R) == 0);
        check_registers();
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
            CHECK(STDL_SDL2_StagePalette(R) == 0);
            check_registers();
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

#ifndef __m68k__
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

int main(void)
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

int main(void)
{
    int f;
    /* the machine's own screen, its layout read back from the page */
    SDL_SetHint(STDL_SDL2_HINT_PAGES, STDL_SDL2_TEST_PAGES);
    f = run(0xC0FFEEUL, 0x70, 1);
    printf("S2R: %s pages, %d failures\n", STDL_SDL2_TEST_PAGES, f);
    printf("S2R:done\n");
    SDL_Quit();
    return f ? 1 : 0;
}
#endif
