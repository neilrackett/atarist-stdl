/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The SDL1 add-on against models of what SDL 1.2 does, on the host.
 *
 * Surfaces: a few of them, each with a model of its pixels, palette,
 * key and clip rectangle, put through random fills, blits (clipped
 * every way, keyed, onto themselves, between palettes), palette
 * changes and conversions. The model draws a pixel at a time and
 * shares none of video.c's arithmetic; after every step every surface
 * must be its model, and every rectangle handed back the model's.
 *
 * The screen: the real SDL2 add-on on the stand-in for STDL's screen
 * (addons/sdl2/tests/fake_stdl.c), as a TT, a Falcon on VGA and on
 * RGB, with two pages and three and with a border index. The model is
 * what the display should show: the screen surface's pixels as they
 * were at each update, over the rectangle widened to its 16-pixel
 * groups, as SDL.h says. After every update the page shown is decoded
 * a pixel at a time and must be that; the registers must be the
 * colours set; and the mouse must be measured from the picture.
 *
 * Built with SDL1_TEST_ST against the SDL2 add-on's ST build it first
 * runs the screen on an ST's, where a page holds 16 slots and cannot
 * be decoded so: there the add-on's own check must agree at every
 * flip.
 */

#include "SDL.h"
#include "../../sdl2/tests/fake_stdl.h"
#include <sys/wait.h>
#include <unistd.h>

extern const char *const sdl2_hint_names[4];
extern char test_error[256];

static int failures;
static long step;

#define CHECK(c) do { if (!(c)) { if (failures < 12) { \
    printf("FAIL %s (line %d, step %ld)\n", #c, __LINE__, step); } \
    failures++; } } while (0)

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

/* ---------------------------------------------------------------- */
/* surfaces                                                         */

#define NS   5
#define MAXW 56
#define MAXH 40

typedef struct {
    SDL_Surface *s;
    int w, h;
    Uint8 px[MAXH][MAXW];
    SDL_Color pal[256];
    int ncol, keyed;
    Uint8 key;
    int cx, cy, cw, ch;         /* the clip rectangle */
    Uint8 *own;                 /* a surface made over the test's block */
} Model;

static Model m[NS];

/* SDL 1.2's map from one palette to another, as SDL.h words it: a
 * colour that is the same in both stays, any other goes to the first
 * of the nearest */
static Uint8 map_colour(const Model *src, const Model *dst, Uint8 v)
{
    long best = -1;
    int i, k = 0;
    if (src == dst || v >= src->ncol ||
        (v < dst->ncol && src->pal[v].r == dst->pal[v].r &&
         src->pal[v].g == dst->pal[v].g && src->pal[v].b == dst->pal[v].b)) {
        return v;
    }
    for (i = 0; i < dst->ncol; i++) {
        const long dr = (long)dst->pal[i].r - src->pal[v].r;
        const long dg = (long)dst->pal[i].g - src->pal[v].g;
        const long db = (long)dst->pal[i].b - src->pal[v].b;
        const long d = dr * dr + dg * dg + db * db;
        if (best < 0 || d < best) {
            best = d;
            k = i;
        }
    }
    return (Uint8)k;
}

static void check_surface(const Model *t)
{
    const Uint8 *p = (const Uint8 *)t->s->pixels;
    int x, y, bad = 0;
    for (y = 0; y < t->h; y++) {
        for (x = 0; x < t->w; x++) {
            bad += p[y * t->s->pitch + x] != t->px[y][x];
        }
    }
    CHECK(bad == 0);
    CHECK(t->s->clip_rect.x == t->cx && t->s->clip_rect.y == t->cy &&
          t->s->clip_rect.w == t->cw && t->s->clip_rect.h == t->ch);
    CHECK(((t->s->flags & SDL_SRCCOLORKEY) != 0) == t->keyed);
    CHECK(!t->keyed || t->s->format->colorkey == t->key);
}

static void check_all(void)
{
    int i;
    for (i = 0; i < NS; i++) {
        check_surface(&m[i]);
    }
}

