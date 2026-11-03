/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/*
 * 8-plane surfaces (STDL_RESPONSIVE_COLOUR) against their reference.
 *
 * The reference (p8ref.c) is the planar routines run twice, on the
 * colours' low and high nibbles, so it is as right as they are and as
 * the nibble split; planes8.c is code of its own throughout. The same
 * seeded run of calls - fills, lines, points, blits plain, shifted and
 * keyed, sprites pre-shifted and not, text on and off its grid, clips,
 * converters - goes through each, and every display page after every
 * Flip must be byte for byte the same, on a TT's 480 doubled lines and
 * a Falcon's 240 and 200, and tall, 240 rows on each (a Falcon's RGB
 * in overscan). Off-screen surfaces and their masks are
 * compared pixel by pixel at the end.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/responsive.h"

#ifdef __m68k__
/* the machine's own screen; these only name it in the messages */
static int stdl_host_resp_falcon, stdl_host_resp_vga;
#define SET_MACHINE(f, v) ((void)(f), (void)(v))
#else
extern int stdl_host_resp_falcon, stdl_host_resp_vga;
#define SET_MACHINE(f, v) (stdl_host_resp_falcon = (f), \
                           stdl_host_resp_vga = (v))
#endif
int p8ref_use(unsigned what);

/* the options and the height a run asks for; over 200 rows, g_tall:
 * the tall screen, or the display's own height, which a TT and a
 * Falcon show alike */
static unsigned g_what = STDL_RESPONSIVE_COLOUR;
static unsigned g_tall = STDL_RESPONSIVE_TALL;
static int g_h = 200;
/* which of the later calls a run makes (TEST_OPS, a bitmask, for
 * finding which one two runs part on) */
static unsigned g_ops = ~0u;
#define OP(b) (g_ops & (1u << (b)))

static int use_lib(void)
{
    return STDL_UseResponsive(g_what);
}

static int use_ref(void)
{
    return p8ref_use(g_what);
}

static int failures;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        failures++; \
        fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
        fprintf(stderr, __VA_ARGS__); \
        fprintf(stderr, "\n"); \
    } \
} while (0)

static unsigned rng_state;
static unsigned rnd(void)
{
    rng_state = rng_state * 1103515245u + 12345u;
    return (rng_state >> 16) & 0x7FFF;
}
static int rr(int lo, int hi)            /* lo..hi inclusive */
{
    return lo + (int)(rnd() % (unsigned)(hi - lo + 1));
}

#define FRAMES  48
#define NSPR    10

/*
 * Built for the machine as well (RESPCHK.TOS, the header of main says
 * how): there it runs on that machine's own screen, and keeps a hash
 * of each page where the host keeps the page, as 48 frames of two
 * pages twice over would not fit.
 */
static uint32_t fnv(const uint8_t *p, uint32_t n)
{
    uint32_t h = 2166136261u;

    while (n-- > 0) {
        h = (h ^ *p++) * 16777619u;
    }
    return h;
}

typedef struct {
    uint8_t *pages[FRAMES][2];
    uint32_t hash[FRAMES][2];
    uint8_t *surf[4];           /* off-screen surfaces, a byte a pixel */
    uint8_t *mask[4];           /* their masks, a byte a pixel       */
    uint32_t page_bytes;
} run_t;

static uint8_t font8[96 * 8], font12[96 * 2 * 10];
static STDL_Font f8 = { 8, 8, 32, 127, 1, font8 };
static STDL_Font f12 = { 12, 10, 32, 127, 2, font12 };

static int mask_bit(const STDL_Surface *s, int x, int y)
{
    const uint16_t *m;

    if (s->mask == NULL) {
        return 0;
    }
    m = (const uint16_t *)(s->mask + (size_t)y * s->maskstride);
    return (m[x >> 4] >> (15 - (x & 15))) & 1;
}

/* the first of a TT page's lines from..to-1, a row's first, that the
 * line under it does not repeat, or -1 */
static int undoubled(const uint8_t *page, int line, int from, int to)
{
    int l;

    for (l = from; l < to; l += 2) {
        if (memcmp(page + (size_t)l * line, page + (size_t)(l + 1) * line,
                   (size_t)line) != 0) {
            return l;
        }
    }
    return -1;
}

static void snap(const STDL_Surface *s, uint8_t **pix, uint8_t **msk)
{
    int x, y;

    *pix = malloc((size_t)s->w * s->h);
    *msk = malloc((size_t)s->w * s->h);
    for (y = 0; y < s->h; y++) {
        for (x = 0; x < s->w; x++) {
            (*pix)[y * s->w + x] = STDL_GetPixel(s, x, y);
            (*msk)[y * s->w + x] = (uint8_t)mask_bit(s, x, y);
        }
    }
}

static STDL_Rect rect(int x, int y, int w, int h)
{
    STDL_Rect r;

    r.x = (int16_t)x; r.y = (int16_t)y;
    r.w = (uint16_t)w; r.h = (uint16_t)h;
    return r;
}

/* the first byte two pages differ at, as a pixel */
static void where(const uint8_t *a, const uint8_t *b, uint32_t n)
{
    uint32_t i;

    for (i = 0; i < n && a[i] == b[i]; i++) {
    }
    if (i < n) {
        printf("      first difference: line %u, x %u, plane %u\n",
               (unsigned)(i / (unsigned)stdl_rs.line),
               (unsigned)((i % (unsigned)stdl_rs.line) / 16 * 16),
               (unsigned)((i % 16) / 2));
    }
}

