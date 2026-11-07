/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The add-on's surfaces, streams and log on the host. Blits are
 * checked against a pixel-by-pixel reference over random rectangles,
 * clipped every way, keyed or not, onto another surface and onto
 * themselves; a texture made from a surface against the page it shows
 * on; streams in memory and in a file, with SDL2's whole-object reads
 * and clamped seeks; the log's file and SDL2's default priorities.
 */

#include "SDL.h"
#include "SDL_stdl.h"

#include <stdio.h>
#include <string.h>

#include "fake_stdl.h"

static int failures;

#define CHECK(c) do { \
        if (!(c)) { \
            if (failures < 12) { \
                printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); \
            } \
            failures++; \
        } \
    } while (0)

static unsigned long seed = 12345;

static int rnd(int n)
{
    seed = seed * 1103515245UL + 12345UL;
    return (int)((seed >> 16) % (unsigned long)n);
}

static void fill_random(SDL_Surface *s)
{
    int x, y;
    for (y = 0; y < s->h; y++) {
        for (x = 0; x < s->w; x++) {
            ((Uint8 *)s->pixels)[y * s->pitch + x] = (Uint8)rnd(256);
        }
    }
}

static Uint8 px(const SDL_Surface *s, int x, int y)
{
    return ((const Uint8 *)s->pixels)[y * s->pitch + x];
}

static void creation(void)
{
    static Uint8 mine[20 * 6];
    SDL_Surface *s = SDL_CreateRGBSurface(0, 13, 7, 8, 0, 0, 0, 0);
    SDL_Surface *f;
    SDL_Palette *pal;
    CHECK(s && s->w == 13 && s->h == 7 && s->pitch == 16);
    CHECK(s->format->format == SDL_PIXELFORMAT_INDEX8 &&
          s->format->BitsPerPixel == 8 && s->format->BytesPerPixel == 1);
    CHECK(s->format->palette && s->format->palette->ncolors == 256 &&
          s->format->palette->colors[9].r == 255);
    CHECK(s->clip_rect.x == 0 && s->clip_rect.w == 13 && s->clip_rect.h == 7);
    CHECK(px(s, 12, 6) == 0);
    CHECK(SDL_CreateRGBSurface(0, 8, 8, 32, 0xFF0000, 0xFF00, 0xFF, 0) ==
          NULL && strstr(SDL_GetError(), "8-bit"));
    CHECK(SDL_CreateRGBSurfaceWithFormat(0, 8, 8, 32, 0x16362004u) == NULL);
    CHECK(SDL_MUSTLOCK(s) == 0 && SDL_LockSurface(s) == 0 && s->locked);
    SDL_UnlockSurface(s);
    CHECK(!s->locked);

    /* a palette shared by two surfaces, freed with the last */
    pal = SDL_AllocPalette(256);
    f = SDL_CreateRGBSurfaceFrom(mine, 20, 6, 8, 20, 0, 0, 0, 0);
    CHECK(f && f->pixels == mine && (f->flags & SDL_PREALLOC));
    CHECK(SDL_SetSurfacePalette(s, pal) == 0 && s->format->palette == pal);
    CHECK(SDL_SetSurfacePalette(f, pal) == 0 && pal->refcount == 3);
    SDL_FreePalette(pal);
    SDL_FreeSurface(s);
    CHECK(pal->refcount == 1);
    /* not the program's pixels */
    SDL_FreeSurface(f);
    CHECK(SDL_CreateRGBSurfaceFrom(mine, 20, 6, 8, 10, 0, 0, 0, 0) == NULL);
}

static void fills(void)
{
    SDL_Surface *s = SDL_CreateRGBSurface(0, 30, 20, 8, 0, 0, 0, 0);
    SDL_Rect r, clip;
    int x, y, bad = 0;
    clip.x = 5;
    clip.y = 4;
    clip.w = 20;
    clip.h = 10;
    CHECK(SDL_SetClipRect(s, &clip) == SDL_TRUE);
    SDL_FillRect(s, NULL, 7);
    r.x = -3;
    r.y = 2;
    r.w = 10;
    r.h = 4;
    SDL_FillRect(s, &r, 0x109);         /* the index, a byte */
    for (y = 0; y < s->h; y++) {
        for (x = 0; x < s->w; x++) {
            const int in = x >= 5 && x < 25 && y >= 4 && y < 14;
            const int inr = in && x < 7 && y < 6;
            bad += px(s, x, y) != (inr ? 9 : in ? 7 : 0);
        }
    }
    CHECK(bad == 0);
    clip.x = 40;
    CHECK(SDL_SetClipRect(s, &clip) == SDL_FALSE);
    SDL_GetClipRect(s, &r);
    CHECK(r.w == 0 || r.h == 0);
    CHECK(SDL_SetClipRect(s, NULL) == SDL_TRUE);
    SDL_GetClipRect(s, &r);
    CHECK(r.x == 0 && r.y == 0 && r.w == 30 && r.h == 20);
    SDL_FreeSurface(s);
}