static void new_surface(Model *t, int from)
{
    int x, y;
    memset(t, 0, sizeof(*t));
    t->w = 1 + rndn(MAXW);
    t->h = 1 + rndn(MAXH);
    if (from) {
        /* over the test's own block, its rows further apart than wide */
        const int pitch = t->w + rndn(5);
        t->own = (Uint8 *)malloc((size_t)pitch * (size_t)t->h);
        t->s = SDL_CreateRGBSurfaceFrom(t->own, t->w, t->h, 8, pitch,
                                        0, 0, 0, 0);
        CHECK(t->s && (t->s->flags & SDL_PREALLOC) && t->s->pitch == pitch);
    } else {
        t->s = SDL_CreateRGBSurface(SDL_SWSURFACE, t->w, t->h, 8, 0, 0, 0, 0);
        CHECK(t->s && t->s->pitch == ((t->w + 3) & ~3));
    }
    CHECK(t->s->format->BitsPerPixel == 8 && t->s->format->BytesPerPixel == 1);
    CHECK(t->s->format->palette->ncolors == 256 && t->s->w == t->w &&
          t->s->h == t->h && t->s->refcount == 1);
    t->ncol = 256;
    t->cw = t->w;
    t->ch = t->h;
    for (y = 0; y < t->h; y++) {
        for (x = 0; x < t->w; x++) {
            /* few values, so that keys and equal colours are met */
            t->px[y][x] = (Uint8)rndn(12);
            ((Uint8 *)t->s->pixels)[y * t->s->pitch + x] = t->px[y][x];
        }
    }
}

static void free_surface(Model *t)
{
    SDL_FreeSurface(t->s);
    free(t->own);
}

/* a rectangle about a surface: inside it, over its edges, or beyond */
static void some_rect(const Model *t, SDL_Rect *r)
{
    r->x = (Sint16)(rndn(t->w + 24) - 12);
    r->y = (Sint16)(rndn(t->h + 24) - 12);
    r->w = (Uint16)rndn(t->w + 16);
    r->h = (Uint16)rndn(t->h + 16);
}

static void do_palette(Model *t)
{
    SDL_Color c[40];
    const int first = rndn(14), n = 1 + rndn(12);
    int i;
    for (i = 0; i < n; i++) {
        /* few levels, so that two surfaces often share a colour */
        c[i].r = (Uint8)(rndn(4) * 85);
        c[i].g = (Uint8)(rndn(4) * 85);
        c[i].b = (Uint8)(rndn(4) * 85);
        c[i].unused = (Uint8)rnd();
        t->pal[first + i] = c[i];
    }
    CHECK(SDL_SetColors(t->s, c, first, n) == 1);
    for (i = 0; i < n; i++) {
        const SDL_Color *got = &t->s->format->palette->colors[first + i];
        CHECK(got->r == c[i].r && got->g == c[i].g && got->b == c[i].b);
    }
}

static void do_clip(Model *t)
{
    SDL_Rect r, got;
    int x0, y0, x1, y1;
    if (rndn(4) == 0) {
        CHECK(SDL_SetClipRect(t->s, NULL) == SDL_TRUE);
        t->cx = t->cy = 0;
        t->cw = t->w;
        t->ch = t->h;
    } else {
        SDL_bool some;
        some_rect(t, &r);
        some = SDL_SetClipRect(t->s, &r);
        x0 = r.x < 0 ? 0 : r.x;
        y0 = r.y < 0 ? 0 : r.y;
        x1 = r.x + r.w > t->w ? t->w : r.x + r.w;
        y1 = r.y + r.h > t->h ? t->h : r.y + r.h;
        t->cx = x0;
        t->cy = y0;
        t->cw = x1 > x0 && y1 > y0 ? x1 - x0 : 0;
        t->ch = x1 > x0 && y1 > y0 ? y1 - y0 : 0;
        CHECK((some == SDL_TRUE) == (t->cw > 0));
    }
    SDL_GetClipRect(t->s, &got);
    CHECK(got.w == t->cw && got.h == t->ch &&
          (!t->cw || (got.x == t->cx && got.y == t->cy)));
    /* an empty one's corner is SDL's to choose */
    t->cx = t->s->clip_rect.x;
    t->cy = t->s->clip_rect.y;
}

static void do_fill(Model *t)
{
    SDL_Rect r;
    const Uint8 c = (Uint8)rndn(12);
    const int whole = rndn(5) == 0;
    int x, y, x0 = t->cx, y0 = t->cy, x1 = t->cx + t->cw, y1 = t->cy + t->ch;
    if (!whole) {
        some_rect(t, &r);
        x0 = r.x > x0 ? r.x : x0;
        y0 = r.y > y0 ? r.y : y0;
        x1 = r.x + r.w < x1 ? r.x + r.w : x1;
        y1 = r.y + r.h < y1 ? r.y + r.h : y1;
    }
    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            t->px[y][x] = c;
        }
    }
    CHECK(SDL_FillRect(t->s, whole ? NULL : &r, 0xABCD00u | c) == 0);
    if (!whole) {
        if (x1 > x0 && y1 > y0) {
            CHECK(r.x == x0 && r.y == y0 && r.w == x1 - x0 && r.h == y1 - y0);
        } else {
            CHECK(r.w == 0 && r.h == 0);
        }
    }
}