static void one_run(int (*use)(void), unsigned seed, run_t *out)
{
    STDL_Surface *scr, *bg, *keyed, *dmask, *glyphs;
    STDL_Sprite *spr[NSPR];
    STDL_Point pts[300];
    STDL_Span spans[24];
    uint8_t c0, c1, c2, c3;
    int a0, a1, a2;
    STDL_SpriteAt at[6];
    uint8_t chunky[23 * 17], cmap[16];
    STDL_Surface *dup = NULL, *made = NULL;
    uint8_t cols[300], idx[37 * 21], bits[5 * 9], map[16];
    char text[24];
    int i, f, n, k;

    rng_state = seed;
    CHECK(use() == 0, "use");
    scr = stdl_resp_video->set_mode(320, g_h, 8, STDL_DOUBLEBUF);
    CHECK(scr != NULL && scr == stdl_resp_screen, "set_mode");
    CHECK(scr == NULL || scr->h == (g_h > 200 ? 240 : 200), "height");
    if (scr == NULL) {
        return;
    }
    out->page_bytes = stdl_rs.page_bytes;
    CHECK(scr->format->BitsPerPixel == 8, "8 bits");

    /* the background: noise and bands, through the calls themselves */
    bg = STDL_CreateSurface(320, 200);
    CHECK(bg != NULL && STDL_IS_RESP(bg), "8-plane background");
    for (i = 0; i < 400; i++) {
        STDL_Rect r = rect(rr(-20, 330), rr(-20, 210), rr(1, 60),
                           rr(1, 40));
        STDL_FillRect(bg, &r, (uint8_t)rnd());
    }
    for (i = 0; i < 3000; i++) {
        STDL_PutPixel(bg, rr(-5, 324), rr(-5, 204), (uint8_t)rnd());
    }

    /* sprites: strips of frames, keyed on 0 or another colour */
    for (k = 0; k < NSPR; k++) {
        static const int fws[NSPR] = { 5, 8, 16, 17, 23, 32, 40, 9, 31, 1 };
        const int fw = fws[k], nf = rr(1, 3), h = rr(1, 20);
        const uint8_t key = (k & 1) ? 0 : (uint8_t)rr(1, 255);
        STDL_Surface *s = STDL_CreateSurface(fw * nf, h);
        int x, y;

        CHECK(s != NULL, "sprite surface");
        for (y = 0; y < h; y++) {
            for (x = 0; x < fw * nf; x++) {
                STDL_PutPixel(s, x, y,
                              (rnd() & 3) == 0 ? key : (uint8_t)rnd());
            }
        }
        CHECK(STDL_SetColourKey(s, 1, key) == 0, "key");
        spr[k] = STDL_SpriteFromSurface(s, fw, (k % 3) ? STDL_PRESHIFT : 0);
        CHECK(spr[k] != NULL && STDL_IS_RESP(spr[k]), "8-plane sprite");
        STDL_FreeSurface(s);
    }

    /* a keyed converter surface, and a 1bpp one remapped */
    for (i = 0; i < (int)sizeof(idx); i++) {
        idx[i] = (rnd() % 5) == 0 ? 77 : (uint8_t)rnd();
    }
    keyed = STDL_SurfaceFromIndexed8(idx, 37, 21, 37, 77);
    CHECK(keyed != NULL && STDL_IS_RESP(keyed) && keyed->mask != NULL,
          "keyed converter");
    for (i = 0; i < (int)sizeof(bits); i++) {
        bits[i] = (uint8_t)rnd();
    }
    glyphs = STDL_SurfaceFrom1bpp(bits, 35, 9, 3, 0);
    for (i = 0; i < 16; i++) {
        map[i] = (uint8_t)rnd();
    }
    STDL_RemapSurface(glyphs, map);
    for (i = 0; i < 9; i++) {
        STDL_PutPixel(glyphs, rr(0, 34), rr(0, 8), (uint8_t)rr(0, 15));
    }
    STDL_RemapSurface(glyphs, map);

    /* a masked destination: drawn on, then used as a keyed source */
    dmask = STDL_SurfaceFromIndexed8(idx, 37, 21, 37, 77);
    STDL_FillRect(dmask, NULL, 0);
    {
        STDL_Rect r = rect(3, 2, 11, 7);
        STDL_FillRect(dmask, &r, 200);
    }
    STDL_PutPixel(dmask, 30, 15, 9);
    STDL_HLine(dmask, 2, 33, 18, 140);
    STDL_VLine(dmask, 20, 0, 20, 141);
    STDL_BlitSurface(keyed, NULL, dmask, NULL);
    {
        /* holes, as on 4 planes, from every call that SETs a colour */
        static const STDL_Point tri[3] = { { 2, 10 }, { 16, 12 }, { 6, 20 } };
        static const STDL_Point pp[3] = { { 30, 2 }, { 31, 3 }, { 32, 4 } };
        static const STDL_Span sp[2] = { { 1, 3, 12 }, { 25, 19, 8 } };
        static const uint8_t pc[3] = { STDL_TRANSPARENT, 9, STDL_TRANSPARENT };
        STDL_Rect r = rect(20, 1, 9, 5);

        STDL_FillRect(dmask, &r, STDL_TRANSPARENT);
        STDL_PutPixel(dmask, 10, 4, STDL_TRANSPARENT);
        STDL_HLine(dmask, 0, 36, 7, STDL_TRANSPARENT);
        STDL_VLine(dmask, 15, 0, 20, STDL_TRANSPARENT);
        STDL_HSpans(dmask, sp, 2, STDL_TRANSPARENT);
        STDL_VSpans(dmask, sp, 2, STDL_TRANSPARENT);
        STDL_Points(dmask, pp, 3, STDL_TRANSPARENT);
        STDL_PointsC(dmask, pp + 1, pc, 2);
        STDL_FillPolygon(dmask, tri, 3, STDL_TRANSPARENT, STDL_DRAW_SET);
        STDL_FillCircle(dmask, 28, 14, 5, STDL_TRANSPARENT);
    }

    STDL_BlitSurface(bg, NULL, scr, NULL);
    for (f = 0; f < FRAMES; f++) {
        if ((f % 7) == 3) {
            STDL_Rect c = rect(rr(-10, 100), rr(-10, 80), rr(50, 300),
                               rr(40, 200));
            STDL_SetClipRect(scr, &c);
        } else if ((f % 7) == 5) {
            STDL_SetClipRect(scr, NULL);
        }
        /* restores, aligned and not */
        for (i = 0; i < 12; i++) {
            const int w = rr(1, 90), h = rr(1, 50);
            STDL_Rect s = rect(rr(-10, 300), rr(-10, 190), w, h);
            STDL_Rect d = (i & 1) ? s : rect(rr(-30, 320), rr(-20, 200), 0, 0);
            STDL_BlitSurface(bg, &s, scr, &d);
        }
        if ((f % 11) == 0) {
            STDL_BlitSurface(bg, NULL, scr, NULL);
        }
        for (i = 0; i < 30; i++) {
            k = rr(0, NSPR - 1);
            STDL_BlitSprite(spr[k], rr(0, spr[k]->nframes - 1), scr,
                            rr(-45, 330), rr(-25, 205));
        }
        n = rr(1, 300);
        for (i = 0; i < n; i++) {
            pts[i].x = (int16_t)rr(-3, 322);
            pts[i].y = (int16_t)rr(-3, 202);
            cols[i] = (uint8_t)rnd();
        }
        STDL_Points(scr, pts, n / 2 + 1, (uint8_t)rnd());
        STDL_PointsC(scr, pts, cols, n);
        for (i = 0; i < 6; i++) {
            STDL_Rect r = rect(rr(-20, 330), rr(-20, 210), rr(0, 70),
                               rr(0, 40));
            STDL_FillRect(scr, &r, (uint8_t)rnd());
        }
        STDL_HLine(scr, rr(-20, 340), rr(-20, 340), rr(-2, 201),
                   (uint8_t)rnd());
        {
            /* the rows a tall screen adds */
            STDL_Rect r = rect(rr(-20, 330), rr(190, 250), rr(1, 70),
                               rr(1, 40));
            STDL_FillRect(scr, &r, (uint8_t)rnd());
            k = rr(0, NSPR - 1);
            STDL_BlitSprite(spr[k], 0, scr, rr(-45, 330), rr(200, 245));
        }
        STDL_VLine(scr, rr(-2, 321), rr(-20, 220), rr(-20, 220),
                   (uint8_t)rnd());
        STDL_Line(scr, rr(-20, 340), rr(-20, 220), rr(-20, 340),
                  rr(-20, 220), (uint8_t)rnd());
        STDL_PutPixel(scr, rr(-1, 320), rr(-1, 200), (uint8_t)rnd());
        for (i = 0; i < (int)sizeof(text) - 1; i++) {
            text[i] = (char)rr(28, 130);
        }
        text[rr(1, (int)sizeof(text) - 1)] = '\0';
        STDL_DrawText(scr, &f8, rr(-30, 20) * 8, rr(-6, 198), text,
                      (uint8_t)rnd());
        STDL_DrawText(scr, &f8, rr(-30, 330), rr(-6, 198), text,
                      (uint8_t)rnd());
        STDL_DrawText(scr, &f12, rr(-30, 330), rr(-9, 198), text,
                      (uint8_t)rnd());
        {
            STDL_Rect d = rect(rr(-40, 330), rr(-25, 205), 0, 0);
            STDL_Rect s = rect(rr(-5, 30), rr(-5, 18), rr(1, 40), rr(1, 25));
            STDL_Rect e = rect(rr(-40, 330), rr(-25, 205), 0, 0);

            STDL_BlitSurface(keyed, NULL, scr, &d);
            STDL_BlitSurface(dmask, &s, scr, &e);
            d = rect(rr(-40, 330), rr(-25, 205), 0, 0);
            STDL_BlitSurface(glyphs, NULL, scr, &d);
        }
        /* spans, the XOR calls, polygons, circles, single characters */
        n = rr(1, 24);
        for (i = 0; i < n; i++) {
            spans[i].x = (int16_t)rr(-30, 330);
            spans[i].y = (int16_t)rr(-10, 250);
            spans[i].len = (int16_t)rr(-2, 90);
        }
        c0 = (uint8_t)rnd(); c1 = (uint8_t)rnd();
        c2 = (uint8_t)rnd(); c3 = (uint8_t)rnd();
        if (OP(0)) STDL_HSpans(scr, spans, n, c0);
        if (OP(1)) STDL_VSpans(scr, spans, n / 2, c1);
        if (OP(2)) STDL_XorHSpans(scr, spans, n, c2);
        if (OP(3)) STDL_XorVSpans(scr, spans + n / 2, n - n / 2, c3);
        {
            STDL_Rect r = rect(rr(-20, 330), rr(-20, 250), rr(0, 80),
                               rr(0, 60));
            c0 = (uint8_t)rnd();
            if (OP(4)) STDL_XorRect(scr, &r, c0);
        }
        a0 = rr(-20, 340); a1 = rr(-20, 340); a2 = rr(-2, 245);
        c0 = (uint8_t)rnd();
        if (OP(5)) STDL_XorHLine(scr, a0, a1, a2, c0);
        a0 = rr(-2, 321); a1 = rr(-20, 260); a2 = rr(-20, 260);
        c0 = (uint8_t)rnd();
        if (OP(6)) STDL_XorVLine(scr, a0, a1, a2, c0);
        a0 = rr(-1, 320); a1 = rr(-1, 240); c0 = (uint8_t)rnd();
        if (OP(7)) STDL_XorPixel(scr, a0, a1, c0);
        for (i = 0; i < 4; i++) {          /* SET, XOR, OR and CLEAR */
            STDL_Point tri[5];
            int v;

            for (v = 0; v < 5; v++) {
                tri[v].x = (int16_t)rr(-40, 360);
                tri[v].y = (int16_t)rr(-40, 280);
            }
            c0 = (uint8_t)rnd();
            if (OP(8)) STDL_FillPolygon(scr, tri, 3 + (i & 1), c0, i);
        }
        a0 = rr(-10, 330); a1 = rr(-10, 250); a2 = rr(0, 40);
        c0 = (uint8_t)rnd();
        if (OP(9)) STDL_FillCircle(scr, a0, a1, a2, c0);
        a0 = rr(-10, 330); a1 = rr(-10, 250); a2 = rr(0, 40);
        c0 = (uint8_t)rnd();
        if (OP(10)) STDL_Circle(scr, a0, a1, a2, c0);
        a0 = rr(-10, 325); a1 = rr(-10, 245); a2 = rr(0, 140);
        c0 = (uint8_t)rnd();
        if (OP(11)) STDL_DrawChar(scr, &f8, a0, a1, a2, c0);
        a0 = rr(-10, 325); a1 = rr(-10, 245); a2 = rr(0, 140);
        c0 = (uint8_t)rnd();
        if (OP(11)) STDL_DrawChar(scr, &f12, a0, a1, a2, c0);
        /* chunky through a map, every walk, and onto a masked surface */
        for (i = 0; i < (int)sizeof(chunky); i++) {
            chunky[i] = (rnd() & 3) == 0 ? 0 : (uint8_t)rnd();
        }
        for (i = 0; i < 16; i++) {
            cmap[i] = (uint8_t)rnd();
        }
        k = rr(0, 3);
        a0 = rr(-30, 330); a1 = rr(-20, 250);
        if (OP(12))
        /* mirrored, the walk starts at the last pixel of a row: 22
         * on, or the last column's 16 x 23 on when column-major */
        STDL_BlitIndexed8(scr, chunky + ((k & 1) ? ((k & 2) ? 368 : 22) : 0),
                          23,
                          a0, a1, (k & 2) ? 17 : 23,
                          (k & 2) ? 23 : 17, cmap,
                          (unsigned)((k & 1) ? STDL_I8_XFLIP : 0)
                          | ((k & 2) ? STDL_I8_COLMAJOR : 0));
        a0 = rr(-5, 30); a1 = rr(-5, 18); a2 = rr(0, 2);
        if (OP(13))
        STDL_BlitIndexed8(dmask, chunky, 23, a0, a1, 23, 17,
                          cmap, (unsigned)(a2 == 0 ? STDL_I8_UNDER
                                           : STDL_I8_MARK));
        /* a batch of sprites */
        for (i = 0; i < 6; i++) {
            at[i].x = (int16_t)rr(-45, 330);
            at[i].y = (int16_t)rr(-25, 245);
            at[i].frame = 0;
        }
        k = rr(0, NSPR - 1);
        if (OP(14)) STDL_BlitSprites(spr[k], at, 6, scr);
        if (f == 5) {
            dup = STDL_DuplicateSurface(dmask);
            made = STDL_CreateSurface(29, 13);
            CHECK(made != NULL && STDL_CreateMask(made, 1) == 0, "mask");
            STDL_PutPixel(made, 3, 3, 77);
            STDL_FillRect(made, NULL, 0);
            {
                STDL_Rect r = rect(5, 2, 10, 6);
                STDL_FillRect(made, &r, 250);
            }
            /* a hole, and the key that keeps it */
            STDL_FillCircle(made, 14, 6, 4, STDL_TRANSPARENT);
            CHECK(STDL_SetColourKey(made, 1, STDL_TRANSPARENT) == 0, "key");
        }
        if (OP(16)) {
            uint16_t pw[4];
            uint8_t pb[4];

            for (i = 0; i < 4; i++) {
                pw[i] = (uint16_t)rnd();
                pb[i] = (uint8_t)rnd();
            }
            STDL_PutGroup(scr, rr(-20, 340), rr(-2, 241), pw, 0);
            STDL_PutGroup8(scr, rr(0, 340), rr(-2, 241), pb, 0);
            STDL_PutGroup(dmask, rr(0, 36), rr(0, 20), pw,
                          (uint16_t)rnd());
            STDL_PutGroup8(dmask, rr(0, 36), rr(0, 20), pb, (uint8_t)rnd());
        }
        if (OP(17)) {
            /* the blit flags onto the masked surface, keyed and plain
             * sources, shifted: the mask a priority plane */
            static const unsigned fl[3] = {
                STDL_BLIT_UNDER, STDL_BLIT_MARK,
                STDL_BLIT_UNDER | STDL_BLIT_MARK
            };
            STDL_Rect sr = rect(rr(-5, 30), rr(-5, 18), rr(1, 40), rr(1, 25));
            STDL_Rect d = rect(rr(-20, 36), rr(-10, 20), 0, 0);
            STDL_Rect e = rect(rr(-20, 36), rr(-10, 20), 0, 0);

            STDL_BlitSurfaceEx(keyed, &sr, dmask, &d, fl[rr(0, 2)]);
            STDL_BlitSurfaceEx(glyphs, NULL, dmask, &e, fl[rr(0, 2)]);
        }
        if (dup != NULL && OP(15)) {
            STDL_Rect d = rect(rr(-40, 330), rr(-25, 245), 0, 0);
            STDL_Rect e = rect(rr(-40, 330), rr(-25, 245), 0, 0);

            STDL_BlitSurface(dup, NULL, scr, &d);
            STDL_BlitSurface(made, NULL, scr, &e);
        }
        stdl_resp_video->flip();
        for (i = 0; i < 2; i++) {
            out->hash[f][i] = fnv(stdl_rs.page[i], stdl_rs.page_bytes);
#ifndef __m68k__
            out->pages[f][i] = malloc(stdl_rs.page_bytes);
            memcpy(out->pages[f][i], stdl_rs.page[i], stdl_rs.page_bytes);
#endif
        }
    }

    snap(bg, &out->surf[0], &out->mask[0]);
    snap(keyed, &out->surf[1], &out->mask[1]);
    snap(dmask, &out->surf[2], &out->mask[2]);
    snap(glyphs, &out->surf[3], &out->mask[3]);

    for (k = 0; k < NSPR; k++) {
        STDL_FreeSprite(spr[k]);
    }
    STDL_FreeSurface(dup);
    STDL_FreeSurface(made);
    STDL_FreeSurface(bg);
    STDL_FreeSurface(keyed);
    STDL_FreeSurface(dmask);
    STDL_FreeSurface(glyphs);
    stdl_resp_video->shutdown();
    stdl_resp_screen = NULL;
}