/* SDL_UpperBlit's result, pixel by pixel: each source pixel inside
 * both surfaces and the clip rectangle lands, but for the key */
static void reference(const Uint8 *src, int sw, int sh, int spitch,
                      const SDL_Rect *sr, Uint8 *dst, const SDL_Surface *d,
                      SDL_Rect dr, int keyed, Uint8 key, SDL_Rect *out)
{
    int i, j, x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1;
    const int w = sr ? sr->w : sw, h = sr ? sr->h : sh;
    const int ox = sr ? sr->x : 0, oy = sr ? sr->y : 0;
    const SDL_Rect *c = &d->clip_rect;
    for (j = 0; j < h; j++) {
        for (i = 0; i < w; i++) {
            const int sx = ox + i, sy = oy + j;
            const int dx = dr.x + i, dy = dr.y + j;
            if (sx < 0 || sy < 0 || sx >= sw || sy >= sh || dx < c->x ||
                dy < c->y || dx >= c->x + c->w || dy >= c->y + c->h) {
                continue;
            }
            x0 = dx < x0 ? dx : x0;
            y0 = dy < y0 ? dy : y0;
            x1 = dx > x1 ? dx : x1;
            y1 = dy > y1 ? dy : y1;
            if (!keyed || src[sy * spitch + sx] != key) {
                dst[dy * d->pitch + dx] = src[sy * spitch + sx];
            }
        }
    }
    if (x1 < 0) {
        out->w = out->h = 0;
    } else {
        out->x = x0;
        out->y = y0;
        out->w = x1 - x0 + 1;
        out->h = y1 - y0 + 1;
    }
}

static void blits(void)
{
    SDL_Surface *a = SDL_CreateRGBSurface(0, 37, 23, 8, 0, 0, 0, 0);
    SDL_Surface *b = SDL_CreateRGBSurface(0, 41, 29, 8, 0, 0, 0, 0);
    static Uint8 want[64 * 64], copy[64 * 64];
    int n, bad = 0, rects = 0, empty = 0;
    Uint32 key = 0;
    CHECK(SDL_GetColorKey(a, &key) < 0);
    CHECK(SDL_SetColorKey(a, SDL_TRUE, 0x33) == 0);
    CHECK(SDL_GetColorKey(a, &key) == 0 && key == 0x33);
    SDL_SetColorKey(a, SDL_FALSE, 0);
    for (n = 0; n < 4000; n++) {
        SDL_Rect sr, dr, out, clip;
        const int self = n % 5 == 4;
        SDL_Surface *d = self ? a : b;
        /* keyed onto itself overlapping is undefined, as in SDL2 */
        const int keyed = !self && n % 3 == 1;
        const int nosrc = n % 7 == 3;
        fill_random(a);
        fill_random(b);
        clip.x = rnd(12) - 2;
        clip.y = rnd(10) - 2;
        clip.w = rnd(d->w + 4);
        clip.h = rnd(d->h + 4);
        SDL_SetClipRect(d, n % 4 ? &clip : NULL);
        sr.x = rnd(50) - 10;
        sr.y = rnd(34) - 8;
        sr.w = rnd(45);
        sr.h = rnd(30);
        dr.x = rnd(56) - 12;
        dr.y = rnd(40) - 10;
        dr.w = dr.h = 99;               /* ignored on the way in */
        SDL_SetColorKey(a, keyed, 0x40);
        if (keyed) {
            /* the key, and its neighbour, where they will be seen */
            int k;
            for (k = 0; k < 60; k++) {
                ((Uint8 *)a->pixels)[rnd(a->h) * a->pitch + rnd(a->w)] =
                    (Uint8)(rnd(2) + 0x40);
            }
        }
        memcpy(copy, a->pixels, (size_t)a->pitch * a->h);
        memcpy(want, d->pixels, (size_t)d->pitch * d->h);
        reference(copy, a->w, a->h, a->pitch, nosrc ? NULL : &sr, want, d, dr,
                  keyed, 0x40, &out);
        CHECK(SDL_BlitSurface(a, nosrc ? NULL : &sr, d, &dr) == 0);
        bad += memcmp(want, d->pixels, (size_t)d->pitch * d->h) != 0;
        if (out.w) {
            rects++;
            bad += dr.x != out.x || dr.y != out.y || dr.w != out.w ||
                   dr.h != out.h;
        } else {
            empty++;
            bad += dr.w != 0 || dr.h != 0;
        }
    }
    CHECK(bad == 0);
    /* both cases were tried */
    CHECK(rects > 1000 && empty > 200);
    SDL_SetClipRect(b, NULL);
    CHECK(SDL_BlitSurface(a, NULL, b, NULL) == 0);
    CHECK(SDL_BlitSurface(NULL, NULL, b, NULL) < 0);
    SDL_FreeSurface(a);
    SDL_FreeSurface(b);
}