static void do_blit(Model *src, Model *dst)
{
    static Uint8 was[MAXH][MAXW];
    SDL_Rect sr, dr;
    const int whole_src = rndn(4) == 0, no_dst = rndn(6) == 0;
    int sx, sy, sw, sh, dx, dy, i, j;
    int bx0 = 32767, by0 = 32767, bx1 = -32768, by1 = -32768;
    some_rect(src, &sr);
    dr.x = (Sint16)(rndn(dst->w + 24) - 12);
    dr.y = (Sint16)(rndn(dst->h + 24) - 12);
    dr.w = dr.h = 999;          /* not read */
    sx = whole_src ? 0 : sr.x;
    sy = whole_src ? 0 : sr.y;
    sw = whole_src ? src->w : sr.w;
    sh = whole_src ? src->h : sr.h;
    dx = no_dst ? 0 : dr.x;
    dy = no_dst ? 0 : dr.y;
    /* a surface onto itself reads what was there before */
    memcpy(was, src->px, sizeof(was));
    for (j = 0; j < sh; j++) {
        for (i = 0; i < sw; i++) {
            const int px = sx + i, py = sy + j, qx = dx + i, qy = dy + j;
            if (px < 0 || py < 0 || px >= src->w || py >= src->h ||
                qx < dst->cx || qy < dst->cy || qx >= dst->cx + dst->cw ||
                qy >= dst->cy + dst->ch) {
                continue;
            }
            bx0 = qx < bx0 ? qx : bx0;
            by0 = qy < by0 ? qy : by0;
            bx1 = qx > bx1 ? qx : bx1;
            by1 = qy > by1 ? qy : by1;
            if (!src->keyed || was[py][px] != src->key) {
                dst->px[qy][qx] = map_colour(src, dst, was[py][px]);
            }
        }
    }
    CHECK(SDL_BlitSurface(src->s, whole_src ? NULL : &sr, dst->s,
                          no_dst ? NULL : &dr) == 0);
    if (!no_dst) {
        if (bx1 >= bx0) {
            CHECK(dr.x == bx0 && dr.y == by0 && dr.w == bx1 - bx0 + 1 &&
                  dr.h == by1 - by0 + 1);
        } else {
            CHECK(dr.w == 0 && dr.h == 0);
        }
    }
}

static void do_key(Model *t)
{
    t->keyed = rndn(3) != 0;
    t->key = (Uint8)(t->keyed ? rndn(12) : 0);
    CHECK(SDL_SetColorKey(t->s, t->keyed ? SDL_SRCCOLORKEY | SDL_RLEACCEL : 0,
                          0x7700u | t->key) == 0);
}

/* STDL's single-pixel calls on a surface: a write inside the clip
 * rectangle, a read inside the surface, and nothing or 0 elsewhere */
static void do_pixel(Model *t)
{
    const int x = rndn(t->w + 8) - 4, y = rndn(t->h + 8) - 4;
    const int in = x >= 0 && y >= 0 && x < t->w && y < t->h;
    const Uint8 c = (Uint8)rndn(12);
    CHECK(STDL_GetPixel(t->s, x, y) == (in ? t->px[y][x] : 0));
    STDL_PutPixel(t->s, x, y, c);
    if (x >= t->cx && y >= t->cy && x < t->cx + t->cw && y < t->cy + t->ch) {
        t->px[y][x] = c;
    }
    CHECK(STDL_GetPixel(t->s, x, y) == (in ? t->px[y][x] : 0));
}

static void do_count(const Model *t)
{
    Uint8 used[256], seen[256];
    int x, y, n = 0;
    memset(seen, 0, sizeof(seen));
    for (y = 0; y < t->h; y++) {
        for (x = 0; x < t->w; x++) {
            seen[t->px[y][x]] = 1;
        }
    }
    for (x = 0; x < 256; x++) {
        n += seen[x];
    }
    CHECK(STDL_SDL1_CountColours(t->s, used) == n);
    CHECK(memcmp(used, seen, sizeof(seen)) == 0);
    CHECK(STDL_SDL1_CountColours(t->s, NULL) == n);
}