static void free_run(run_t *r)
{
    int f, i;

    for (f = 0; f < FRAMES; f++) {
        for (i = 0; i < 2; i++) {
            free(r->pages[f][i]);
        }
    }
    for (i = 0; i < 4; i++) {
        free(r->surf[i]);
        free(r->mask[i]);
    }
    memset(r, 0, sizeof(*r));
}

static const char *cfg(void)
{
    static char b[64];

    snprintf(b, sizeof(b), "%s%s, %d rows%s", stdl_host_resp_falcon
                                              ? "Falcon " : "TT",
            stdl_host_resp_falcon ? (stdl_host_resp_vga ? "VGA" : "RGB")
                                  : "", g_h,
            g_tall == STDL_RESPONSIVE_HEIGHT ? " (height)" : "");
    return b;
}

static void compare(int falcon, int vga, int h, unsigned seed)
{
    static run_t a, b;
    int f, i, bad = 0;

    SET_MACHINE(falcon, vga);
    g_h = h;
    g_what = STDL_RESPONSIVE_COLOUR | (h > 200 ? g_tall : 0);
    one_run(use_ref, seed, &a);
    one_run(use_lib, seed, &b);
    for (f = 0; f < FRAMES && !bad; f++) {
        for (i = 0; i < 2; i++) {
            if (a.hash[f][i] != b.hash[f][i]) {
                CHECK(0, "%s, seed %u: frame %d page %d differs", cfg(),
                      seed, f, i);
                if (a.pages[f][i] && b.pages[f][i]) {
                    where(a.pages[f][i], b.pages[f][i], a.page_bytes);
                }
                bad = 1;
                break;
            }
        }
    }
    for (i = 0; i < 4; i++) {
        CHECK(a.surf[i] && b.surf[i]
              && memcmp(a.surf[i], b.surf[i], 1) == 0, "surfaces");
    }
    CHECK(memcmp(a.surf[0], b.surf[0], 320 * 200) == 0,
          "%s: the background", cfg());
    CHECK(memcmp(a.surf[1], b.surf[1], 37 * 21) == 0
          && memcmp(a.mask[1], b.mask[1], 37 * 21) == 0,
          "%s: the keyed converter's pixels and mask", cfg());
    CHECK(memcmp(a.surf[2], b.surf[2], 37 * 21) == 0
          && memcmp(a.mask[2], b.mask[2], 37 * 21) == 0,
          "%s: the masked destination's pixels and mask", cfg());
    CHECK(memcmp(a.surf[3], b.surf[3], 35 * 9) == 0,
          "%s: the remapped 1bpp surface", cfg());

#ifndef __m68k__
    /* a TT's lines come in equal pairs */
    if (!falcon) {
        const int l = undoubled(b.pages[FRAMES - 1][0], stdl_rs.line, 0, 480);

        CHECK(l < 0, "TT line %d is not doubled", l);
    }
#endif
    free_run(&a);
    free_run(&b);
}

