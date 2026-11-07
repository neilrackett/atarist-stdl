/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * SDL_LoadBMP on the host. BMPs written here, a pixel and an entry at
 * a time, of 1, 4 and 8 bits, every width from 1 to 35 (every row
 * padding), bottom-up and top-down, with the 40-byte header and V4's
 * and V5's, colour counts given and not, a gap before the pixels and a
 * file starting part-way into its stream; each loaded and checked
 * pixel by pixel, the padding written as junk and the reserved bytes
 * of the palette too. Then what is refused, with SDL2's words: RLE,
 * 16 to 32 bits, 2 bits, OS/2's header, bad sizes, not a BMP; every
 * prefix of a file, cut short through SDL_RWFromConstMem; and files
 * read through SDL_LoadBMP's own path. Then the round trip:
 * bmp8conv.py's files, made by stdlconv bmp8 with each option and
 * read back by Pillow, against what this loads (skipped without
 * Pillow), with the core's examples' 4-bit BMPs, which other tools
 * wrote.
 */

#include "SDL.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>

static int failures;

#define CHECK(c) do { \
        if (!(c)) { \
            if (failures < 12) { \
                printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            } \
            failures++; \
        } \
    } while (0)

static void le16(Uint8 *p, int v)
{
    p[0] = (Uint8)v;
    p[1] = (Uint8)(v >> 8);
}

static void le32(Uint8 *p, long v)
{
    le16(p, (int)(v & 0xFFFF));
    le16(p + 2, (int)((unsigned long)v >> 16));
}

/* what a hand-made file's pixel and entry are */
static int pixel(int x, int y, int ncol)
{
    return (x * 7 + y * 13 + x * y) % ncol;
}

static SDL_Color entry(int i)
{
    SDL_Color c;
    c.r = (Uint8)(i * 3);
    c.g = (Uint8)(255 - i);
    c.b = (Uint8)(i ^ 0x5A);
    c.a = 255;
    return c;
}

struct shape {
    int w, h, bits;
    int topdown;
    int hsize;                  /* 40, 108 (V4), 124 (V5) */
    long used;                  /* biClrUsed as written */
    long comp;                  /* biCompression */
    int gap;                    /* bytes between palette and pixels */
};

/* a BMP of that shape into out; its size */
static int make_bmp(Uint8 *out, const struct shape *s)
{
    const int ncol = s->used ? (int)s->used : 1 << s->bits;
    const int npal = s->bits <= 8 ? ncol : 0;
    const int row = ((s->w * s->bits + 31) / 32) * 4;
    const long off = 14 + s->hsize + npal * 4 + s->gap;
    int x, y, i;
    Uint8 *p;
    memset(out, 0xA5, (size_t)(off + row * s->h));   /* junk: padding */
    out[0] = 'B';
    out[1] = 'M';
    le32(out + 2, off + row * s->h);
    le32(out + 6, 0);
    le32(out + 10, off);
    memset(out + 14, 0, (size_t)s->hsize);
    le32(out + 14, s->hsize);
    le32(out + 18, s->w);
    le32(out + 22, s->topdown ? -s->h : s->h);
    le16(out + 26, 1);
    le16(out + 28, s->bits);
    le32(out + 30, s->comp);
    le32(out + 34, row * s->h);
    le32(out + 46, s->used);
    for (i = 0; i < npal; i++) {
        const SDL_Color c = entry(i);
        p = out + 14 + s->hsize + i * 4;
        p[0] = c.b;
        p[1] = c.g;
        p[2] = c.r;
        p[3] = 0x77;            /* reserved: must not be read as alpha */
    }
    for (y = 0; y < s->h; y++) {
        p = out + off + (long)row * (s->topdown ? y : s->h - 1 - y);
        memset(p, 0, (size_t)((s->w * s->bits + 7) / 8));
        for (x = 0; x < s->w; x++) {
            const int v = s->bits <= 8 ? pixel(x, y, ncol) : 0;
            const int at = x * s->bits;
            if (s->bits == 8) {
                p[x] = (Uint8)v;
            } else if (s->bits < 8) {
                p[at >> 3] |= (Uint8)(v << (8 - s->bits - (at & 7)));
            }
        }
    }
    return (int)(off + row * s->h);
}