static void surfaces(void)
{
    int i;
    for (i = 0; i < NS; i++) {
        new_surface(&m[i], i & 1);
    }
    check_all();
    for (step = 0; step < 6000; step++) {
        Model *a = &m[rndn(NS)], *b = &m[rndn(NS)];
        switch (rndn(16)) {
        case 0:
            do_palette(a);
            break;
        case 1:
            do_clip(a);
            break;
        case 2:
            do_key(a);
            break;
        case 3:
        case 4:
            do_fill(a);
            break;
        case 5:
            do_count(a);
            break;
        case 7:
            do_pixel(a);
            break;
        case 6:
            if (rndn(8) == 0) {
                free_surface(a);
                new_surface(a, rndn(2));
            }
            break;
        default:
            /* a keyed surface onto itself is no one's promise */
            if (a == b && a->keyed) {
                do_key(a);
                a->keyed = 0;
                CHECK(SDL_SetColorKey(a->s, 0, 0) == 0);
            }
            do_blit(a, b);
            break;
        }
        check_all();
    }
    for (i = 0; i < NS; i++) {
        free_surface(&m[i]);
    }
    /* what it has not got */
    CHECK(SDL_CreateRGBSurface(0, 8, 8, 16, 0, 0, 0, 0) == NULL);
    CHECK(SDL_CreateRGBSurface(0, 0, 8, 8, 0, 0, 0, 0) == NULL);
    CHECK(SDL_CreateRGBSurfaceFrom(NULL, 8, 8, 8, 8, 0, 0, 0, 0) == NULL);
    CHECK(SDL_BlitSurface(NULL, NULL, NULL, NULL) == -1);
    CHECK(SDL_FillRect(NULL, NULL, 0) == -1);
    CHECK(SDL_DisplayFormat(NULL) == NULL);
    SDL_FreeSurface(NULL);
}

/* ---------------------------------------------------------------- */
/* the screen                                                       */

static SDL_Surface *scr;
static int W, H, OX, OY;        /* the picture, and its place */
static unsigned X;              /* the border index */
static int on_st;
/* the screen's size: 320x240, or 640x480 where the first mode is wide
 * (a TT's or a Falcon's 16 colours, through the experimental build) */
static int SW = 320, SH = 240;
/* what the display should show of it: a pixel's value, or -1 where no
 * update has reached, which is the cleared screen's register 0 */
static short shown[480][640];
/* and in which colours; and the screen surface's own palette, which
 * SDL_LOGPAL moves and SDL_PHYSPAL leaves */
static SDL_Color phys[256], logi[256];

/* the page on show, a pixel of it: eight plane words a group, the
 * pixel's bits XORed with the border index as the add-on stores them */
static int page_px(int x, int y)
{
    const Uint8 *g = fake_page[fake_shown] + (long)y * fake_stride +
                     (x >> 4) * 16;
    const int bit = 15 - (x & 15);
    int k, v = 0;
    for (k = 0; k < 8; k++) {
        v |= (((g[2 * k] << 8 | g[2 * k + 1]) >> bit) & 1) << k;
    }
    return v;
}

static void check_page(void)
{
    int x, y, bad = 0, twice = 0;
    Uint32 flips, wrong;
    CHECK(STDL_SDL1_GetCheck(&flips, &wrong) == 0 && wrong == 0);
    if (on_st) {
        return;
    }
    for (y = 0; y < 240; y++) {
        for (x = 0; x < 320; x++) {
            const int in = x >= OX && x < OX + W && y >= OY && y < OY + H;
            /* outside the picture is register 0 */
            const int v = in ? shown[y - OY][x - OX] : -1;
            const int want = v < 0 ? 0 : (v ^ (int)X);
            bad += page_px(x, y) != want;
        }
        /* a TT's row is two lines, alike */
        if (fake_layout == FAKE_TT) {
            const Uint8 *row = fake_page[fake_shown] + (long)y * fake_stride;
            twice += memcmp(row, row + 320, 320) != 0;
        }
    }
    CHECK(bad == 0);
    CHECK(twice == 0);
}

/* register i has colour i ^ X, and register 0 is black where X is set */
static void check_colours(void)
{
    int i, bad = 0;
    if (on_st) {
        return;
    }
    for (i = 0; i < 256; i++) {
        const SDL_Color c = phys[(unsigned)i ^ X];
        const int black = X && i == 0;
        bad += fake_regs[i].r != (black ? 0 : c.r) ||
               fake_regs[i].g != (black ? 0 : c.g) ||
               fake_regs[i].b != (black ? 0 : c.b);
    }
    CHECK(bad == 0);
}