/*
 * A tall screen without the colour: a 4-plane surface. On a TT, Flip
 * must leave each row's groups on both of its lines, upper planes zero,
 * whatever drew them - the library's calls or writes of the program's
 * own; on a Falcon the surface must be the display page.
 */
static void test_tall4(int falcon, int vga)
{
    STDL_Surface *scr;
    uint8_t *copy;
    int f, i, y, g;

    SET_MACHINE(falcon, vga);
    g_h = 240;
    g_what = g_tall;
    CHECK(use_lib() == 0, "use");
    scr = stdl_resp_video->set_mode(320, 240, 4, STDL_DOUBLEBUF);
    CHECK(scr != NULL && scr->planes == 4 && scr->h == 240
          && scr->format->BitsPerPixel == 4, "%s: a 4-plane tall screen",
          cfg());
    if (scr == NULL) {
        return;
    }
    /* every register write reaches the machine's own registers, a
     * word for the ST's set straight too */
    CHECK(stdl_pal_write != NULL, "the colours go to its registers");
    STDL_SetColour(3, 0x0F01);
    CHECK(stdl_rs.colours[3].r == 255 && stdl_rs.colours[3].g == 0
          && stdl_rs.colours[3].b == 34, "STDL_SetColour reaches it");
    /* the screen's own machine: on the host what was asked for, on
     * the machine (RESPCHK) the machine */
    falcon = stdl_rs.falcon;
    copy = malloc((size_t)scr->stride * 240);
    for (f = 0; f < 12; f++) {
        const int p = stdl_rs.back;
        const uint8_t *page;

        if (f == 6 && !falcon) {
            /* the console's text, on every plane and line of page 0
             * (its Logbase): the next present of it puts it all back */
            uint32_t k;

            for (k = 0; k < stdl_rs.page_bytes; k++) {
                stdl_rs.page[0][k] = (uint8_t)rnd();
            }
            stdl_resp_con_wrote = 0xFF;
        }
        for (i = 0; i < 20; i++) {
            STDL_Rect r = rect(rr(-20, 330), rr(-20, 250), rr(0, 90),
                               rr(0, 60));
            STDL_FillRect(scr, &r, (uint8_t)rr(0, 15));
        }
        STDL_Line(scr, rr(-20, 340), rr(-20, 260), rr(-20, 340),
                  rr(-20, 260), (uint8_t)rr(0, 15));
        for (i = 0; i < 50; i++) {
            /* the program's own writes */
            scr->pixels[(size_t)rr(0, 239) * scr->stride
                        + rr(0, 159)] = (uint8_t)rnd();
        }
        memcpy(copy, scr->pixels, (size_t)scr->stride * 240);
        stdl_resp_video->flip();
        page = stdl_rs.page[p];
        if (falcon) {
            CHECK(memcmp(page + stdl_rs.left
                         + (size_t)stdl_rs.top * stdl_rs.line, copy,
                         (size_t)scr->stride) == 0
                  && scr->stride == stdl_rs.line,
                  "%s: the surface is the display page", cfg());
            continue;
        }
        for (y = 0; y < 240; y++) {
            for (g = 0; g < 20; g++) {
                const uint8_t *src = copy + (size_t)y * scr->stride + g * 8;
                const uint8_t *d0 = page + (size_t)(2 * y) * stdl_rs.line
                                  + g * 16;
                const uint8_t *d1 = d0 + stdl_rs.line;
                static const uint8_t zero[8];

                if (memcmp(d0, src, 8) || memcmp(d1, src, 8)
                    || memcmp(d0 + 8, zero, 8) || memcmp(d1 + 8, zero, 8)) {
                    CHECK(0, "TT, frame %d: row %d group %d not presented",
                          f, y, g);
                    y = 240;
                    break;
                }
            }
        }
    }
    free(copy);
    stdl_resp_video->shutdown();
    stdl_resp_screen = NULL;
}

/*
 * The console's text on a TT's 8-plane screen: once it has written,
 * page 0's next present clears its bands and copies every row to the
 * line under it, where the box drawn would have copied a little.
 */
static void test_console8(void)
{
    STDL_Surface *scr;
    uint8_t *page;
    int f, l, k, line, top, bottom, lines;

    SET_MACHINE(0, 0);
    g_what = STDL_RESPONSIVE_COLOUR;
    CHECK(use_lib() == 0, "use");
    scr = stdl_resp_video->set_mode(320, 200, 8, STDL_DOUBLEBUF);
    CHECK(scr != NULL, "mode");
    if (scr == NULL || stdl_rs.falcon) {
        goto done;
    }
    page = stdl_rs.page[0];
    line = stdl_rs.line;
    top = stdl_rs.top;
    bottom = top + (stdl_rs.h << stdl_rs.dbl);
    lines = (int)(stdl_rs.page_bytes / (uint32_t)line);
    for (f = 0; f < 2; f++) {
        STDL_FillRect(scr, NULL, 77);
        stdl_resp_video->flip();
    }
    /* the bands and the odd lines, as the console writes them */
    for (l = 0; l < lines; l++) {
        if (l < top || l >= bottom || ((l - top) & 1)) {
            for (k = 0; k < line; k++) {
                page[(size_t)l * line + k] = (uint8_t)rnd();
            }
        }
    }
    stdl_resp_con_wrote = 0xFF;
    for (f = 0; f < 2; f++) {
        STDL_Rect r = rect(30, 40, 20, 10);

        STDL_FillRect(scr, &r, (uint8_t)(f + 1));
        stdl_resp_video->flip();
    }
    for (l = 0; l < lines; l++) {
        if (l >= top && l < bottom) {
            continue;
        }
        for (k = 0; k < line && page[(size_t)l * line + k] == 0; k++) {
        }
        if (k < line) {
            CHECK(0, "band line %d not cleared", l);
            break;
        }
    }
    l = undoubled(page, line, top, bottom);
    CHECK(l < 0, "line %d's odd line not the even one's", l);
done:
    stdl_resp_video->shutdown();
    stdl_resp_screen = NULL;
}