/* the surface is the shape's picture and palette */
static int matches(SDL_Surface *g, const struct shape *s)
{
    const int ncol = s->used ? (int)s->used : 1 << s->bits;
    const SDL_Palette *pal;
    int x, y, i, ok = 1;
    if (!g || g->w != s->w || g->h != s->h || g->pitch != ((s->w + 3) & ~3) ||
        g->format->format != SDL_PIXELFORMAT_INDEX8) {
        return 0;
    }
    pal = g->format->palette;
    if (!pal || pal->ncolors != ncol) {
        return 0;
    }
    for (i = 0; i < ncol; i++) {
        const SDL_Color c = entry(i);
        ok &= memcmp(&pal->colors[i], &c, sizeof(c)) == 0;
    }
    for (y = 0; y < s->h; y++) {
        for (x = 0; x < s->w; x++) {
            ok &= ((Uint8 *)g->pixels)[y * g->pitch + x] == pixel(x, y, ncol);
        }
    }
    return ok;
}

static Uint8 buf[64 * 1024];

static void shapes(void)
{
    static const int bits[3] = { 1, 4, 8 };
    static const int hsizes[3] = { 40, 108, 124 };
    struct shape s;
    SDL_Surface *g;
    int b, w, h, n, wrong = 0, tried = 0;
    memset(&s, 0, sizeof(s));
    for (b = 0; b < 3; b++) {
        for (w = 1; w <= 35; w++) {
            for (h = 1; h <= 5; h += 2) {
                s.w = w;
                s.h = h;
                s.bits = bits[b];
                s.topdown = (w + h) & 1;
                s.hsize = hsizes[(w + b) % 3];
                n = make_bmp(buf, &s);
                g = SDL_LoadBMP_RW(SDL_RWFromConstMem(buf, n), 1);
                wrong += !matches(g, &s);
                SDL_FreeSurface(g);
                /* and the other way up */
                s.topdown = !s.topdown;
                n = make_bmp(buf, &s);
                g = SDL_LoadBMP_RW(SDL_RWFromConstMem(buf, n), 1);
                wrong += !matches(g, &s);
                SDL_FreeSurface(g);
                tried += 2;
            }
        }
    }
    CHECK(wrong == 0);
    if (wrong) {
        printf("test_bmp: %d of %d shapes loaded wrongly\n", wrong, tried);
    }

    /* a colour count: fewer than the bits give, and past 256 (them all,
     * as SDL2) */
    s.w = 13;
    s.h = 6;
    s.bits = 4;
    s.topdown = 0;
    s.hsize = 40;
    s.used = 5;
    n = make_bmp(buf, &s);
    g = SDL_LoadBMP_RW(SDL_RWFromConstMem(buf, n), 1);
    CHECK(matches(g, &s) && g->format->palette->ncolors == 5);
    SDL_FreeSurface(g);
    s.bits = 8;
    s.used = 300;
    n = make_bmp(buf, &s);
    s.used = 0;                 /* read as all 256 */
    g = SDL_LoadBMP_RW(SDL_RWFromConstMem(buf, n), 1);
    CHECK(g && g->format->palette->ncolors == 256);
    SDL_FreeSurface(g);

    /* a gap before the pixels: their offset is the file's */
    s.used = 0;
    s.gap = 6;
    n = make_bmp(buf, &s);
    g = SDL_LoadBMP_RW(SDL_RWFromConstMem(buf, n), 1);
    CHECK(matches(g, &s));
    SDL_FreeSurface(g);
    s.gap = 0;

    /* a file part-way into its stream, the stream kept open: offsets
     * are from where the file starts */
    {
        SDL_RWops *rw;
        memset(buf, 0x3C, 7);
        n = make_bmp(buf + 7, &s);
        rw = SDL_RWFromConstMem(buf, n + 7 + 5);
        SDL_RWseek(rw, 7, RW_SEEK_SET);
        g = SDL_LoadBMP_RW(rw, 0);
        CHECK(matches(g, &s));
        SDL_FreeSurface(g);
        CHECK(SDL_RWclose(rw) == 0);
    }

    /* a loaded surface is an ordinary one: keyed and blitted */
    {
        SDL_Surface *d = SDL_CreateRGBSurface(0, 13, 6, 8, 0, 0, 0, 0);
        n = make_bmp(buf, &s);
        g = SDL_LoadBMP_RW(SDL_RWFromConstMem(buf, n), 1);
        SDL_FillRect(d, NULL, 99);
        CHECK(SDL_SetColorKey(g, SDL_TRUE, 0) == 0);
        CHECK(SDL_BlitSurface(g, NULL, d, NULL) == 0);
        CHECK(((Uint8 *)d->pixels)[0] == 99 &&   /* pixel(0, 0) is 0 */
              ((Uint8 *)d->pixels)[1] == pixel(1, 0, 256));
        SDL_FreeSurface(d);
        SDL_FreeSurface(g);
    }
}