/* an update's rectangle in the model: as given, widened to its groups
 * and cut to the picture */
static void show(int x, int y, int w, int h)
{
    int x0 = x & ~15, x1 = (x + w + 15) & ~15, y0 = y, y1 = y + h, i, j;
    x0 = x0 < 0 ? 0 : x0;
    y0 = y0 < 0 ? 0 : y0;
    x1 = x1 > W ? W : x1;
    y1 = y1 > H ? H : y1;
    for (j = y0; j < y1; j++) {
        for (i = x0; i < x1; i++) {
            shown[j][i] = ((const Uint8 *)scr->pixels)[j * scr->pitch + i];
        }
    }
}

static void mode(int w, int h)
{
    int i, x, y;
    scr = SDL_SetVideoMode(w, h, 8, SDL_SWSURFACE);
    CHECK(scr != NULL);
    if (!scr) {
        printf("SDL_SetVideoMode: %s\n", test_error);
        exit(1);
    }
    W = w;
    H = h;
    OX = ((SW - w) / 2) & ~15;
    OY = (SH - h) / 2;
    CHECK(scr == SDL_GetVideoSurface() && scr->w == w && scr->h == h &&
          scr->format->BitsPerPixel == 8 && scr->pitch >= w &&
          !(scr->pitch & 1) && !((size_t)scr->pixels & 1));
    CHECK(SDL_GetVideoInfo()->current_w == w && SDL_GetVideoInfo()->vfmt ==
          scr->format);
    memset(shown, 0xFF, sizeof(shown));
    /* SDL 1.2's first palette, on the display */
    for (i = 0; i < 256; i++) {
        phys[i] = logi[i] = scr->format->palette->colors[i];
        CHECK(phys[i].r == (((i & 0xE0) * 255 + 112) / 224) &&
              phys[i].b == (i & 3) * 85);
    }
    check_colours();
    for (y = 0; y < h; y++) {
        for (x = 0; x < w; x++) {
            CHECK(((const Uint8 *)scr->pixels)[y * scr->pitch + x] == 0);
        }
    }
}

static void draw(void)
{
    Uint8 *p = (Uint8 *)scr->pixels;
    SDL_Rect r;
    int n, i;
    switch (rndn(3)) {
    case 0:
        /* the program's own writes to the pixels */
        for (n = 1 + rndn(200); n > 0; n--) {
            p[rndn(H) * scr->pitch + rndn(W)] = (Uint8)rnd();
        }
        break;
    case 1:
        r.x = (Sint16)(rndn(W + 40) - 20);
        r.y = (Sint16)(rndn(H + 40) - 20);
        r.w = (Uint16)rndn(120);
        r.h = (Uint16)rndn(90);
        SDL_FillRect(scr, &r, rnd());
        break;
    default:
        /* a run along a row, across groups */
        i = rndn(H) * scr->pitch;
        for (n = rndn(W); n < W && rndn(40); n++) {
            p[i + n] = (Uint8)rnd();
        }
        break;
    }
}

static void update(void)
{
    SDL_Rect r[4];
    int n, i;
    switch (rndn(6)) {
    case 0:
        CHECK(SDL_Flip(scr) == 0);
        show(0, 0, W, H);
        break;
    case 1:
        SDL_UpdateRect(scr, 0, 0, 0, 0);
        show(0, 0, W, H);
        break;
    case 2:
        r[0].x = (Sint16)rndn(W);
        r[0].y = (Sint16)rndn(H);
        r[0].w = (Uint16)(1 + rndn(W - r[0].x));
        r[0].h = (Uint16)(1 + rndn(H - r[0].y));
        SDL_UpdateRect(scr, r[0].x, r[0].y, r[0].w, r[0].h);
        show(r[0].x, r[0].y, r[0].w, r[0].h);
        break;
    default:
        n = rndn(5);
        for (i = 0; i < n; i++) {
            /* some over the picture's edges, which SDL 1.2 forbids and
             * the add-on cuts */
            r[i].x = (Sint16)(rndn(W + 20) - 10);
            r[i].y = (Sint16)(rndn(H + 20) - 10);
            r[i].w = (Uint16)rndn(100);
            r[i].h = (Uint16)rndn(60);
            show(r[i].x, r[i].y, r[i].w, r[i].h);
        }
        SDL_UpdateRects(scr, n, r);
        break;
    }
    check_page();
}