#ifndef __m68k__
extern int stdl_host_bottom_opens;

/*
 * The display's own height on an ST: its own 200-line screen, with no
 * border opened and no error; a TT's and a Falcon's 240 are its tall
 * screen's (compare, test_tall4). Asked for with the tall screen, the
 * tall screen.
 */
static void test_height(void)
{
    const uint32_t mch = stdl.mach.mch_cookie;
    const int opens = stdl_host_bottom_opens;

    g_what = STDL_RESPONSIVE_HEIGHT;
    CHECK(use_lib() == 0, "height: use");
    stdl.mach.mch_cookie = 0x00020000UL;            /* a TT */
    CHECK(stdl_resp_video->enter(320, 240, 4), "height: a TT's 240 lines");
    CHECK(!stdl_resp_video->enter(320, 200, 4), "height: 200 on a TT");
    stdl.mach.mch_cookie = 0x00010010UL;            /* a Mega STE */
    CHECK(!stdl_resp_video->enter(320, 240, 4), "height: an ST's own screen");
    STDL_SetError("");
    stdl_resp_video->st_mode(240);
    CHECK(stdl_host_bottom_opens == opens && stdl_ovsc_cap == 0
          && STDL_GetError()[0] == '\0', "height: no border on an ST");
    g_what = STDL_RESPONSIVE_TALL | STDL_RESPONSIVE_HEIGHT;
    CHECK(use_lib() == 0, "tall and height: use");
    stdl_resp_video->st_mode(240);
    CHECK(stdl_host_bottom_opens == opens + 1,
          "tall and height: the tall screen opens the border");
    stdl.mach.mch_cookie = mch;
}
#endif

/*
 * 4-plane sources on an 8-plane screen: a keyed surface blitted, and
 * sprites made from one, pre-shifted or not, drawn - each a pixel at a
 * time against what the 4-plane surface holds, over a background no
 * 4-plane colour can be.
 */
static uint8_t expect[240][320];

static void test_mixed(void)
{
    STDL_Surface *scr, *s4;
    STDL_Sprite *spr[2];
    int t, x, y, i, k, bad = 0;

    SET_MACHINE(0, 0);
    g_what = STDL_RESPONSIVE_COLOUR;
    CHECK(use_lib() == 0, "use");
    scr = stdl_resp_video->set_mode(320, 200, 8, STDL_DOUBLEBUF);
    if (scr == NULL) {
        CHECK(0, "mode");
        return;
    }
    rng_state = 99;
    s4 = stdl_surface_create(37, 21);
    for (y = 0; y < 21; y++) {
        for (x = 0; x < 37; x++) {
            STDL_PutPixel(s4, x, y, (uint8_t)rr(0, 15));
        }
    }
    STDL_SetColourKey(s4, 1, 5);
    spr[0] = STDL_SpriteFromSurface(s4, 37, STDL_PRESHIFT);
    spr[1] = STDL_SpriteFromSurface(s4, 37, 0);
    CHECK(spr[0] != NULL && spr[0]->planes != 8 && spr[1] != NULL,
          "4-plane sprites");
    STDL_FillRect(scr, NULL, 200);
    memset(expect, 200, sizeof(expect));
    for (t = 0; t < 120 && !bad; t++) {
        const int dx = rr(-45, 330), dy = rr(-25, 210);

        if (t & 1) {
            STDL_Rect r = rect(dx, dy, 0, 0);

            STDL_BlitSurface(s4, NULL, scr, &r);
        } else {
            STDL_BlitSprite(spr[(t >> 1) & 1], 0, scr, dx, dy);
        }
        for (y = 0; y < 21; y++) {
            for (x = 0; x < 37; x++) {
                const int px = dx + x, py = dy + y;
                const uint16_t *m = (const uint16_t *)(s4->mask
                                     + (size_t)y * s4->maskstride);

                if (px < 0 || px >= 320 || py < 0 || py >= 200
                    || (m[x >> 4] & (0x8000u >> (x & 15)))) {
                    continue;
                }
                expect[py][px] = STDL_GetPixel(s4, x, y);
            }
        }
        for (y = 0; y < 200 && !bad; y++) {
            for (x = 0; x < 320; x++) {
                k = STDL_GetPixel(scr, x, y);
                if (k != expect[y][x]) {
                    CHECK(0, "%s %d at (%d,%d): (%d,%d) is %d, want %d",
                          (t & 1) ? "blit" : "sprite", t, dx, dy, x, y, k,
                          expect[y][x]);
                    bad = 1;
                    break;
                }
            }
        }
    }
    for (i = 0; i < 2; i++) {
        STDL_FreeSprite(spr[i]);
    }
    STDL_FreeSurface(s4);
    stdl_resp_video->shutdown();
    stdl_resp_screen = NULL;
}

/*
 * STDL_TRANSPARENT as on 4 planes, in both models: a colour on a
 * surface without a mask, a hole over colour 0 in one with, and a key
 * that keeps the mask; and the library's opacity cache told of holes.
 */
static void test_transparent(void)
{
    int model, x, y;

    for (model = 0; model < 2; model++) {
        STDL_Surface *scr, *s, *c;
        STDL_Rect r = rect(4, 2, 20, 5), d = rect(0, 0, 0, 0);

        SET_MACHINE(0, 0);
        g_what = STDL_RESPONSIVE_COLOUR;
        (model ? use_lib : use_ref)();
        scr = stdl_resp_video->set_mode(320, 200, 8, 0);
        s = STDL_CreateSurface(40, 10);
        if (scr == NULL || s == NULL) {
            CHECK(0, "model %d: mode", model);
            return;
        }
        STDL_FillRect(s, NULL, STDL_TRANSPARENT);
        CHECK(STDL_GetPixel(s, 0, 0) == STDL_TRANSPARENT,
              "model %d: 16 a colour without a mask", model);
        CHECK(STDL_CreateMask(s, 0) == 0, "mask");
        CHECK(!model || STDL_SurfaceIsOpaque(s), "opaque");
        STDL_FillRect(s, &r, STDL_TRANSPARENT);
        STDL_PutPixel(s, 30, 8, STDL_TRANSPARENT);
        CHECK(!model || !STDL_SurfaceIsOpaque(s), "holes not seen");
        CHECK(STDL_SetColourKey(s, 1, STDL_TRANSPARENT) == 0, "key");
        c = STDL_DuplicateSurface(s);
        CHECK(c != NULL && c->mask != NULL, "model %d: copy", model);
        STDL_FillRect(scr, NULL, 5);
        STDL_BlitSurface(s, NULL, scr, &d);
        for (y = 0; y < 10; y++) {
            for (x = 0; x < 40; x++) {
                const int in = (x >= 4 && x < 24 && y >= 2 && y < 7)
                               || (x == 30 && y == 8);

                CHECK(mask_bit(s, x, y) == in
                      && STDL_GetPixel(s, x, y) == (in ? 0 : 16)
                      && STDL_GetPixel(scr, x, y) == (in ? 5 : 16),
                      "model %d (%d,%d): mask %d, %u, shown %u", model, x,
                      y, mask_bit(s, x, y), STDL_GetPixel(s, x, y),
                      STDL_GetPixel(scr, x, y));
                CHECK(c == NULL || mask_bit(c, x, y) == in,
                      "model %d (%d,%d): the copy's mask", model, x, y);
            }
        }
        STDL_FreeSurface(c);
        STDL_FreeSurface(s);
        stdl_resp_video->shutdown();
        stdl_resp_screen = NULL;
    }
}

/*
 * A bank's art for an 8-plane screen (asset8.c, docs/format.md 6.1),
 * written here from one piece of index art: its own 256 colours
 * (surface8, sprite8, palette8), planes 4-7 over the ST's art (their
 * form byte 2), and the ST's alone. On an 8-plane screen the loaders give
 * each id's 8-plane form, pixel for pixel, and the 4-plane one where
 * the bank has nothing else; with no such screen, the ST's chunks.
 */
#define BK_W 32                 /* two sprite frames of 16 */
#define BK_H 6
static uint8_t bk_art[BK_H][BK_W];
static uint8_t bk_buf[8192];
static uint32_t bk_len;
static struct { uint16_t type, id; uint32_t off, len; } bk_dir[16];
static int bk_n;

