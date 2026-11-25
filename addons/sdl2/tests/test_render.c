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
 * for byte, a pixel at a time, and the registers the
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
 * as the screen is set up once a program, and again with copies
 * compared (STDL_SDL2_HINT_COMPARE), once of those without the
 * add-on's own check, which then has the chunky copy and no page of
 * its own, and with the compare turned on and off as the run goes
 * (STDL_SDL2_SetCompare), from none at the start and with the check
 * off but where X is 0. On the machine, the one screen the machine
 * has, against the real STDL, the screen check on. It prints
 * S2R:done; SDL2RCH2 is the same with two pages, SDL2RCHC with copies
 * compared (-DSTDL_SDL2_TEST_COMPARE) and SDL2RCHT with the compare
 * toggled and the check off (-DSTDL_SDL2_TEST_TOGGLE):
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
 * through the renderer's two maps of the palette onto 16 colours, the
 * first where the screen's x + y is even, a pixel at a time, and a
 * present flips when the maps have moved though nothing was drawn. The
 * test notes the colours its model shows and expects the renderer's
 * colours seen, maps and registers from them (st_eval), and the
 * reduction is checked for what it promises before the runs, and the
 * ST's own pair loop (pair_checks): pixel pairs on an ST's screen show
 * the second of each pair where they are not pairs. On an ST:
 *
 *   STCMD_NO_TTY=1 stcmd make sdl2st
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 -m68000 \
 *       -DSTDL_SDL2_ST -Iinclude -Iaddons/sdl2/include/SDL2 \
 *       -Iaddons/sdl2/src -c -o rchk.o addons/sdl2/tests/test_render.c
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -m68000 \
 *       -o dist/SDL2RCST.TOS rchk.o libstdl-sdl2-st.a libstdl.a
 *
 * With -DSTDL_SDL2_TEST_WIDE as well it is SDL2RCWD, a window of 640x480:
 * on a TT or a Falcon the wide screen (STDL_RESPONSIVE_WIDE), 40 groups
 * a row, through the same 16-colour path; on the host test_render_st
 * runs it on a TT and a Falcon, 480 rows and 400.
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

/* the screen's size: 320x240, or 640x480 for the ST build's wide screen
 * (wide_run), which a window wider than 320 gets on a TT or a Falcon */
static int SW = 320, SH = 240;
#ifdef STDL_SDL2_ST
static int wide_run;
#endif
/* SW by SH, allocated when the run starts: a 640x480 model would leave
 * an ST running the 320-pixel test no room for the renderer */
static Uint8 *model;
#define MODEL(y, x) model[(long)(y) * SW + (x)]
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

static Tex tex[12];
static int ntex;

#ifdef STDL_SDL2_ST
/* On an ST, the colours the page has shown since the renderer last
 * counted them, as the renderer should have them (video.c's seen),
 * noted as the model is drawn; whether one came that was not there;
 * whether the palette changed; and the rows the page has. The rules
 * for when the renderer counts and makes its maps are st_eval's. */
static Uint8 tseen[256];
static int tfresh, tpal;
/* an ST's screen, and the rows of the screen its page has: set when
 * the window is made, from the screen STDL set up */
static int is_st, page_rows = 240;
#define PROW0 ((SH - page_rows) / 2)
/* the program's own map and colours (STDL_SDL2_SetSTColours): while
 * they are given nothing is noted or counted, and a new map's second
 * slots are made at the next evaluation; given back, the renderer
 * counts at the next one */
static int tgiven, tgnew;
static Uint8 tgmap[256];
static SDL_Color tgcol[16];

static void note(int y, Uint8 v)
{
    if (is_st && !tgiven && y >= PROW0 && y < PROW0 + page_rows &&
        !tseen[v]) {
        tseen[v] = 1;
        tfresh = 1;
    }
}
#define NOTE(y, v) note(y, v)
#else
#define NOTE(y, v) ((void)0)
#endif

static void model_place(int w, int h)
{
    lw = w;
    lh = h;
    xoff = ((SW - w) / 2) & ~15;
    yoff = (SH - h) / 2;
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
            MODEL(ly + yoff, lx + xoff) = t->px[ty * t->pitch + tx];
            NOTE(ly + yoff, MODEL(ly + yoff, lx + xoff));
            any = 1;
        }
    }
    return any;
}

static void model_fill(Uint8 c)
{
    memset(model, c, (size_t)SW * SH);
    NOTE(PROW0, c);
}

/* ------------------------------------------------------------------ */
/* what the screen should hold                                         */

static Uint8 *want;
static long want_bytes;

#ifdef STDL_SDL2_ST
/* the ST build: an ST's 16-colour page, or on a TT or a Falcon the
 * 256-colour one, as the other build has it */
#define PAGE_ROWS page_rows

/* colour c at x of a row of an ST's four planes, 8 bytes a group,
 * through maps a and b: the first slot where the screen's x + y (xy)
 * is even, the second where it is odd; ORed into a cleared row */
static void st_pixel(Uint8 *row, long x, long xy, const Uint8 *a,
                     const Uint8 *b, Uint8 c)
{
    const unsigned v = (xy & 1) ? b[c] : a[c];
    Uint8 *grp = row + (x >> 4) * 8;
    int p;
    for (p = 0; p < 4; p++) {
        if (v >> p & 1) {
            grp[2 * p + ((x & 15) >> 3)] |= (Uint8)(0x80 >> (x & 7));
        }
    }
}

/* the page's rows, the screen's middle ones, through the renderer's
 * map, four planes */
static void expect_page_st(long stride)
{
    int bits, dither;
    const Uint8 *rseen;
    const struct stdl_sdl2_st_maps *m = stdl_sdl2_st_view(R, &bits, &dither,
                                                          &rseen);
    const int row0 = PROW0;
    int x, y;
    memset(want, 0, (size_t)want_bytes);
    for (y = 0; y < page_rows; y++) {
        for (x = 0; x < SW; x++) {
            st_pixel(want + y * stride, x, x + y + row0, m->a, m->b,
                     MODEL(y + row0, x));
        }
    }
}
#else
#define is_st 0
#define PAGE_ROWS 240
#endif