static void colours(void)
{
    SDL_Color c[256];
    const int first = rndn(256), n = 1 + rndn(256 - first);
    const int flags = 1 + rndn(3);
    int i;
    for (i = 0; i < n; i++) {
        c[i].r = (Uint8)rnd();
        c[i].g = (Uint8)rnd();
        c[i].b = (Uint8)rnd();
        c[i].unused = 0;
    }
    if (flags == (SDL_LOGPAL | SDL_PHYSPAL) && rndn(2)) {
        CHECK(SDL_SetColors(scr, c, first, n) == 1);
    } else {
        CHECK(SDL_SetPalette(scr, flags, c, first, n) == 1);
    }
    for (i = 0; i < n; i++) {
        if (flags & SDL_PHYSPAL) {
            phys[first + i] = c[i];
        }
        if (flags & SDL_LOGPAL) {
            logi[first + i] = c[i];
        }
    }
    for (i = 0; i < 256; i++) {
        const SDL_Color *l = &scr->format->palette->colors[i];
        CHECK(l->r == logi[i].r && l->g == logi[i].g && l->b == logi[i].b);
    }
    check_colours();
    /* the colours are not a frame: the page stays */
    check_page();
}

static void mouse(void)
{
    STDL_Event se;
    SDL_Event e;
    int x, y;
    fake_mouse_x = rndn(320);
    fake_mouse_y = rndn(240);
    fake_mouse_buttons = (uint8_t)rndn(4);
    CHECK(SDL_GetMouseState(&x, &y) == fake_mouse_buttons);
    CHECK(x == fake_mouse_x - OX && y == fake_mouse_y - OY);
    while (SDL_PollEvent(&e)) {
    }
    memset(&se, 0, sizeof(se));
    se.type = rndn(2) ? STDL_MOUSEMOTION : STDL_MOUSEBUTTONDOWN;
    if (se.type == STDL_MOUSEMOTION) {
        se.motion.x = (uint16_t)(OX + 7);
        se.motion.y = (uint16_t)(OY + 9);
    } else {
        se.button.x = (uint16_t)(OX + 7);
        se.button.y = (uint16_t)(OY + 9);
    }
    fake_event(&se);
    fake_vbl++;
    CHECK(SDL_PollEvent(&e) == 1 && e.type == se.type);
    if (se.type == STDL_MOUSEMOTION) {
        CHECK(e.motion.x == 7 && e.motion.y == 9);
    } else {
        CHECK(e.button.x == 7 && e.button.y == 9);
    }
    CHECK(SDL_PollEvent(&e) == 0);
}

/* a picture in another palette, blitted and converted: in the screen's
 * colours either way */
static void convert(void)
{
    SDL_Surface *s = SDL_CreateRGBSurface(0, 16, 4, 8, 0, 0, 0, 0), *d;
    SDL_Color c[16];
    SDL_Rect at;
    Uint8 want[16];
    int i, k;
    for (i = 0; i < 16; i++) {
        long best = -1;
        c[i].r = (Uint8)rnd();
        c[i].g = (Uint8)rnd();
        c[i].b = (Uint8)rnd();
        c[i].unused = 0;
        ((Uint8 *)s->pixels)[i] = (Uint8)i;
        want[i] = 0;
        for (k = 0; k < 256; k++) {
            const SDL_Color *q = &scr->format->palette->colors[k];
            const long dr = (long)q->r - c[i].r, dg = (long)q->g - c[i].g,
                       db = (long)q->b - c[i].b;
            const long dist = dr * dr + dg * dg + db * db;
            if (k == i && dist == 0) {
                want[i] = (Uint8)i;
                break;
            }
            if (best < 0 || dist < best) {
                best = dist;
                want[i] = (Uint8)k;
            }
        }
    }
    SDL_SetColors(s, c, 0, 16);
    SDL_SetColorKey(s, SDL_SRCCOLORKEY, 5);
    d = SDL_DisplayFormat(s);
    CHECK(d != NULL && d->w == 16 && d->h == 4);
    CHECK(d && memcmp(d->pixels, want, 16) == 0);
    CHECK(d && (d->flags & SDL_SRCCOLORKEY) && d->format->colorkey == want[5]);
    CHECK(d && memcmp(d->format->palette->colors,
                      scr->format->palette->colors, 256 * 4) == 0);
    /* and the converted one blits as it is, the unconverted one mapped */
    SDL_SetColorKey(s, 0, 0);
    at.x = at.y = 0;
    SDL_BlitSurface(s, NULL, scr, &at);
    CHECK(memcmp(scr->pixels, want, 16) == 0);
    memset(scr->pixels, 0, 16);
    if (d) {
        SDL_SetColorKey(d, 0, 0);
        SDL_BlitSurface(d, NULL, scr, &at);
        CHECK(memcmp(scr->pixels, want, 16) == 0);
    }
    SDL_FreeSurface(d);
    SDL_FreeSurface(s);
    /* the screen is the mode's: freeing it does nothing */
    SDL_FreeSurface(scr);
    CHECK(SDL_GetVideoSurface() == scr);
}