static void bk8(unsigned v) { bk_buf[bk_len++] = (uint8_t)v; }
static void bk16(unsigned v) { bk8(v >> 8); bk8(v); }
static void bk32(uint32_t v) { bk16(v >> 16); bk16(v & 0xFFFF); }

static void bk_begin(int type, int id)
{
    bk_dir[bk_n].type = (uint16_t)type;
    bk_dir[bk_n].id = (uint16_t)id;
    bk_dir[bk_n].off = bk_len;
}

static void bk_end(void)
{
    bk_dir[bk_n].len = bk_len - bk_dir[bk_n].off;
    bk_n++;
}

/* rows of `planes` planes from bit `shift` of the art: a group is
 * planes words, plane p's word holding bit shift + p of each pixel */
static void bk_rows(int planes, int shift)
{
    int y, g, p, b;

    for (y = 0; y < BK_H; y++) {
        for (g = 0; g < BK_W / 16; g++) {
            for (p = 0; p < planes; p++) {
                unsigned w = 0;

                for (b = 0; b < 16; b++) {
                    if ((bk_art[y][g * 16 + b] >> (shift + p)) & 1) {
                        w |= 0x8000u >> b;
                    }
                }
                bk16(w);
            }
        }
    }
}

/* the ST's sprite format: a frame of 16, then each row's group as its
 * mask (low nibble the key) and four planes clear under it */
static void bk_sprite4(int id, unsigned key)
{
    int f, y, p, b;

    bk_begin(CHUNK_SPRITE, id);
    bk16(16); bk16(BK_H); bk16(BK_W / 16); bk8(1); bk8(4); bk16(1);
    bk32(5 * BK_H);
    for (f = 0; f < BK_W / 16; f++) {
        for (y = 0; y < BK_H; y++) {
            unsigned m = 0, w[4] = { 0, 0, 0, 0 };

            for (b = 0; b < 16; b++) {
                const unsigned v = bk_art[y][f * 16 + b] & 15u;

                if (v == key) {
                    m |= 0x8000u >> b;
                    continue;
                }
                for (p = 0; p < 4; p++) {
                    if ((v >> p) & 1) {
                        w[p] |= 0x8000u >> b;
                    }
                }
            }
            bk16(m); bk16(w[0]); bk16(w[1]); bk16(w[2]); bk16(w[3]);
        }
    }
    bk_end();
}

/* the ST's surface chunk, 4-plane rows of the art's low nibbles */
static void bk_surface4(int id, int haskey, unsigned key)
{
    bk_begin(CHUNK_SURFACE, id);
    bk16(BK_W); bk16(BK_H); bk8((unsigned)haskey); bk8(key);
    bk_rows(4, 0);
    bk_end();
}

/* an 8-plane screen's chunk: a surface8, or with a frame width a
 * sprite8; form 2's rows planes 4-7, the others' all eight */
static void bk_art8(int id, int framew, unsigned form, unsigned key)
{
    bk_begin(framew ? CHUNK_SPRITE8 : CHUNK_SURFACE8, id);
    if (framew) {
        bk16((unsigned)framew);
    }
    bk16(BK_W); bk16(BK_H); bk8(form); bk8(key);
    if (form == 2) {
        bk_rows(4, 4);
    } else {
        bk_rows(8, 0);
    }
    bk_end();
}

/* the directory, then the payloads; the directory written after them
 * in bk_buf and moved to the front */
static void bk_write(const char *path)
{
    FILE *f = fopen(path, "wb");
    const uint32_t base = 8 + 12 * (uint32_t)bk_n, payloads = bk_len;
    int i;

    bk8('S'); bk8('T'); bk8('D'); bk8('L'); bk16(1); bk16((unsigned)bk_n);
    for (i = 0; i < bk_n; i++) {
        bk16(bk_dir[i].type);
        bk16(bk_dir[i].id);
        bk32(base + bk_dir[i].off);
        bk32(bk_dir[i].len);
    }
    fwrite(bk_buf + payloads, 1, bk_len - payloads, f);
    fwrite(bk_buf, 1, payloads, f);
    fclose(f);
}

/* a sprite drawn over colour 77 at an odd x, against the art: `want`
 * gives a pixel's colour, or -1 where it is transparent */
static void bk_drawn(STDL_Surface *scr, STDL_Sprite *spr, const char *what,
                     int (*want)(unsigned v))
{
    int f, x, y;

    for (f = 0; f < BK_W / 16; f++) {
        STDL_FillRect(scr, NULL, 77);
        STDL_BlitSprite(spr, f, scr, 37, 11);
        for (y = 0; y < BK_H; y++) {
            for (x = 0; x < 16; x++) {
                const int v = want(bk_art[y][f * 16 + x]);
                const int got = STDL_GetPixel(scr, 37 + x, 11 + y);

                if (got != (v < 0 ? 77 : v)) {
                    CHECK(0, "%s frame %d (%d,%d): %d, want %d", what, f,
                          x, y, got, v < 0 ? 77 : v);
                    return;
                }
            }
        }
    }
}

static int bk_own(unsigned v) { return v == 200 ? -1 : (int)v; }
static int bk_over(unsigned v) { return (v & 15) == 5 ? -1 : (int)v; }
static int bk_st(unsigned v) { return (v & 15) == 3 ? -1 : (int)(v & 15); }