static void texture(void)
{
    SDL_Window *w;
    SDL_Renderer *r;
    SDL_Surface *s = SDL_CreateRGBSurface(0, 48, 10, 8, 0, 0, 0, 0);
    SDL_Texture *t;
    SDL_Rect to;
    int x, y, bad = 0, wq, hq;
    fill_random(s);
    w = SDL_CreateWindow("t", 0, 0, 320, 240, 0);
    r = SDL_CreateRenderer(w, -1, 0);
    t = SDL_CreateTextureFromSurface(r, s);
    CHECK(t && SDL_QueryTexture(t, NULL, NULL, &wq, &hq) == 0 && wq == 48 &&
          hq == 10);
    to.x = 32;
    to.y = 7;
    to.w = 48;
    to.h = 10;
    SDL_RenderCopy(r, t, NULL, &to);
    SDL_RenderPresent(r);
    /* the page shown, read back from its 8 planes */
    for (y = 0; y < 10; y++) {
        const Uint8 *row = fake_page[fake_shown] + (7 + y) * fake_stride +
                           fake_margin;
        for (x = 0; x < 48; x++) {
            const int sx = 32 + x, g = sx >> 4, bit = 15 - (sx & 15);
            int p, v = 0;
            for (p = 0; p < 8; p++) {
                const Uint8 *wd = row + g * 16 + p * 2;
                v |= ((wd[0] << 8 | wd[1]) >> bit & 1) << p;
            }
            bad += v != px(s, x, y);
        }
    }
    CHECK(bad == 0);
    SDL_DestroyTexture(t);
    SDL_DestroyWindow(w);
    SDL_FreeSurface(s);
}

static void streams(void)
{
    Uint8 buf[10] = { 0x34, 0x12, 0x12, 0x34, 0x78, 0x56, 0x34, 0x12,
                      0xAA, 0xBB };
    Uint8 out[16], four[4];
    SDL_RWops *rw = SDL_RWFromMem(buf, 10);
    FILE *fp;
    CHECK(rw && rw->type == SDL_RWOPS_MEMORY && SDL_RWsize(rw) == 10);
    CHECK(SDL_ReadLE16(rw) == 0x1234 && SDL_ReadBE16(rw) == 0x1234);
    CHECK(SDL_ReadLE32(rw) == 0x12345678 && SDL_RWtell(rw) == 8);
    /* whole objects only: one 4-byte object does not fit in 2 */
    CHECK(SDL_RWread(rw, out, 4, 1) == 0 && SDL_RWtell(rw) == 8);
    CHECK(SDL_RWread(rw, out, 1, 5) == 2 && out[1] == 0xBB);
    CHECK(SDL_ReadU8(rw) == 0);                 /* past the end */
    CHECK(SDL_RWseek(rw, 2, RW_SEEK_SET) == 2);
    CHECK(SDL_ReadBE32(rw) == 0x12347856);
    CHECK(SDL_RWseek(rw, -20, RW_SEEK_CUR) == 0);
    CHECK(SDL_RWseek(rw, 5, RW_SEEK_END) == 10);
    CHECK(SDL_RWseek(rw, -3, RW_SEEK_END) == 7);
    CHECK(SDL_RWseek(rw, 0, 9) < 0);
    CHECK(SDL_RWseek(rw, 0, RW_SEEK_SET) == 0);
    CHECK(SDL_WriteBE32(rw, 0x01020304) == 1 && buf[0] == 1 && buf[3] == 4);
    CHECK(SDL_WriteLE16(rw, 0x0506) == 1 && buf[4] == 6 && buf[5] == 5);
    CHECK(SDL_WriteU8(rw, 9) == 1 && buf[6] == 9);
    CHECK(SDL_RWwrite(rw, "abcd", 2, 2) == 1 && buf[7] == 'a' &&
          buf[8] == 'b' && buf[9] == 0xBB);
    CHECK(SDL_RWclose(rw) == 0);

    rw = SDL_RWFromConstMem(buf, 10);
    CHECK(rw && rw->type == SDL_RWOPS_MEMORY_RO);
    CHECK(SDL_WriteU8(rw, 0) == 0 && buf[0] == 1);
    CHECK(SDL_ReadBE16(rw) == 0x0102);
    SDL_RWclose(rw);
    CHECK(SDL_RWFromMem(NULL, 4) == NULL);

    /* a file, written and read back */
    rw = SDL_RWFromFile("test_rw.tmp", "wb");
    CHECK(rw && rw->type == SDL_RWOPS_STDFILE);
    CHECK(SDL_WriteLE32(rw, 0xCAFEF00D) == 1 && SDL_WriteBE16(rw, 0x0102));
    CHECK(SDL_RWclose(rw) == 0);
    rw = SDL_RWFromFile("test_rw.tmp", "rb");
    CHECK(rw && SDL_RWsize(rw) == 6 && SDL_RWtell(rw) == 0);
    CHECK(SDL_RWread(rw, four, 1, 4) == 4 && four[0] == 0x0D &&
          four[3] == 0xCA);
    CHECK(SDL_ReadBE16(rw) == 0x0102);
    CHECK(SDL_RWseek(rw, -2, RW_SEEK_END) == 4 && SDL_ReadLE16(rw) == 0x0201);
    SDL_RWclose(rw);
    remove("test_rw.tmp");
    CHECK(SDL_RWFromFile("test_rw.tmp", "rb") == NULL &&
          strstr(SDL_GetError(), "Couldn't open"));
    fp = fopen("test_rw.tmp", "wb");
    rw = SDL_RWFromFP(fp, SDL_FALSE);
    SDL_RWclose(rw);
    CHECK(fputc('x', fp) == 'x');                 /* still open */
    fclose(fp);
    remove("test_rw.tmp");
}