static int screen(unsigned long s, unsigned x)
{
    static const int narrow[][2] = {
        { 320, 200 }, { 320, 240 }, { 256, 224 }, { 300, 180 }, { 32, 16 }
    };
    /* the first sets the screen up, the wide one; then smaller */
    static const int wide[][2] = {
        { 640, 480 }, { 640, 400 }, { 320, 240 }, { 600, 300 }, { 32, 16 }
    };
    const int (*sizes)[2] = SW > 320 ? wide : narrow;
    char name[32];
    int si, k;
    seed = s;
    X = x;
    failures = 0;
    for (k = 0; k < 4; k++) {
        static const char *const mine[4] = {
            STDL_SDL1_HINT_PAGES, STDL_SDL1_HINT_CHECK,
            STDL_SDL1_HINT_ST_TALL, STDL_SDL1_HINT_ST_DITHER
        };
        CHECK(strcmp(mine[k], sdl2_hint_names[k]) == 0);
    }
    CHECK(STDL_SDL1_GetCheck(NULL, NULL) == -1);
    CHECK(STDL_SDL1_SetHint(STDL_SDL1_HINT_CHECK, "1") == 1);
    if (x) {
        CHECK(STDL_SDL1_SetBorderIndex(x) == 0);
    }
    {
        /* 640x480 over the experimental build on a TT or a Falcon */
        const int wide_ok = SDL_VideoModeOK(640, 480, 8, 0) == 8;
        SDL_Rect **m = SDL_ListModes(NULL, 0);
#ifdef SDL1_TEST_ST
        CHECK(wide_ok == (fake_mach.mch_cookie >= 0x00020000UL));
#else
        CHECK(!wide_ok);
#endif
        CHECK(SDL_VideoModeOK(320, 240, 8, 0) == 8 &&
              SDL_VideoModeOK(641, 480, 8, 0) == 0 &&
              SDL_VideoModeOK(640, 481, 8, 0) == 0);
        CHECK(m && m[0] && m[0]->w == (wide_ok ? 640 : 320) &&
              m[0]->h == (wide_ok ? 480 : 240));
        if (!wide_ok) {
            CHECK(SDL_SetVideoMode(640, 480, 8, 0) == NULL);
        }
    }
    CHECK(SDL_SetVideoMode(320, 200, 16, 0) == NULL);
    CHECK(SDL_DisplayFormat(NULL) == NULL && SDL_GetVideoSurface() == NULL);
    CHECK(SDL_VideoDriverName(name, sizeof(name)) == name &&
          strcmp(name, "stdl-sdl1") == 0);
    for (si = 0; si < 5; si++) {
        /* the first asks for another depth and takes what there is */
        if (si == 0) {
            scr = SDL_SetVideoMode(sizes[0][0], sizes[0][1], 16,
                                   SDL_ANYFORMAT);
            CHECK(scr && scr->format->BitsPerPixel == 8);
        }
        mode(sizes[si][0], sizes[si][1]);
        /* the screen there is, once a mode is set: no wider mode */
        CHECK(SDL_VideoModeOK(SW + 1, 200, 8, 0) == 0 &&
              SDL_VideoModeOK(SW, SH, 8, 0) == 8);
        /* the border index is the screen's from its start */
        CHECK(STDL_SDL1_SetBorderIndex(0x30) == -1);
        for (step = 0; step < 220; step++) {
            switch (rndn(12)) {
            case 0:
                colours();
                break;
            case 1:
                /* an ST's picture is placed by its screen: the SDL2
                 * add-on's own tests have the mouse there */
                if (!on_st) {
                    mouse();
                }
                break;
            case 2:
                convert();
                break;
            case 3:
            case 4:
            case 5:
                update();
                break;
            default:
                draw();
                break;
            }
        }
        SDL_Flip(scr);
        show(0, 0, W, H);
        check_page();
    }
    if (!on_st) {
        SDL_WarpMouse(5, 6);
        CHECK(fake_warp_x == OX + 5 && fake_warp_y == OY + 6);
    }
    {
        Uint32 flips = 0, wrong = 1;
        CHECK(STDL_SDL1_GetCheck(&flips, &wrong) == 0 && wrong == 0 &&
              flips > (on_st ? 0u : 100u));
    }
    return failures;
}