static void test_bank8(void)
{
    static const char *path = "bank8.tmp";
    static STDL_Colour cols[256];
    STDL_Palette pal = { 256, cols };
    STDL_Surface *scr, *s[4];
    STDL_Sprite *spr;
    int i, x, y, k, pass;

    rng_state = 4242;
    for (y = 0; y < BK_H; y++) {
        for (x = 0; x < BK_W; x++) {
            k = rr(0, 7);
            bk_art[y][x] = (uint8_t)(k == 0 ? 200 : k == 1 ? 0x35
                                     : k == 2 ? 0xA3 : rnd());
        }
    }
    bk_len = 0;
    bk_n = 0;
    bk_begin(CHUNK_PALETTE, 0);
    bk16(16);
    for (i = 0; i < 16; i++) {
        bk8((unsigned)i * 16); bk8((unsigned)i * 16); bk8((unsigned)i * 16);
        bk8(0);
    }
    bk_end();
    bk_begin(CHUNK_PALETTE8, 0);
    bk16(256);
    for (i = 0; i < 256; i++) {
        bk8((unsigned)i); bk8(255u - (unsigned)i); bk8((unsigned)i ^ 0x55u);
        bk8(0);
    }
    bk_end();
    bk_surface4(1, 1, 3);
    bk_art8(1, 0, 1, 200);              /* its own, keyed on 200 */
    bk_surface4(2, 1, 5);
    bk_art8(2, 0, 2, 0);                /* over the ST's, keyed on 5 */
    bk_surface4(3, 0, 0);
    bk_sprite4(4, 3);
    bk_art8(4, 16, 1, 200);
    bk_sprite4(5, 5);
    bk_art8(5, 16, 2, 0);
    bk_sprite4(6, 3);
    bk_write(path);

    /* the colour alone: the bank's 8-plane art is an option of its own */
    SET_MACHINE(0, 0);
    g_what = STDL_RESPONSIVE_COLOUR;
    CHECK(use_lib() == 0, "use");
    scr = stdl_resp_video->set_mode(320, 200, 8, 0);
    s[1] = STDL_LoadSurface(path, 1);
    CHECK(s[1] != NULL && s[1]->planes == 4,
          "the ST's art without STDL_RESPONSIVE_BANKS");
    STDL_FreeSurface(s[1]);
    stdl_resp_video->shutdown();
    stdl_resp_screen = NULL;

    g_what = STDL_RESPONSIVE_COLOUR | STDL_RESPONSIVE_BANKS;
    CHECK(use_lib() == 0, "use");
    scr = stdl_resp_video->set_mode(320, 200, 8, 0);
    CHECK(scr != NULL && STDL_RESP_DEEP(), "an 8-plane screen");

    /* the palette: its 256 colours */
    CHECK(STDL_LoadPalette(path, 0, &pal) == 256
          && cols[9].r == 9 && cols[9].g == 246 && cols[9].b == (9 ^ 0x55)
          && cols[255].r == 255 && cols[255].g == 0,
          "an 8-plane screen's 256 colours");

    /* surfaces: their own, over the ST's, and the ST's alone */
    for (i = 1; i <= 3; i++) {
        s[i] = STDL_LoadSurface(path, i);
        CHECK(s[i] != NULL && s[i]->planes == (i == 3 ? 4 : 8),
              "surface %d: %d planes", i, s[i] ? s[i]->planes : 0);
    }
    for (y = 0; y < BK_H && s[1] && s[2] && s[3]; y++) {
        for (x = 0; x < BK_W; x++) {
            const unsigned v = bk_art[y][x];

            CHECK(STDL_GetPixel(s[1], x, y) == v
                  && mask_bit(s[1], x, y) == (v == 200),
                  "surface8 (%d,%d): %u mask %d, want %u", x, y,
                  STDL_GetPixel(s[1], x, y), mask_bit(s[1], x, y), v);
            CHECK(STDL_GetPixel(s[2], x, y) == v
                  && mask_bit(s[2], x, y) == ((v & 15) == 5),
                  "surface over the ST's (%d,%d): %u mask %d, want %u", x,
                  y, STDL_GetPixel(s[2], x, y), mask_bit(s[2], x, y), v);
            CHECK(STDL_GetPixel(s[3], x, y) == (v & 15),
                  "the ST's surface (%d,%d)", x, y);
        }
    }
    for (i = 1; i <= 3; i++) {
        STDL_FreeSurface(s[i]);
    }

    /* sprites, pre-shifted and not, drawn on the screen */
    for (pass = 0; pass < 2; pass++) {
        const uint32_t flags = pass ? STDL_PRESHIFT : 0;

        spr = STDL_LoadSprite(path, 4, flags);
        CHECK(spr != NULL && spr->planes == 8, "sprite8");
        if (spr != NULL) {
            bk_drawn(scr, spr, "sprite8", bk_own);
            STDL_FreeSprite(spr);
        }
        spr = STDL_LoadSprite(path, 5, flags);
        CHECK(spr != NULL && spr->planes == 8, "sprite over the ST's");
        if (spr != NULL) {
            bk_drawn(scr, spr, "sprite over the ST's", bk_over);
            STDL_FreeSprite(spr);
        }
        spr = STDL_LoadSprite(path, 6, flags);
        CHECK(spr != NULL && spr->planes == 4, "the ST's sprite");
        if (spr != NULL) {
            bk_drawn(scr, spr, "the ST's sprite", bk_st);
            STDL_FreeSprite(spr);
        }
    }
    stdl_resp_video->shutdown();
    stdl_resp_screen = NULL;

    /* no 8-plane screen: the ST's chunks, whatever else the bank has */
    CHECK(STDL_LoadPalette(path, 0, &pal) == 16 && cols[9].r == 144,
          "the ST's 16 colours");
    s[1] = STDL_LoadSurface(path, 1);
    CHECK(s[1] != NULL && s[1]->planes == 4
          && STDL_GetPixel(s[1], 0, 0) == (bk_art[0][0] & 15u),
          "the ST's surface without an 8-plane screen");
    STDL_FreeSurface(s[1]);
    spr = STDL_LoadSprite(path, 4, 0);
    CHECK(spr != NULL && spr->planes == 4, "the ST's sprite there");
    STDL_FreeSprite(spr);
    remove(path);
}

/*
 * The blit flags on an 8-plane surface (STDL_BlitSurfaceEx), against
 * what was asked for, in both models: a destination whose mask marks a
 * band as foreground, a source blitted across it shifted, under UNDER
 * (the band keeps itself), MARK (what is drawn is marked) and both; and
 * in the library, which the reference does not model, a 4-plane source.
 */
static void test_blitflags(void)
{
    static const unsigned fl[3] = {
        STDL_BLIT_UNDER, STDL_BLIT_MARK, STDL_BLIT_UNDER | STDL_BLIT_MARK
    };
    int model, pass, x, y;

    for (model = 0; model < 2; model++) {
        STDL_Surface *scr, *dst, *src;

        SET_MACHINE(0, 0);
        g_what = STDL_RESPONSIVE_COLOUR;
        (model ? use_lib : use_ref)();
        scr = stdl_resp_video->set_mode(320, 200, 8, 0);
        dst = STDL_CreateSurface(48, 8);
        if (scr == NULL || dst == NULL) {
            CHECK(0, "model %d: mode", model);
            return;
        }
        for (pass = 0; pass < (model ? 4 : 3); pass++) {
            const unsigned flags = fl[pass < 3 ? pass : 2];
            const int deep = pass < 3;
            const unsigned col = deep ? 200 : 5;
            STDL_Rect band = rect(10, 0, 10, 8), at = rect(3, 0, 0, 0);

            STDL_FillRect(dst, NULL, 9);
            CHECK(STDL_CreateMask(dst, 0) == 0, "mask");
            STDL_FillRect(dst, &band, STDL_TRANSPARENT);
            src = deep ? STDL_CreateSurface(32, 8)
                       : stdl_surface_create(32, 8);
            STDL_FillRect(src, NULL, (uint8_t)col);
            CHECK(STDL_BlitSurfaceEx(src, NULL, dst, &at, flags) == 0,
                  "model %d pass %d: the blit", model, pass);
            for (y = 0; y < 8; y++) {
                for (x = 0; x < 48; x++) {
                    const int inband = x >= 10 && x < 20;
                    const int drawn = x >= 3 && x < 35
                        && !((flags & STDL_BLIT_UNDER) && inband);
                    const int pix = drawn ? (int)col : inband ? 0 : 9;
                    const int m = drawn ? ((flags & STDL_BLIT_MARK) != 0
                                           || ((flags & STDL_BLIT_UNDER)
                                               && inband))
                                        : inband;

                    if (STDL_GetPixel(dst, x, y) != pix
                        || mask_bit(dst, x, y) != m) {
                        CHECK(0, "model %d pass %d (%d,%d): %u mask %d, "
                              "want %d mask %d", model, pass, x, y,
                              STDL_GetPixel(dst, x, y), mask_bit(dst, x, y),
                              pix, m);
                        x = 48;
                        y = 8;
                    }
                }
            }
            STDL_FreeSurface(src);
        }
        STDL_FreeSurface(dst);
        stdl_resp_video->shutdown();
        stdl_resp_screen = NULL;
    }
}

/*
 * A surface over a caller's block of 8 planes
 * (STDL_CreateSurfaceFrom8Planes), and STDL_CreateSurfaceFrom's still
 * 4 planes: a stride wider than it needs, drawing that lands in the
 * caller's bytes, a blit keyed on the caller's mask, an UNDER blit that
 * takes that mask for a priority plane, and a free that leaves both
 * blocks (the sanitizer would see a free of them).
 */