static void expect_page(long stride, int dbl)
{
    const long dp = dbl ? stride / 2 : stride;
    int x, y, p;
    memset(want, 0, (size_t)want_bytes);
    for (y = 0; y < 240; y++) {
        for (x = 0; x < 320; x++) {
            const unsigned v = MODEL(y, x) ^ X;
            Uint8 *grp = want + y * stride + (x >> 4) * 16;
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

static long stride_;
static int dbl_;
static const Uint8 *last_base;

/* the layout from the page's size: a TT's 640 bytes a row, a Falcon's
 * 320 */
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
    CHECK(!is_st || bytes == (long)(SW / 2) * page_rows);
#endif
    stride_ = bytes / PAGE_ROWS;
    dbl_ = stride_ == 640;
#ifdef STDL_SDL2_ST
    if (is_st) {
        expect_page_st(stride_);
    } else
#endif
    expect_page(stride_, dbl_);
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
/* the maps the renderer should have, and its 16 colours */
static struct stdl_sdl2_st_maps want_maps;

/*
 * What the renderer does at a present after a palette change or a
 * colour seen that was not, and at every staging: the maps kept if
 * they fit the palette and the colours seen, else made again, from the
 * colours on the page counted afresh if the palette changed. Says
 * whether a map moved, for which the renderer flips.
 */
static int st_eval(void)
{
    int bits, dither, moved = 0, x, y;
    const Uint8 *rseen;
    SDL_Color regs[16];
    want_maps = *stdl_sdl2_st_view(R, &bits, &dither, &rseen);
    CHECK(memcmp(rseen, tseen, sizeof(tseen)) == 0);
    if (tgiven) {
        if (tgnew) {
            Uint8 b[256];
            stdl_sdl2_st_seconds(pal->colors, pal->ncolors, X, tgcol, tgmap,
                                 b, dither);
            moved = stdl_sdl2_st_use(&want_maps, tgmap, b);
            tgnew = 0;
        }
    } else if (!stdl_sdl2_st_keep(pal->colors, pal->ncolors, X, bits, tseen,
                                  &want_maps, regs)) {
        if (tpal) {
            memset(tseen, 0, sizeof(tseen));
            for (y = PROW0; y < PROW0 + page_rows; y++) {
                for (x = 0; x < SW; x++) {
                    tseen[MODEL(y, x)] = 1;
                }
            }
        }
        moved = stdl_sdl2_st_remake(pal->colors, pal->ncolors, X, bits, dither,
                                    tseen, &want_maps, regs);
    }
    tpal = tfresh = 0;
    return moved;
}

/* the maps the renderer has are what st_eval made, it has seen what the
 * model has shown, and its registers are the maps' colours over the
 * palette and the colours seen */
static void check_registers_st(void)
{
    const SDL_Color *regs = STDL_SDL2_GetRegisterColours(R);
    int bits, dither, i, bad = 0;
    const Uint8 *rseen;
    const struct stdl_sdl2_st_maps *m = stdl_sdl2_st_view(R, &bits, &dither,
                                                          &rseen);
    SDL_Color want[16];
    CHECK(memcmp(m->a, want_maps.a, 256) == 0);
    CHECK(memcmp(m->b, want_maps.b, 256) == 0);
    CHECK(m->made == want_maps.made && m->made_valid == want_maps.made_valid);
    CHECK(memcmp(rseen, tseen, sizeof(tseen)) == 0);
    if (tgiven) {
        memcpy(want, tgcol, sizeof(want));
    } else {
        stdl_sdl2_st_fit(pal->colors, pal->ncolors, X, bits, tseen, m->a,
                         m->b, want);
    }
    for (i = 0; i < 16; i++) {
        if (regs[i].r != want[i].r || regs[i].g != want[i].g ||
            regs[i].b != want[i].b) {
            bad++;
        }
#ifndef __m68k__
        if (fake_regs[i].r != want[i].r || fake_regs[i].g != want[i].g ||
            fake_regs[i].b != want[i].b) {
            bad++;
        }
#endif
    }
    CHECK(bad == 0);
}

/* The program's map and colours given, the same map again with new
 * colours, or (give 0) given back, all evaluated at the next present
 * or staging: a map unlike the maps in use is made into them, colours
 * alone go to the registers. */
static void st_give(int give, int same)
{
    int bits, dither, i;
    const Uint8 *rseen;
    Uint8 map[256];
    SDL_Color col[16];
    if (!give) {
        CHECK(STDL_SDL2_SetSTColours(R, NULL, NULL) == 0);
        if (tgiven) {
            /* the maps made again, from a count */
            CHECK(!stdl_sdl2_st_view(R, &bits, &dither, &rseen)->made_valid);
            tgiven = tgnew = 0;
            tpal = 1;
        }
        return;
    }
    for (i = 0; i < 256; i++) {
        map[i] = (same && tgiven) ? tgmap[i] : (Uint8)rnd();
        tgmap[i] = (Uint8)(map[i] & 15);
    }
    for (i = 0; i < 16; i++) {
        col[i].r = (Uint8)rnd();
        col[i].g = (Uint8)rnd();
        col[i].b = (Uint8)rnd();
        col[i].a = 7;
        tgcol[i] = col[i];
        tgcol[i].a = 255;
    }
    CHECK(STDL_SDL2_SetSTColours(R, map, col) == 0);
    tgnew |= !tgiven ||
             memcmp(tgmap, stdl_sdl2_st_view(R, &bits, &dither, &rseen)->a,
                    sizeof(tgmap)) != 0;
    tgiven = tpal = 1;
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
#ifdef STDL_SDL2_ST
    /* the ST build flips for a new map alone */
    if (is_st && (tpal || tfresh)) {
        drawn |= st_eval();
    }
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

/* Pixel pairs: the copy's pixels made pairs on the screen (x and
 * x + 1 the same, x even; the logical size's x is on a group, so its
 * own x's parity is the screen's), then copied with
 * STDL_SDL2_RenderCopyPairs, so the screen is the copy's as any. Made
 * after a present, with the batch empty: the pixels are read when
 * converted. */
static void copy_pairs(Tex *t, const SDL_Rect *s, const SDL_Rect *d)
{
    int m, got, i, j;
    for (j = 0; j < s->h; j++) {
        const int ty = s->y + j;
        if (ty < 0 || ty >= t->h) {
            continue;
        }
        for (i = 1; i < s->w; i++) {
            const int tx = s->x + i;
            if (((d->x + i) & 1) && tx > 0 && tx < t->w) {
                t->px[ty * t->pitch + tx] = t->px[ty * t->pitch + tx - 1];
            }
        }
    }
    m = model_copy(t, s, d);
    got = STDL_SDL2_RenderCopyPairs(R, t->t, s, d);
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
#ifdef STDL_SDL2_ST
    const int moved = is_st ? st_eval() : 0;
#else
    const int moved = 0;
#endif
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
#ifdef STDL_SDL2_ST
    tpal = 1;
#endif
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

/* Textures of few colours, then others. On an ST the maps are made
 * from the colours on the page: counted after the palette change, here
 * the clear's colour alone, then noted as copies bring others, whole
 * groups and parts of groups, and the same again bringing none. */
static void few_colours(void)
{
    Tex *t = borrowed(SW, SH, SW, 0);
    SDL_Rect box;
    int i, k;
    clear_index(0);
    random_colours(256);
    present();
    for (k = 0; k < 8; k++) {
        const int base = rndn(240);
        for (i = 0; i < SW * SH; i++) {
            t->px[i] = (Uint8)(base + rndn(12));
        }
        box.x = 16 * rndn(16);
        box.w = 16 * (1 + rndn(4));
        box.y = rndn(200);
        box.h = 1 + rndn(40);
        copy(t, &box, &box);
        if (k & 1) {
            box.x += 3;
            box.w = 5;
            copy(t, &box, &box);
        }
        present();
        copy(t, &box, &box);
        present();
        if (k == 5) {
            random_colours(12);
            present();
        }
#ifdef STDL_SDL2_ST
        /* the program's colours for a while, new colours coming on
         * screen meanwhile, then the add-on's own again */
        if (is_st && (k == 2 || k == 6)) {
            st_give(k == 2, 0);
        }
#endif
        step++;
    }
}

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
    model_place(SW, SH);
}

/* the compare turned on and off as the run goes (STDL_SDL2_SetCompare) */
static int toggling;

static void random_run(long steps)
{
    long n;
    for (n = 0; n < steps; n++, step++) {
        int what;
        Tex *t;
        SDL_Rect s, d;
        if (toggling && rndn(16) == 0) {
            /* between copies as often as between frames */
            CHECK(STDL_SDL2_SetCompare(R, rndn(3) != 0) == 0);
        }
        what = rndn(100);
        t = &tex[rndn(ntex)];
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
        } else if (what < 47) {
            /* pixel pairs, then more copies over them before the
             * present, as a game's HUD over its low-detail view */
            present();
            random_rect(&s, t->w, t->h);
            d = s;
            d.x = rndn(lw) - 3;
            d.y = rndn(lh) - 3;
            copy_pairs(t, &s, &d);
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
            int w = 16 + rndn(SW - 15), h = 1 + rndn(SH);
            if (rndn(4) == 0) {
                CHECK(SDL_RenderSetLogicalSize(R, 0, 0) == 0);
                model_place(SW, SH);
            } else {
                CHECK(SDL_RenderSetLogicalSize(R, w, h) == 0);
                model_place(w, h);
            }
        } else if (what < 91) {
#ifdef STDL_SDL2_ST
            /* the program's own 16 colours, the same map again with new
             * colours, or the add-on's own again */
            if (is_st) {
                const int k = rndn(4);
                st_give(k != 0, k == 1);
            } else {
                CHECK(STDL_SDL2_SetSTColours(R, NULL, NULL) == -1);
            }
#endif
        } else {
            present();
        }
    }
    present();
}

static void make_textures(void)
{
    ntex = 0;
    own(SW, SH);
    own(37, 29);
    borrowed(250, 200, 263, 3);   /* odd pitch, odd address */
    borrowed(SW, SH, SW, 0);
}

/* flips the add-on's own check is to find unlike its model */
static Uint32 lies;

/*
 * Pairs' rule: a group is converted as pairs only if every copy of it
 * in the batch was a pairs copy. The whole screen in pairs but for one
 * group, whose pixels are not, copied as pairs with that group copied
 * again as any, after and then before, shows the group as it is: a
 * pixel of it (partial) and then the whole group (on an ST's screen a
 * compared copy, which changes nothing after the pairs copy and must
 * still take the group back). Then as pairs alone, it shows what the
 * loops take of it, which the add-on's own check finds unlike: on a TT
 * or a Falcon the C twin's pixels, the first of each pair in the
 * group's first half and the second in its second; on an ST's screen
 * the second of each pair. Last, pairs from a texture at an odd
 * address with an odd pitch, from its second row (at an even address,
 * the next one at an odd): an ST's screen reads a pair as a word, so
 * converts those whole and shows them as they are; the 68030's loops
 * take them as pairs.
 */
static void pairs_rules(void)
{
    Tex *t;
    SDL_Rect all, spot, grp;
    int k, y, x;
    CHECK(SDL_RenderSetLogicalSize(R, 0, 0) == 0);
    model_place(SW, SH);
    spot.x = 33;
    spot.y = 100;                   /* on every page an ST's screen has */
    spot.w = 1;
    spot.h = 6;
    grp = spot;
    grp.x = 32;
    grp.w = 16;
    for (k = 0; k < 6; k++) {
        t = &tex[k < 5 ? 3 : 2];    /* 320x240 borrowed; 250x200 at 3, 263 */
        all.x = 0;
        all.y = k == 5;             /* its rows from an odd one: an even address */
        all.w = t->w;
        all.h = t->h - all.y;
        present();
        for (y = 0; y < t->h; y++) {
            for (x = 0; x + 1 < t->w; x += 2) {
                t->px[y * t->pitch + x + 1] = t->px[y * t->pitch + x];
            }
        }
        for (y = spot.y; y < spot.y + spot.h; y++) {
            Uint8 *g = t->px + y * t->pitch + 32;
            g[1] = (Uint8)(g[0] ^ 0x55);        /* the first half's */
            g[8] = (Uint8)(g[9] ^ 0xAA);        /* the second half's */
        }
        switch (k) {
        case 0:
            CHECK(STDL_SDL2_RenderCopyPairs(R, t->t, &all, &all) == 0);
            CHECK(SDL_RenderCopy(R, t->t, &spot, &spot) == 0);
            break;
        case 1:
            CHECK(SDL_RenderCopy(R, t->t, &spot, &spot) == 0);
            CHECK(STDL_SDL2_RenderCopyPairs(R, t->t, &all, &all) == 0);
            break;
        case 3:
            CHECK(STDL_SDL2_RenderCopyPairs(R, t->t, &all, &all) == 0);
            CHECK(SDL_RenderCopy(R, t->t, &grp, &grp) == 0);
            break;
        case 4:
            CHECK(SDL_RenderCopy(R, t->t, &grp, &grp) == 0);
            CHECK(STDL_SDL2_RenderCopyPairs(R, t->t, &all, &all) == 0);
            break;
        default:
            CHECK(STDL_SDL2_RenderCopyPairs(R, t->t, &all, &all) == 0);
            break;
        }
        model_copy(t, &all, &all);
        drawn = 1;
        /* the loops' pixels where they are not pairs */
        if (k == 2 || (k == 5 && !is_st)) {
            for (y = spot.y; y < spot.y + spot.h; y++) {
                Uint8 *m = &MODEL(y + yoff, 32 + xoff);
                if (is_st) {
                    m[0] = m[1];
                } else {
                    m[1] = m[0];
                }
                m[8] = m[9];
            }
            lies++;
        }
        present();
    }
}

/*
 * The compare turned off and on again: what was copied while it was
 * off is not in the renderer's chunky copy, which still has what was
 * there before. A box copied with the compare on, another over it with
 * it off and the first again with it on, a frame each and then in one
 * frame, must show the first; and after a clear with it off and a box
 * over that, a box of the clear's colour with it on must show that.
 */
static void toggle_rules(void)
{
    Tex *a = &tex[0], *b = &tex[3], *c;
    SDL_Rect box;
    const Uint8 col = 0x5A;
    int y;
    CHECK(SDL_RenderSetLogicalSize(R, 0, 0) == 0);
    model_place(SW, SH);
    box.x = 32;
    box.y = 40;
    box.w = 64;
    box.h = 16;
    present();
    CHECK(STDL_SDL2_SetCompare(R, 1) == 0);
    copy(a, &box, &box);
    present();
    CHECK(STDL_SDL2_SetCompare(R, 0) == 0);
    copy(b, &box, &box);
    present();
    CHECK(STDL_SDL2_SetCompare(R, 1) == 0);
    copy(a, &box, &box);
    present();
    CHECK(STDL_SDL2_SetCompare(R, 0) == 0);
    copy(b, &box, &box);
    CHECK(STDL_SDL2_SetCompare(R, 1) == 0);
    copy(a, &box, &box);
    present();
    c = own(64, 16);
    for (y = 0; y < 16; y++) {
        memset(c->px + y * c->pitch, col, 64);
    }
    CHECK(STDL_SDL2_SetCompare(R, 0) == 0);
    clear_index(col);
    present();
    copy(b, &box, &box);
    present();
    CHECK(STDL_SDL2_SetCompare(R, 1) == 0);
    {
        SDL_Rect s = box;
        s.x = s.y = 0;
        copy(c, &s, &box);
    }
    present();
    ntex--;
    SDL_DestroyTexture(c->t);
}

/*
 * The compare off for one copy of a frame, as a program has it whose
 * view is all new pixels and whose border seldom changes. The copy made
 * with it off takes the groups it touches, in part too, out of the
 * renderer's chunky copy, and a clear takes them all: a box copied with
 * the compare on, a small one over part of it with it off (a pixel at a
 * group's end and at the next one's start, on the box's first row and
 * its last, across two groups, the whole box, over its edge) or a clear
 * of either kind, and the first box again with it on must show the
 * first, in one frame and a frame apart.
 *
 * And every other group is compared as if the compare had never been
 * off. The whole screen copied with it on, a box over it with it off: a
 * box beside that one, above it or below it, copied again as the screen
 * has it, is not converted. To see that, its pixels are changed after
 * the copy and before the present, as a program must not, since a
 * conversion reads them then: the page must show none of it.
 */
static void toggle_one_copy(void)
{
    static const SDL_Rect small[] = {
        { 47, 40, 1, 1 }, { 48, 55, 1, 1 }, { 40, 44, 10, 4 },
        { 32, 40, 64, 16 }, { 90, 47, 20, 3 }
    };
    static const SDL_Rect beside[] = {
        { 0, 40, 32, 16 }, { 96, 40, 224, 16 }, { 32, 24, 64, 16 },
        { 32, 56, 64, 16 }, { 0, 0, 320, 40 }, { 0, 56, 320, 184 }
    };
    Tex *a = &tex[0], *b = &tex[3];
    SDL_Rect box, all;
    int i, k, x, y;
    CHECK(SDL_RenderSetLogicalSize(R, 0, 0) == 0);
    model_place(SW, SH);
    box.x = 32;
    box.y = 40;
    box.w = 64;
    box.h = 16;
    all.x = all.y = 0;
    all.w = (Uint16)SW;
    all.h = (Uint16)SH;
    for (i = 0; i < (int)(sizeof(small) / sizeof(small[0])) + 2; i++) {
        for (k = 0; k < 2; k++) {
            CHECK(STDL_SDL2_SetCompare(R, 1) == 0);
            copy(a, &box, &box);
            present();
            CHECK(STDL_SDL2_SetCompare(R, 0) == 0);
            if (i < (int)(sizeof(small) / sizeof(small[0]))) {
                copy(b, &small[i], &small[i]);
            } else if (i & 1) {
                clear_index(0xA5);
            } else {
                clear_now();
            }
            if (k) {
                present();
            }
            CHECK(STDL_SDL2_SetCompare(R, 1) == 0);
            copy(a, &box, &box);
            present();
        }
    }

    /* a clear with the compare on first: the chunky copy is whole when
     * it is turned off, with nothing said yet of which groups it has */
    CHECK(STDL_SDL2_SetCompare(R, 1) == 0);
    clear_index(0x3C);
    copy(b, &all, &all);
    present();
    CHECK(STDL_SDL2_SetCompare(R, 0) == 0);
    copy(a, &box, &box);
    CHECK(STDL_SDL2_SetCompare(R, 1) == 0);
    present();
    for (i = 0; i < (int)(sizeof(beside) / sizeof(beside[0])); i++) {
        const SDL_Rect *o = &beside[i];
        CHECK(SDL_RenderCopy(R, b->t, o, o) == 0);
        drawn = 1;                  /* a copy flips, whatever it changed */
        for (k = 0; k < 2; k++) {
            for (y = o->y; y < o->y + o->h; y++) {
                for (x = o->x; x < o->x + o->w; x++) {
                    b->px[y * b->pitch + x] ^= 0xFF;
                }
            }
            if (!k) {
                present();
            }
        }
    }
}

/*
 * The compare turned on for the first time, by a renderer made without
 * the hint: the chunky copy allocated then has none of the screen,
 * whatever its memory holds. New memory is often zeroes, so a box of
 * zeroes over a screen of other pixels must show.
 */
static void toggle_first(void)
{
    Tex *z;
    SDL_Rect s, d;
    int y;
    CHECK(SDL_RenderSetLogicalSize(R, 0, 0) == 0);
    model_place(SW, SH);
    copy(&tex[0], NULL, NULL);
    present();
    z = own(64, 16);
    for (y = 0; y < 16; y++) {
        memset(z->px + y * z->pitch, 0, 64);
    }
    s.x = s.y = 0;
    s.w = d.w = 64;
    s.h = d.h = 16;
    d.x = 128;
    d.y = 200;
    CHECK(STDL_SDL2_SetCompare(R, 1) == 0);
    copy(z, &s, &d);
    present();
    ntex--;
    SDL_DestroyTexture(z->t);
}

/* a run without the add-on's own check */
static int unchecked;

/* one screen: returns the failures */
static int run(unsigned long s, unsigned x, int xconst)
{
    seed = s;
    step = 0;
    failures = 0;
    if (!unchecked) {
        SDL_SetHint(STDL_SDL2_HINT_CHECK, "1");
    }
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    {
        /* the largest window: 640x480 on a TT or a Falcon in the ST
         * build, 320x240 otherwise */
        SDL_DisplayMode dm;
#ifdef STDL_SDL2_ST
        const int big = STDL_GetMachineInfo()->mch_cookie >= 0x00020000UL;
#else
        const int big = 0;
#endif
        CHECK(SDL_GetDesktopDisplayMode(0, &dm) == 0 &&
              dm.format == SDL_PIXELFORMAT_INDEX8 &&
              dm.w == (big ? 640 : 320) && dm.h == (big ? 480 : 240));
        CHECK(SDL_GetDesktopDisplayMode(1, &dm) == -1 &&
              SDL_GetDesktopDisplayMode(0, NULL) == -1);
    }
#ifdef STDL_SDL2_ST
    /* the wide screen takes the 16-colour path an ST's screen does */
    is_st = STDL_SDL2_NO_256() || wide_run;
    if (wide_run) {
        SW = 640;
        SH = 480;
    } else if (STDL_SDL2_NO_256()) {
        /* an ST's largest window is 320 wide, and no mode is set */
        CHECK(SDL_CreateWindow("wide", 0, 0, 640, 480, 0) == NULL);
    }
#endif
    model = (Uint8 *)malloc((size_t)SW * SH);
    if (!model) {
        printf("FAIL no memory for the model\n");
        return 1;
    }
    win = SDL_CreateWindow("test", SDL_WINDOWPOS_CENTERED,
                           SDL_WINDOWPOS_CENTERED, SW, SH, 0);
    CHECK(win != NULL);
    CHECK(SDL_CreateWindow("again", 0, 0, SW, SH, 0) == NULL);
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
    model_place(SW, SH);
#ifdef STDL_SDL2_ST
    /* the page's rows, and the colours from here on noted */
    memset(&want_maps, 0, sizeof(want_maps));
    memset(tseen, 0, sizeof(tseen));
    tfresh = 0;
    tpal = 1;
    tgiven = tgnew = 0;
    page_rows = is_st ? STDL_GetVideoSurface()->h : 240;
#endif
    {
        const Uint8 *base;
        long bytes;
        STDL_SDL2_GetShownPage(R, &base, &bytes);
        CHECK(base == NULL);
        last_base = NULL;
    }
    make_textures();
    CHECK(SDL_RenderSetLogicalSize(R, SW + 1, 200) == -1);
    CHECK(STDL_SDL2_SetCompare(NULL, 1) == -1);
    copy(&tex[0], NULL, NULL);
    CHECK(STDL_SDL2_SetBorderIndex(R, 0x10) == -1);   /* drawn on now */
    present();
    {
        /* pixel pairs at once, of colours on the page already: an ST's
         * pair tables as they are made at the first pairs copy, as no
         * new maps come to make them again */
        SDL_Rect a;
        a.x = a.y = 0;
        a.w = (Uint16)SW;
        a.h = (Uint16)SH;
        copy_pairs(&tex[0], &a, &a);
        present();
    }
    few_colours();
    port_sequences();
    if (toggling) {
        toggle_first();
    }
    random_run(1500);
    if (toggling) {
        toggle_rules();
        toggle_one_copy();
    }
    pairs_rules();                /* last: its screen is a lie to the check */
    {
        STDL_SDL2_Check chk;
        if (unchecked) {
            CHECK(STDL_SDL2_GetCheck(R, &chk) == -1);
        } else {
            CHECK(STDL_SDL2_GetCheck(R, &chk) == 0);
            CHECK(chk.bad == lies);
            CHECK(chk.flips > 100);
        }
    }
    return failures;
}

#ifdef STDL_SDL2_ST
/* what the reduction promises (st.h): as an ST shows colours, an STE
 * and as they are */
static const int bitses[] = { 3, 4, 8 };
static int dist3(const SDL_Color *o, const SDL_Color *c)
{
    return abs(o->r - c->r) + abs(o->g - c->g) + abs(o->b - c->b);
}

static int mixed(const SDL_Color *o, int a, int b, const SDL_Color *c)
{
    return abs((o[a].r + o[b].r + 1) / 2 - c->r) +
           abs((o[a].g + o[b].g + 1) / 2 - c->g) +
           abs((o[a].b + o[b].b + 1) / 2 - c->b);
}

static int level(int v, int bits)
{
    const int top = bits == 3 ? 7 : bits == 4 ? 15 : 255;
    return ((v * top + 127) / 255) * 255 / top;
}

static int reduce_checks(void)
{
    SDL_Color c[256], few[16], out[16];
    Uint8 a[256], b[256], seen[256];
    int i, k, ch;
    failures = 0;
    seed = 0x5EED;
    /* 15 colours and black, anywhere among the 256: each shown exactly,
     * whichever one channel tells them apart, and none mixed, with the
     * checkerboard and without; then the same 15 seen among 241 others
     * not seen, which change nothing */
    for (ch = 0; ch < 16; ch++) {
        const int others = ch >= 8;
        memset(few, 0, sizeof(few));
        for (k = 1; k < 16; k++) {
            const Uint8 apart = (Uint8)(k * 16), same = 200;
            const int w = ch & 3;
            few[k].r = w == 0 ? apart : w == 3 ? (Uint8)(1 + rndn(255)) : same;
            few[k].g = w == 1 ? apart : w == 3 ? (Uint8)rnd() : same;
            few[k].b = w == 2 || w == 3 ? apart : same;
        }
        memset(seen, 0, sizeof(seen));
        for (i = 0; i < 256; i++) {
            if (!others || i < 15) {
                c[i] = few[others ? i + 1 : rndn(16)];
                seen[i] = 1;
            } else {
                c[i].r = (Uint8)rnd();
                c[i].g = (Uint8)rnd();
                c[i].b = (Uint8)rnd();
            }
        }
        stdl_sdl2_st_reduce(c, 256, 0, 8, (ch & 4) != 0,
                            others ? seen : NULL, a, b, out);
        for (i = 0; i < 256; i++) {
            const SDL_Color o = out[a[i]];
            CHECK(a[i] < 16);
            if (!seen[i]) {
                continue;
            }
            CHECK(b[i] == a[i]);
            CHECK(o.r == c[i].r && o.g == c[i].g && o.b == c[i].b);
            CHECK((a[i] == 0) == !(c[i].r | c[i].g | c[i].b));
        }
    }
    /* any 256, as an ST shows them, an STE, and as they are, all seen or
     * some: slot 0 black, with black's, X's and what is past a short
     * palette; every slot's colour the machine's nearest to the mean of
     * the colours seen whose first slot it is; a colour not seen at the
     * nearest slot; a second slot only where the mix is nearer than the
     * first alone, and none without the flag */
    for (k = 0; k < 72; k++) {
        const int bits = bitses[k % 3];
        const int dither = k & 1;
        const unsigned x = (k & 2) ? 0x70 : 0;
        const int n = (k & 4) ? 1 + rndn(256) : 256;
        const int part = (k & 8) ? 1 + rndn(4) : 0;    /* 1 in part seen */
        unsigned sum[16][3], cnt[16];
        Uint32 made;
        int mixes = 0, nseen = 0;
        for (i = 0; i < 256; i++) {
            c[i].r = (Uint8)rnd();
            c[i].g = (Uint8)rnd();
            c[i].b = (Uint8)rnd();
            seen[i] = (Uint8)(!part || rndn(part) == 0);
        }
        c[5].r = c[5].g = c[5].b = 0;
        made = stdl_sdl2_st_reduce(c, n, x, bits, dither, part ? seen : NULL,
                                   a, b, out);
        memset(sum, 0, sizeof(sum));
        memset(cnt, 0, sizeof(cnt));
        CHECK(!(out[0].r | out[0].g | out[0].b));
        for (i = 0; i < 256; i++) {
            const int gone = i >= n || (x && (unsigned)i == x) ||
                             !(c[i].r | c[i].g | c[i].b);
            CHECK(a[i] < 16 && b[i] < 16);
            if (gone) {
                CHECK(a[i] == 0 && b[i] == 0);
                continue;
            }
            if (seen[i]) {
                sum[a[i]][0] += c[i].r;
                sum[a[i]][1] += c[i].g;
                sum[a[i]][2] += c[i].b;
                cnt[a[i]]++;
                nseen++;
            } else {
                int j;
                for (j = 0; j < 16; j++) {
                    CHECK(dist3(&out[a[i]], &c[i]) <= dist3(&out[j], &c[i]));
                }
            }
            mixes += b[i] != a[i];
            if (dither) {
                /* the second slot the nearest mix with the first, by
                 * more than an eighth of the two slots' distance, the
                 * lowest of equals; the first if none is */
                int j, best = dist3(&out[a[i]], &c[i]), bk = a[i];
                for (j = 0; j < 16 && best; j++) {
                    const int d = mixed(out, a[i], j, &c[i]) +
                                  dist3(&out[a[i]], &out[j]) / 8;
                    if (j != a[i] && d < best) {
                        best = d;
                        bk = j;
                    }
                }
                CHECK(b[i] == bk);
            }
        }
        CHECK(dither ? (nseen < 64 || mixes > 0) : mixes == 0);
        /* the error returned: the colours seen against the mix of their
         * two slots' means as they are (not the machine's levels), as a
         * share of those colours' size, 256ths of a 16th */
        {
            Uint32 err = 0, size = 0;
            SDL_Color raw[16];
            memset(raw, 0, sizeof(raw));
            for (i = 1; i < 16; i++) {
                if (cnt[i]) {
                    raw[i].r = (Uint8)((sum[i][0] + cnt[i] / 2) / cnt[i]);
                    raw[i].g = (Uint8)((sum[i][1] + cnt[i] / 2) / cnt[i]);
                    raw[i].b = (Uint8)((sum[i][2] + cnt[i] / 2) / cnt[i]);
                }
            }
            for (i = 0; i < n; i++) {
                if (!seen[i] || (x && (unsigned)i == x) ||
                    !(c[i].r | c[i].g | c[i].b)) {
                    continue;
                }
                err += (Uint32)mixed(raw, a[i], b[i], &c[i]);
                size += (Uint32)c[i].r + c[i].g + c[i].b;
            }
            CHECK(made == (err << 8) / ((size >> 4) + 1));
        }
        for (i = 1; i < 16; i++) {
            if (cnt[i]) {
                CHECK(out[i].r == level((int)((sum[i][0] + cnt[i] / 2) / cnt[i]), bits));
                CHECK(out[i].g == level((int)((sum[i][1] + cnt[i] / 2) / cnt[i]), bits));
                CHECK(out[i].b == level((int)((sum[i][2] + cnt[i] / 2) / cnt[i]), bits));
            } else if (nseen >= 64) {
                CHECK(0);       /* every slot used where there is enough */
            }
        }
    }
    /* the maps kept and made again: kept for the same colours, for a
     * fade's steps, at black and on the way back, and when a colour not
     * seen changes; their colours the kept maps' over the colours seen;
     * made again for other colours */
    for (ch = 0; ch < 12; ch++) {
        /* as an ST, an STE and as they are: the levels a fade's last
         * steps fall between must not make the maps again */
        const int bits = bitses[ch >> 2];
        const int dither = ch & 1;
        const Uint8 *sn = (ch & 2) ? seen : NULL;
        struct stdl_sdl2_st_maps m, kept;
        SDL_Color f[256], want_c[16];
        int moved = 0;
        for (i = 0; i < 256; i++) {
            c[i].r = (Uint8)i;
            c[i].g = (Uint8)(i * 7);
            c[i].b = (Uint8)(255 - i);
            seen[i] = (Uint8)(i & 1);
        }
        memset(&m, 0, sizeof(m));
        CHECK(!stdl_sdl2_st_keep(c, 256, 0, bits, sn, &m, out));
        CHECK(stdl_sdl2_st_remake(c, 256, 0, bits, dither, sn, &m, out) == 1);
        CHECK(stdl_sdl2_st_keep(c, 256, 0, bits, sn, &m, out));
        kept = m;
        for (k = 14; k >= 1; k--) {
            for (i = 0; i < 256; i++) {
                f[i].r = (Uint8)(c[i].r * k / 15);
                f[i].g = (Uint8)(c[i].g * k / 15);
                f[i].b = (Uint8)(c[i].b * k / 15);
            }
            if (!stdl_sdl2_st_keep(f, 256, 0, bits, sn, &m, out)) {
                moved += stdl_sdl2_st_remake(f, 256, 0, bits, dither, sn, &m, out);
            }
        }
        CHECK(moved == 0);
        CHECK(memcmp(kept.a, m.a, 256) == 0 && memcmp(kept.b, m.b, 256) == 0);
        stdl_sdl2_st_fit(f, 256, 0, bits, sn, kept.a, kept.b, want_c);
        CHECK(memcmp(want_c, out, sizeof(want_c)) == 0);
        memset(f, 0, sizeof(f));
        CHECK(stdl_sdl2_st_keep(f, 256, 0, bits, sn, &m, out));
        CHECK(stdl_sdl2_st_keep(c, 256, 0, bits, sn, &m, out));
        if (sn) {
            /* a colour not seen, changed: the same fit, the same colours */
            SDL_Color before[16];
            Uint32 e0, e1;
            e0 = stdl_sdl2_st_fit(c, 256, 0, bits, sn, m.a, m.b, before);
            memcpy(f, c, sizeof(f));
            f[2].r ^= 0x80;
            e1 = stdl_sdl2_st_fit(f, 256, 0, bits, sn, m.a, m.b, out);
            CHECK(e0 == e1 && memcmp(before, out, sizeof(out)) == 0);
        }
        for (i = 0; i < 256; i++) {
            f[i].r = (Uint8)rnd();
            f[i].g = (Uint8)rnd();
            f[i].b = (Uint8)rnd();
        }
        CHECK(!stdl_sdl2_st_keep(f, 256, 0, bits, sn, &m, out));
        CHECK(stdl_sdl2_st_remake(f, 256, 0, bits, dither, sn, &m, out) == 1);
    }
    if (failures) {
        printf("test_render_st: the reduction failed\n");
    }
    return failures;
}
#endif

#ifdef STDL_SDL2_ST
/*
 * An ST's pair conversion (stpair.c) on its own: random maps, boxes of
 * up to 20 groups by 7 rows with strides wider than the box, starting
 * on an even row or an odd one. Rows in pairs come out byte for byte
 * as the whole conversion gives them, and both as a pixel-by-pixel
 * conversion through the maps (the first slot where x + the row is
 * even); rows not in pairs as that conversion of the second pixel of
 * each pair. Nothing past a row's groups is written. On the machine it
 * is the assembler.
 */
static Uint32 ptabs_w[2 * 1024], ptabs_p[2 * 1024];
static Uint8 pc_src[7 * 336], pc_got[7 * 184], pc_want[7 * 184];

static int pair_checks(void)
{
    Uint8 a[256], b[256];
    int k, i;
    failures = 0;
    seed = 0x9A125;
    for (k = 0; k < 96; k++) {
        const int paired = k & 1, odd = (k >> 1) & 1;
        const long groups = 1 + rndn(20), h = 1 + rndn(7);
        const long sp = 16 * groups + 2 * rndn(9);
        const long dp = 8 * groups + 8 * rndn(3);
        const Uint32 *t0 = ptabs_w + (odd ? 1024 : 0);
        const Uint32 *t1 = ptabs_w + (odd ? 0 : 1024);
        long x, y;
        for (i = 0; i < 256; i++) {
            a[i] = (Uint8)(rnd() & 15);
            b[i] = (Uint8)((k & 4) ? a[i] : rnd() & 15);
        }
        stdl_sdl2_st_spread(a, b, ptabs_w);
        stdl_sdl2_st_pairtabs(ptabs_w, ptabs_p);
        fill_random(pc_src, (long)sizeof(pc_src));
        for (y = 0; y < h && paired; y++) {
            for (x = 0; x < 16 * groups; x += 2) {
                pc_src[y * sp + x + 1] = pc_src[y * sp + x];
            }
        }
        /* the pixels as the maps show them, the second of each pair */
        memset(pc_want, 0xA5, sizeof(pc_want));
        for (y = 0; y < h; y++) {
            memset(pc_want + y * dp, 0, (size_t)(8 * groups));
            for (x = 0; x < 16 * groups; x++) {
                st_pixel(pc_want + y * dp, x, x + y + odd, a, b,
                         pc_src[y * sp + (x | 1)]);
            }
        }
        memset(pc_got, 0xA5, sizeof(pc_got));
        stdl_sdl2_st_c2pp(pc_src, sp, pc_got, dp, groups, h,
                          ptabs_p + (odd ? 1024 : 0),
                          ptabs_p + (odd ? 0 : 1024));
        CHECK(memcmp(pc_got, pc_want, sizeof(pc_got)) == 0);
        if (paired) {
            memset(pc_got, 0xA5, sizeof(pc_got));
            stdl_sdl2_st_c2p(pc_src, sp, pc_got, dp, groups, h, t0, t1);
            CHECK(memcmp(pc_got, pc_want, sizeof(pc_got)) == 0);
        }
        step++;
    }
    if (failures) {
        printf("test_render_st: the pair conversion failed\n");
    }
    return failures;
}
#endif

#if !defined(__m68k__)
/* fn(a, b, c, d) in a process of its own, as the screen is set up once
 * a program: 1 if it failed */
static int forked(int (*fn)(int, int, int, int), int a, int b, int c, int d)
{
    const pid_t pid = fork();
    int status = 0;
    if (pid == 0) {
        _exit(fn(a, b, c, d) ? 1 : 0);
    }
    waitpid(pid, &status, 0);
    return !WIFEXITED(status) || WEXITSTATUS(status) != 0;
}
#endif

#if !defined(__m68k__) && defined(STDL_SDL2_ST)
static int main256(void);

/* the tall screen's 240 rows; 200 where the border cannot open, which
 * is ST Low's page and may have three; a border cut short; and the
 * last, the tall screen there and the hint against it */
static const unsigned st_xs[] = { 0, 0x70 };
static const int st_rows[] = { 240, 200, 216, 0 };

static int st_screen(int ri, int pages, int xi, int unused)
{
    const int want_rows = st_rows[ri] ? st_rows[ri] : 200;
    const int got = want_rows > 200 ? 2 : pages;
    int f;
    (void)unused;
    fake_third = 1;
    fake_st_rows = st_rows[ri] ? st_rows[ri] : 240;
    if (!st_rows[ri]) {
        SDL_SetHint(STDL_SDL2_HINT_ST_TALL, "0");
    }
    fake_mach.mch_cookie = 0;
    /* an STE's 4 bits with three pages asked for, an ST's 3 with two;
     * no checkerboard on the 200 rows of a border that did not open */
    fake_mach.is_ste = pages == 3;
    if (ri == 1) {
        SDL_SetHint(STDL_SDL2_HINT_ST_DITHER, "0");
    }
    SDL_SetHint(STDL_SDL2_HINT_PAGES, pages == 2 ? "2" : "3");
    f = run(0xC0FFEEUL + (unsigned long)pages, st_xs[xi], xi == 1);
    if (fake_npages != got || fake_stride != 160 ||
        fake_tall != (st_rows[ri] != 0) || page_rows != want_rows) {
        printf("FAIL %d rows, %d pages asked for\n", st_rows[ri], pages);
        f++;
    }
    return f;
}

/* the wide screen, 640x480 in 16 colours through the same path: a TT
 * and a Falcon, two pages and three, X 0 and 0x70, and a Falcon's RGB
 * that could not open past 400 rows */
static const struct {
    unsigned long mch;
    int rows, pages;
    unsigned x;
} wides[] = {
    { 0x00020000UL, 480, 3, 0 }, { 0x00020000UL, 480, 2, 0x70 },
    { 0x00030000UL, 480, 2, 0 }, { 0x00030000UL, 480, 3, 0x70 },
    { 0x00030000UL, 400, 3, 0 }, { 0x00030000UL, 400, 2, 0x70 }
};

static int wide_screen(int k, int b, int c, int d)
{
    int f;
    (void)b;
    (void)c;
    (void)d;
    fake_third = 1;
    fake_mach.mch_cookie = wides[k].mch;
    fake_mach.is_ste = 1;
    fake_wide_rows = wides[k].rows;
    wide_run = 1;
    SDL_SetHint(STDL_SDL2_HINT_PAGES, wides[k].pages == 2 ? "2" : "3");
    f = run(0xBEEFUL + (unsigned long)k, wides[k].x, wides[k].x != 0);
    if (fake_npages != wides[k].pages || fake_stride != 320 ||
        !fake_wide || page_rows != wides[k].rows) {
        printf("FAIL wide: %d rows, %d pages\n", wides[k].rows,
               wides[k].pages);
        f++;
    }
    return f;
}

/* an ST, which the ST build's SDL_Init takes */
int main(void)
{
    int ri, pages, xi, k, total = reduce_checks() + pair_checks(), runs = 0;
    /* the table the masks' spans and bits come from (internal.h): a
     * wrong entry shows only where a copy's groups end on it */
    for (ri = 0; ri <= 40; ri++) {
        if (stdl_sdl2_below[ri] != (1ULL << ri) - 1) {
            printf("FAIL stdl_sdl2_below[%d]\n", ri);
            total++;
        }
    }
    for (ri = 0; ri < 4; ri++) {
        for (pages = 2; pages <= 3; pages++) {
            for (xi = 0; xi < 2; xi++) {
                runs++;
                if (forked(st_screen, ri, pages, xi, 0)) {
                    printf("test_render_st: %d rows, %d pages, X 0x%X"
                           " failed\n", st_rows[ri], pages, st_xs[xi]);
                    total++;
                }
            }
        }
    }
    for (k = 0; k < (int)(sizeof(wides) / sizeof(wides[0])); k++) {
        runs++;
        if (forked(wide_screen, k, 0, 0, 0)) {
            printf("test_render_st: wide, %s, %d rows, %d pages, X 0x%X"
                   " failed\n", wides[k].mch == 0x00020000UL ? "TT"
                   : "Falcon", wides[k].rows, wides[k].pages, wides[k].x);
            total++;
        }
    }
    if (total) {
        printf("test_render_st: failed\n");
        return 1;
    }
    printf("test_render_st: ok (%d ST and wide screens)\n", runs);
    /* then a TT's and a Falcon's, which this build must show as the
     * other does */
    return main256();
}
#endif

#if !defined(__m68k__)
static const unsigned xs256[] = { 0, 0x70, 0x30 };

/* cmp: 0 no compare, 1 the hint, 2 turned on and off as the run goes
 * from none (the add-on's own check off unless X is 0, so that the
 * chunky copy goes unkept while the compare is off); X xs256[xi], a
 * constant at 0x70 */
static int child(int layout, int pages, int xi, int cmp)
{
    const unsigned x = xs256[xi];
    const int xconst = xi == 1;
    int f;
    fake_layout = layout;
    fake_third = 1;
    fake_mach.mch_cookie = layout == FAKE_TT ? 0x00020000UL : 0x00030000UL;
    SDL_SetHint(STDL_SDL2_HINT_PAGES, pages == 2 ? "2" : "3");
    if (cmp == 1) {
        SDL_SetHint(STDL_SDL2_HINT_COMPARE, "1");
        unchecked = !xconst && x != 0;
    } else if (cmp == 2) {
        toggling = 1;
        unchecked = x != 0;
    }
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
    static const char *names[] = { "TT", "Falcon" };
    int layout, pages, xi, cmp, total = 0, runs = 0;
    for (layout = FAKE_TT; layout <= FAKE_VGA; layout++) {
        for (pages = 2; pages <= 3; pages++) {
            for (xi = 0; xi < 3; xi++) {
                for (cmp = 0; cmp <= 2; cmp++) {
                    runs++;
                    if (forked(child, layout, pages, xi, cmp)) {
                        printf("test_render: %s, %d pages, X 0x%X%s"
                               " failed\n", names[layout], pages, xs256[xi],
                               cmp == 1 ? ", copies compared" :
                               cmp == 2 ? ", the compare toggled" : "");
                        total++;
                    }
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
#ifdef STDL_SDL2_TEST_COMPARE
    SDL_SetHint(STDL_SDL2_HINT_COMPARE, "1");
#endif
#ifdef STDL_SDL2_TEST_TOGGLE
    /* the compare turned on and off, the copy unkept while it is off */
    toggling = 1;
    unchecked = 1;
#endif
#ifdef STDL_SDL2_ST
#ifdef STDL_SDL2_TEST_WIDE
    /* SDL2RCWD: a window of 640x480, the wide screen on a TT or a
     * Falcon */
    wide_run = 1;
#endif
    f = reduce_checks();
    f += pair_checks();
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