/* each screen in a process of its own, as the stand-in's machine and
 * the add-on's screen are set once */
static int forked(int (*run)(int, int, unsigned), int a, int b, unsigned x)
{
    const pid_t pid = fork();
    int status = 0;
    if (pid == 0) {
        const int bad = run(a, b, x);
        fflush(stdout);
        _exit(bad ? 1 : 0);
    }
    waitpid(pid, &status, 0);
    return !WIFEXITED(status) || WEXITSTATUS(status) != 0;
}

static int on_256(int layout, int pages, unsigned x)
{
    fake_layout = layout;
    fake_third = 1;
    fake_mach.mch_cookie = layout == FAKE_TT ? 0x00020000UL : 0x00030000UL;
    STDL_SDL1_SetHint(STDL_SDL1_HINT_PAGES, pages == 2 ? "2" : "3");
    return screen(0xBEEF01UL + (unsigned long)layout * 7 +
                  (unsigned long)pages, x) || fake_npages != pages;
}

#ifdef SDL1_TEST_ST
/* the wide screen: 640x480 in 16 colours on a TT or a Falcon, the first
 * mode's; checked by the SDL2 add-on's own check, as an ST's screen */
static int on_wide_screen(int falcon, int pages, unsigned x)
{
    on_st = 1;
    SW = 640;
    SH = 480;
    fake_third = 1;
    fake_wide_rows = 480;
    fake_mach.mch_cookie = falcon ? 0x00030000UL : 0x00020000UL;
    fake_mach.is_ste = 1;
    STDL_SDL1_SetHint(STDL_SDL1_HINT_PAGES, pages == 2 ? "2" : "3");
    return screen(0x31DE5UL + (unsigned long)falcon * 2 +
                  (unsigned long)pages, x) ||
           fake_npages != pages || fake_stride != 320 || !fake_wide;
}

static int on_an_st(int tall, int ste, unsigned x)
{
    on_st = 1;
    fake_third = 1;
    fake_st_rows = 240;
    fake_mach.mch_cookie = 0;
    fake_mach.is_ste = (uint8_t)ste;
    if (!tall) {
        STDL_SDL1_SetHint(STDL_SDL1_HINT_ST_TALL, "0");
    }
    if (!ste) {
        STDL_SDL1_SetHint(STDL_SDL1_HINT_ST_DITHER, "0");
    }
    return screen(0x57AB1EUL + (unsigned long)tall * 2 + (unsigned long)ste,
                  x) || fake_stride != 160;
}
#endif

int main(void)
{
    static const unsigned xs[] = { 0, 0x70, 0xF0 };
    int layout, pages, xi, bad = 0, runs = 0;
    seed = 0x5D11UL;
    surfaces();
    if (failures) {
        printf("test_sdl1: the surfaces failed\n");
        return 1;
    }
#ifdef SDL1_TEST_ST
    for (layout = 0; layout < 2; layout++) {
        for (pages = 0; pages < 2; pages++) {
            if (forked(on_an_st, layout, pages, pages ? 0x70 : 0)) {
                printf("test_sdl1_st: an ST%s, %s failed\n",
                       pages ? "E" : "", layout ? "240 rows" : "ST Low");
                bad++;
            }
            runs++;
        }
    }
    for (layout = 0; layout < 2; layout++) {
        for (pages = 2; pages <= 3; pages++) {
            if (forked(on_wide_screen, layout, pages, pages == 3 ? 0x70 : 0)) {
                printf("test_sdl1_st: the wide screen, a %s, %d pages "
                       "failed\n", layout ? "Falcon" : "TT", pages);
                bad++;
            }
            runs++;
        }
    }
#endif
    for (layout = FAKE_TT; layout <= FAKE_VGA; layout++) {
        for (pages = 2; pages <= 3; pages++) {
            for (xi = 0; xi < 3; xi++) {
                if (forked(on_256, layout, pages, xs[xi])) {
                    printf("test_sdl1: layout %d, %d pages, X 0x%X failed\n",
                           layout, pages, xs[xi]);
                    bad++;
                }
                runs++;
            }
        }
    }
    if (bad) {
        return 1;
    }
#ifdef SDL1_TEST_ST
    printf("test_sdl1_st: ok (%d screens)\n", runs);
#else
    printf("test_sdl1: ok (%d screens)\n", runs);
#endif
    return 0;
}