/* refused, with SDL2's words, and the stream closed */
static void refusals(void)
{
    /* each file a good 9x4 one with its bits and compression set, then
     * one field patched (`size` 2 or 4 bytes at `at`, 0 for none) */
    static const struct {
        Uint8 bits, comp, at, size;
        long val;
        const char *words;
    } cases[] = {
        { 8, 1, 0, 0, 0, "Compressed BMP files not supported" }, /* RLE8 */
        { 4, 2, 0, 0, 0, "Compressed BMP files not supported" }, /* RLE4 */
        { 24, 0, 0, 0, 0, "24-bpp BMP images are not supported" },
        { 16, 3, 0, 0, 0, "16-bpp BMP images are not supported" },
        { 32, 3, 0, 0, 0, "32-bpp BMP images are not supported" },
        { 2, 0, 0, 0, 0, "2-bpp BMP images are not supported" },
        { 8, 0, 0, 2, 'B' | 'A' << 8, "File is not a Windows BMP file" },
        /* OS/2's 12-byte header: 0x0, as SDL2 says of a header it does
         * not know */
        { 8, 0, 14, 4, 12, "BMP file with bad dimensions (0x0)" },
        { 8, 0, 18, 4, 0, "BMP file with bad dimensions (0x4)" },
        { 8, 0, 22, 4, 0, "BMP file with bad dimensions (9x0)" },
        { 8, 0, 18, 4, 40000, "bad dimensions" },
        { 8, 0, 22, 4, -40000, "bad dimensions" },
        { 8, 0, 22, 4, (long)0x80000000UL, "bad dimensions" },
    };
    struct shape s;
    unsigned i;
    int n;

    memset(&s, 0, sizeof(s));
    s.w = 9;
    s.h = 4;
    s.hsize = 40;
    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        s.bits = cases[i].bits;
        s.comp = cases[i].comp;
        n = make_bmp(buf, &s);
        if (cases[i].size == 2) {
            le16(buf + cases[i].at, (int)cases[i].val);
        } else if (cases[i].size == 4) {
            le32(buf + cases[i].at, cases[i].val);
        }
        if (SDL_LoadBMP_RW(SDL_RWFromConstMem(buf, n), 1) != NULL
            || strstr(SDL_GetError(), cases[i].words) == NULL) {
            printf("FAIL refusal %u: wanted \"%s\", got \"%s\"\n", i,
                   cases[i].words, SDL_GetError());
            failures++;
        }
    }
}

/* every prefix of a file is refused as a short read, the surface and
 * the stream freed (ASan's leak check on Linux) */