static int lines(const char *name, char text[][64], int max)
{
    FILE *fp = fopen(name, "r");
    int n = 0;
    if (!fp) {
        return -1;
    }
    while (n < max && fgets(text[n], 64, fp)) {
        text[n][strcspn(text[n], "\n")] = 0;
        n++;
    }
    fclose(fp);
    return n;
}

static void logging(void)
{
    char text[8][64];
    remove("test_log.tmp");
    /* no hint: nowhere, and the file is not made */
    SDL_Log("lost");
    CHECK(lines("test_log.tmp", text, 8) < 0);
    SDL_SetHint(STDL_SDL2_HINT_LOGFILE, "test_log.tmp");
    SDL_Log("hello %d", 5);
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "not at DEBUG");
    SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "video wants CRITICAL");
    SDL_LogCritical(SDL_LOG_CATEGORY_VIDEO, "critical %s", "video");
    SDL_LogWarn(SDL_LOG_CATEGORY_ASSERT, "assert from WARN");
    SDL_LogVerbose(SDL_LOG_CATEGORY_TEST, "test from VERBOSE");
    SDL_LogSetPriority(SDL_LOG_CATEGORY_VIDEO, SDL_LOG_PRIORITY_DEBUG);
    SDL_LogDebug(SDL_LOG_CATEGORY_VIDEO, "video at DEBUG");
    SDL_LogInfo(SDL_LOG_CATEGORY_CUSTOM + 6, "custom wants CRITICAL");
    SDL_LogSetAllPriority(SDL_LOG_PRIORITY_ERROR);
    SDL_Log("info, now under ERROR");
    SDL_LogMessage(SDL_LOG_CATEGORY_INPUT, SDL_LOG_PRIORITY_ERROR, "input");
    CHECK(lines("test_log.tmp", text, 8) == 6);
    CHECK(strcmp(text[0], "INFO: hello 5") == 0);
    CHECK(strcmp(text[1], "CRITICAL: critical video") == 0);
    CHECK(strcmp(text[2], "WARN: assert from WARN") == 0);
    CHECK(strcmp(text[3], "VERBOSE: test from VERBOSE") == 0);
    CHECK(strcmp(text[4], "DEBUG: video at DEBUG") == 0);
    CHECK(strcmp(text[5], "ERROR: input") == 0);
    remove("test_log.tmp");
}

int main(void)
{
    fake_layout = FAKE_VGA;
    fake_mach.mch_cookie = 0x00030000UL;
    CHECK(SDL_Init(SDL_INIT_VIDEO) == 0);
    creation();
    fills();
    blits();
    texture();
    streams();
    logging();
    CHECK(strcmp(SDL_GetPlatform(), "Atari") == 0);
    CHECK(SDL_GetPerformanceFrequency() == 38400);
    {
        const Uint64 a = SDL_GetPerformanceCounter();
        CHECK(SDL_GetPerformanceCounter() > a);
    }
    SDL_Quit();
    if (failures) {
        printf("test_surface: %d failures\n", failures);
        return 1;
    }
    printf("test_surface: ok\n");
    return 0;
}