static void test_borrowed(void)
{
    enum { BW = 40, BH = 6, BS = 64, MS = 8 };
    uint32_t *pix = calloc(1, BS * BH);
    uint16_t *msk = calloc(1, MS * BH);
    STDL_Surface *scr, *b, *src;
    STDL_Rect r = rect(5, 1, 20, 3), at = rect(0, 0, 0, 0);
    int x, y;

    /* with another model's routines in charge, refused */
    SET_MACHINE(0, 0);
    g_what = STDL_RESPONSIVE_COLOUR;
    CHECK(use_ref() == 0, "use");
    CHECK(stdl_resp_video->set_mode(320, 200, 8, 0) != NULL
          && STDL_CreateSurfaceFrom8Planes(pix, BW, BH, BS, NULL, 0) == NULL,
          "no 8-plane block without the 8-plane routines");
    stdl_resp_video->shutdown();
    stdl_resp_screen = NULL;

    CHECK(use_lib() == 0, "use");
    scr = stdl_resp_video->set_mode(320, 200, 8, 0);
    CHECK(STDL_CreateSurfaceFrom8Planes(pix, BW, BH, 40, NULL, 0) == NULL,
          "a stride not of 16-byte groups refused");
    b = STDL_CreateSurfaceFrom(pix, BW, BH, BS, NULL, 0);
    CHECK(b != NULL && b->planes == 4, "STDL_CreateSurfaceFrom 4 planes");
    STDL_FreeSurface(b);
    b = STDL_CreateSurfaceFrom8Planes(pix, BW, BH, BS, (uint8_t *)msk, MS);
    CHECK(b != NULL && b->planes == 8 && b->stride == BS
          && (b->flags & STDL_SRCKEY), "borrowed 8 planes");
    if (scr == NULL || b == NULL) {
        goto done;
    }
    /* the caller's mask marks x 26-39 */
    for (y = 0; y < BH; y++) {
        msk[y * (MS / 2) + 1] = 0x003Fu;
        msk[y * (MS / 2) + 2] = 0xFFFFu;
    }
    STDL_FillRect(b, &r, 200);
    /* the caller's bytes hold it: row 2, pixel 10's planes 3, 6 and 7 */
    {
        const uint16_t *g = (const uint16_t *)((uint8_t *)pix + 2 * BS);
        const unsigned bit = 0x8000u >> 10;

        CHECK((g[3] & bit) && (g[6] & bit) && (g[7] & bit)
              && !(g[0] & bit) && STDL_GetPixel(b, 10, 2) == 200,
              "the drawing in the caller's block");
    }
    STDL_FillRect(scr, NULL, 7);
    STDL_BlitSurface(b, NULL, scr, &at);
    CHECK(STDL_GetPixel(scr, 10, 2) == 200 && STDL_GetPixel(scr, 35, 2) == 7,
          "keyed on the caller's mask");
    /* UNDER: the marked band keeps itself */
    src = STDL_CreateSurface(BW, BH);
    STDL_FillRect(src, NULL, 99);
    at = rect(0, 0, 0, 0);
    STDL_BlitSurfaceEx(src, NULL, b, &at, STDL_BLIT_UNDER);
    for (x = 0; x < BW; x++) {
        CHECK(STDL_GetPixel(b, x, 4) == (x >= 26 ? 0 : 99),
              "UNDER over the caller's mask at %d: %u", x,
              STDL_GetPixel(b, x, 4));
    }
    STDL_FreeSurface(src);
    STDL_FreeSurface(b);
done:
    free(pix);
    free(msk);
    stdl_resp_video->shutdown();
    stdl_resp_screen = NULL;
}

/*
 * Mode sets in turn: an ST set after a responsive screen takes it down
 * (its pages, the palette's sink), and a second responsive set replaces
 * the first rather than getting it back.
 */
static void test_modesets(void)
{
    STDL_Surface *scr;
#ifndef __m68k__
    const uint32_t mch = stdl.mach.mch_cookie;

    stdl.mach.mch_cookie = 0x00020000UL;        /* a TT: its own screens */
#endif

    SET_MACHINE(0, 0);
    g_what = STDL_RESPONSIVE_COLOUR | STDL_RESPONSIVE_TALL;
    CHECK(use_lib() == 0, "use");
    scr = stdl_resp_video->set_mode(320, 200, 8, 0);
    CHECK(scr != NULL && scr->h == 200, "the 8-plane screen");
    CHECK(!stdl_resp_video->enter(320, 200, 4) && stdl_resp_screen == NULL
          && stdl_pal_write == NULL && stdl_rs.page[0] == NULL,
          "an ST set takes the responsive screen down");
    CHECK(stdl_resp_video->enter(320, 240, 8), "a tall 8-plane set");
    scr = stdl_resp_video->set_mode(320, 240, 8, 0);
    CHECK(scr != NULL && scr->h == 240, "the tall 8-plane screen");
    CHECK(stdl_resp_video->enter(320, 200, 8) && stdl_resp_screen == NULL,
          "a second responsive set takes the first down");
    scr = stdl_resp_video->set_mode(320, 200, 8, 0);
    CHECK(scr != NULL && scr->h == 200, "the second set's own rows");
    stdl_resp_video->shutdown();
#ifndef __m68k__
    /* an ST's tall border, closed again by a 200-line set */
    stdl_ovsc_cap = 240;
    stdl_resp_tall_st(200);
    CHECK(stdl_ovsc_cap == 0, "a 200-line set after a tall one");
    stdl.mach.mch_cookie = mch;
#endif
}

/* the converter's pixels against what was asked for */
static void test_reference(void)
{
    uint8_t idx[19 * 5];
    int model, i, x, y;

    for (i = 0; i < (int)sizeof(idx); i++) {
        idx[i] = (uint8_t)(i * 37 + 11);
    }
    for (model = 0; model < 2; model++) {
        STDL_Surface *s;

        SET_MACHINE(0, 0);
        g_what = STDL_RESPONSIVE_COLOUR;
        (model ? use_lib : use_ref)();
        CHECK(stdl_resp_video->set_mode(320, 200, 8, 0) != NULL, "mode");
        s = STDL_SurfaceFromIndexed8(idx, 19, 5, 19, -1);
        for (y = 0; y < 5; y++) {
            for (x = 0; x < 19; x++) {
                CHECK(STDL_GetPixel(s, x, y) == idx[y * 19 + x],
                      "model %d (%d,%d): %u, want %u", model, x, y,
                      STDL_GetPixel(s, x, y), idx[y * 19 + x]);
            }
        }
        STDL_FreeSurface(s);
        stdl_resp_video->shutdown();
        stdl_resp_screen = NULL;
    }
}

/*
 * On the machine (RESPCHK.TOS), built by hand from the root:
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 -Iinclude \
 *       -Isrc -Ilib/xpad/src -o dist/RESPCHK.TOS \
 *       tests/host/test_responsive.c tests/host/p8ref.c libstdl.a
 * and run on a TT or a Falcon: the same comparisons on its own screen,
 * at 200 and 240 rows, printing "RC:ok" or the failures, then "RC:done".
 */
int main(void)
{
    unsigned i;

#ifdef __m68k__
    if (STDL_Init(STDL_INIT_VIDEO) < 0
        || STDL_SetVideoMode(320, 200, 4, 0) == NULL) {
        fprintf(stderr, "RC:no screen\nRC:done\n");
        return 1;
    }
    stdl_host_resp_falcon = STDL_GetMachineInfo()->is_falcon;
#endif
    if (getenv("TEST_OPS") != NULL) {
        g_ops = (unsigned)strtoul(getenv("TEST_OPS"), NULL, 0);
    }
    for (i = 0; i < sizeof(font8); i++) {
        font8[i] = (uint8_t)(i * 151 + 7);
    }
    for (i = 0; i < sizeof(font12); i++) {
        font12[i] = (uint8_t)(i * 89 + 3);
    }
    test_reference();
    test_transparent();
    test_blitflags();
    test_borrowed();
    test_modesets();
    test_bank8();
#ifdef __m68k__
    compare(0, 0, 200, 1);
    compare(0, 0, 240, 4);
    test_mixed();
    test_tall4(0, 0);
    test_console8();
    g_tall = STDL_RESPONSIVE_HEIGHT;
    compare(0, 0, 240, 7);
    test_tall4(0, 0);
    STDL_Quit();
    fprintf(stderr, failures ? "RC:%d failure(s)\nRC:done\n"
                             : "RC:ok\nRC:done\n", failures);
    return failures != 0;
#else
    compare(0, 0, 200, 1);              /* TT */
    compare(1, 1, 200, 2);              /* Falcon VGA */
    compare(1, 0, 200, 3);              /* Falcon RGB */
    compare(0, 0, 240, 4);              /* tall: TT */
    compare(1, 1, 240, 5);              /* Falcon VGA */
    compare(1, 0, 240, 6);              /* Falcon RGB, overscan */
    test_mixed();
    test_tall4(0, 0);
    test_tall4(1, 1);
    test_tall4(1, 0);
    test_console8();
    /* the display's own height: the same 240 on these, in overscan on
     * a Falcon's RGB too */
    g_tall = STDL_RESPONSIVE_HEIGHT;
    compare(1, 0, 240, 7);
    test_tall4(0, 0);
    test_tall4(1, 0);
    test_height();
    if (failures) {
        printf("test_responsive: %d failure(s)\n", failures);
        return 1;
    }
    printf("test_responsive: ok\n");
    return 0;
#endif
}