static void truncated(void)
{
    static const int bits[3] = { 1, 4, 8 };
    struct shape s;
    SDL_Surface *g;
    int b, n, len, wrong = 0;
    memset(&s, 0, sizeof(s));
    s.w = 11;
    s.h = 3;
    s.hsize = 40;
    for (b = 0; b < 3; b++) {
        s.bits = bits[b];
        s.topdown = b & 1;
        n = make_bmp(buf, &s);
        for (len = 0; len < n; len++) {
            SDL_ClearError();
            wrong += SDL_LoadBMP_RW(SDL_RWFromConstMem(buf, len), 1) != NULL ||
                     strcmp(SDL_GetError(), "Error reading from datastream");
        }
        /* and the whole of it loads */
        g = SDL_LoadBMP_RW(SDL_RWFromConstMem(buf, n), 1);
        CHECK(matches(g, &s));
        SDL_FreeSurface(g);
    }
    CHECK(wrong == 0);
    /* no stream at all: the stream's own error stands */
    SDL_SetError("before");
    CHECK(SDL_LoadBMP_RW(NULL, 1) == NULL &&
          strcmp(SDL_GetError(), "before") == 0);
}

/* SDL_LoadBMP itself, through a file */
static void files(void)
{
    struct shape s;
    SDL_Surface *g;
    FILE *fp;
    int n;
    memset(&s, 0, sizeof(s));
    s.w = 21;
    s.h = 9;
    s.bits = 4;
    s.hsize = 124;
    n = make_bmp(buf, &s);
    fp = fopen("test_bmp.tmp", "wb");
    fwrite(buf, 1, (size_t)n, fp);
    fclose(fp);
    g = SDL_LoadBMP("test_bmp.tmp");
    CHECK(matches(g, &s));
    SDL_FreeSurface(g);
    fp = fopen("test_bmp.tmp", "wb");
    fwrite(buf, 1, (size_t)n - 1, fp);
    fclose(fp);
    CHECK(SDL_LoadBMP("test_bmp.tmp") == NULL &&
          strstr(SDL_GetError(), "Error reading"));
    remove("test_bmp.tmp");
    CHECK(SDL_LoadBMP("test_bmp.tmp") == NULL &&
          strstr(SDL_GetError(), "Couldn't open"));
}

/* bmp8conv.py's files against what this loads */
static void round_trip(void)
{
    char line[512], bmp[256], exp[256];
    FILE *list, *fp;
    int files = 0;
    const int r = system("python3 bmp8conv.py");
    if (r != 0) {
        printf(WEXITSTATUS(r) == 2 ? "test_bmp: round trip skipped (no Pillow)\n"
                                   : "test_bmp: bmp8conv.py failed\n");
        CHECK(WEXITSTATUS(r) == 2);
        return;
    }
    list = fopen("test_bmp.dir/LIST", "r");
    CHECK(list != NULL);
    while (list && fgets(line, sizeof(line), list) &&
           sscanf(line, "%255s %255s", bmp, exp) == 2) {
        static Uint8 e[1 << 20];
        SDL_Surface *g = SDL_LoadBMP(bmp);
        size_t got;
        int w, h, n, x, y, i, ok;
        fp = fopen(exp, "rb");
        got = fp ? fread(e, 1, sizeof(e), fp) : 0;
        if (fp) {
            fclose(fp);
        }
        w = e[0] | e[1] << 8;
        h = e[2] | e[3] << 8;
        n = e[4] | e[5] << 8;
        ok = g && got == (size_t)(6 + n * 3 + w * h) && g->w == w &&
             g->h == h && g->format->palette->ncolors == n;
        for (i = 0; ok && i < n; i++) {
            const SDL_Color c = g->format->palette->colors[i];
            ok = c.r == e[6 + i * 3] && c.g == e[7 + i * 3] &&
                 c.b == e[8 + i * 3] && c.a == 255;
        }
        for (y = 0; ok && y < h; y++) {
            for (x = 0; ok && x < w; x++) {
                ok = ((Uint8 *)g->pixels)[y * g->pitch + x] ==
                     e[6 + n * 3 + y * w + x];
            }
        }
        if (!ok) {
            printf("test_bmp: %s does not load as Pillow reads it\n", bmp);
        }
        CHECK(ok);
        SDL_FreeSurface(g);
        files++;
    }
    if (list) {
        fclose(list);
    }
    CHECK(files == 9);
}

int main(void)
{
    shapes();
    refusals();
    truncated();
    files();
    round_trip();
    if (failures) {
        printf("test_bmp: %d failures\n", failures);
        return 1;
    }
    printf("test_bmp: ok\n");
    return 0;
}
