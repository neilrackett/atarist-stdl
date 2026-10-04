/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The other half of SPRCOST: the primitives a game draws besides
 * sprites, restores and rectangles - tiles, keyed blits at aligned
 * x (the SDL port's sprite), text and single glyphs, lines and
 * circles, span and point batches, chunky frames, single pixels,
 * the XOR forms, filled polygons beside the span lists they replace.
 * Same method and output format, so tests/hatari/sccmp.py compares
 * two logs: each case runs until it has taken MINTICKS of the 200Hz
 * clock, twice per process (passes A and B, the same-binary floor),
 * the library's choice beside the CPU forced where there is a
 * BLiTTER.
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Iinclude -Ilib/xpad/src -o dist/PRIMCOST.TOS \
 *       tests/hatari/primcost.c libstdl.a
 *   TOS=$PWD/tmp/tos/etos256uk.img MACHINE=st EXTRA="--memsize 4" \
 *       tests/hatari/run.sh pc dist/PRIMCOST.TOS 8 "waitfor SC:done"
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdl/stdl.h>

#define MINTICKS 120

static STDL_Surface *dst, *tsrc, *ksrc16, *ksrc32;
static STDL_Tileset *tiles_plain, *tiles_masked;
static STDL_Font font;
static STDL_Point pts[100];
static uint8_t ptcols[100];
static STDL_Span hsp[32], vsp[32];
static uint8_t chunky[32 * 32];
static uint8_t map[16];
static int have_blitter;
static char pass_name;

static uint32_t rng = 0x13579BDFUL;
static uint32_t rnd(void)
{
    rng = rng * 1103515245UL + 12345UL;
    return (rng >> 16) & 0x7FFF;
}

static void randomise(STDL_Surface *s, int colours)
{
    int x, y;
    for (y = 0; y < s->h; y++)
        for (x = 0; x < s->w; x++)
            STDL_PutPixel(s, x, y, (uint8_t)(rnd() % colours));
}

typedef void (*op_fn)(int i);

static void op_tile(int i)    { STDL_BlitTile(tiles_plain, i & 3, dst, 64, 40); }
static void op_tilem(int i)   { STDL_BlitTile(tiles_masked, i & 3, dst, 64, 40); }
static void op_tilerow(int i)
{
    int k;
    for (k = 0; k < 20; k++) {
        STDL_BlitTile(tiles_plain, (i + k) & 3, dst, k * 16, 40);
    }
}
static void op_key16(int i)
{
    STDL_Rect d = { 64, 40, 0, 0 };
    (void)i;
    STDL_BlitSurface(ksrc16, NULL, dst, &d);
}
static void op_key32(int i)
{
    STDL_Rect d = { 64, 40, 0, 0 };
    (void)i;
    STDL_BlitSurface(ksrc32, NULL, dst, &d);
}
static void op_text(int i)
{
    (void)i;
    STDL_DrawText(dst, &font, 21, 50, "SCORE 1234", 7);
}
static void op_chars(int i)
{
    static const char s[] = "SCORE 1234";
    int k;
    (void)i;
    for (k = 0; k < 10; k++) {
        STDL_DrawChar(dst, &font, 21 + k * 8, 50, s[k], 7);
    }
}
/* an 8x8 font on its own grid: the movep path */
static void op_text8(int i)
{
    (void)i;
    STDL_DrawText(dst, &font, 16, 50, "SCORE 1234", 7);
}
static void op_chars8(int i)
{
    static const char s[] = "SCORE 1234";
    int k;
    (void)i;
    for (k = 0; k < 10; k++) {
        STDL_DrawChar(dst, &font, 16 + k * 8, 50, s[k], 7);
    }
}

/* filled polygons, and the same shapes as span lists */
static const STDL_Point diamond[4] = {
    { 160, 20 }, { 240, 100 }, { 160, 180 }, { 80, 100 }
};
static const STDL_Point tri[3] = { { 100, 50 }, { 130, 70 }, { 95, 72 } };
static STDL_Span dsp[200], tsp[40];
static int ndsp, ntsp;

/* a convex polygon's spans by the fill rule, slowly, once */
static int spans_of(const STDL_Point *p, int n, STDL_Span *out)
{
    int y, i, k = 0, top = p[0].y, bot = p[0].y;

    for (i = 1; i < n; i++) {
        if (p[i].y < top) top = p[i].y;
        if (p[i].y > bot) bot = p[i].y;
    }
    for (y = top; y < bot; y++) {
        int l = 32767, r = -32768;

        for (i = 0; i < n; i++) {
            const STDL_Point *a = &p[i], *b = &p[(i + 1) % n];
            int lo, hi, num, den, x;

            if (a->y == b->y) continue;
            lo = a->y < b->y ? a->y : b->y;
            hi = a->y < b->y ? b->y : a->y;
            if (y < lo || y >= hi) continue;
            num = (y - a->y) * (b->x - a->x);
            den = b->y - a->y;
            x = a->x + num / den;
            if (x < l) l = x;
            if (x > r) r = x;
        }
        if (r > l) {
            out[k].x = (int16_t)l;
            out[k].y = (int16_t)y;
            out[k].len = (int16_t)(r - l);
            k++;
        }
    }
    return k;
}
static void op_poly_d(int i)
{
    (void)i;
    STDL_FillPolygon(dst, diamond, 4, 5, STDL_DRAW_XOR);
}
static void op_spans_d(int i) { (void)i; STDL_XorHSpans(dst, dsp, ndsp, 5); }
static void op_poly_t(int i)
{
    (void)i;
    STDL_FillPolygon(dst, tri, 3, 5, STDL_DRAW_XOR);
}
static void op_spans_t(int i) { (void)i; STDL_XorHSpans(dst, tsp, ntsp, 5); }
static void op_poly_set(int i)
{
    (void)i;
    STDL_FillPolygon(dst, diamond, 4, 9, STDL_DRAW_SET);
}

static void op_hline32(int i)  { (void)i; STDL_HLine(dst, 37, 68, 60, 5); }
static void op_hline300(int i) { (void)i; STDL_HLine(dst, 7, 306, 60, 5); }
static void op_vline(int i)    { (void)i; STDL_VLine(dst, 77, 20, 69, 5); }
static void op_line(int i)     { (void)i; STDL_Line(dst, 10, 10, 110, 70, 5); }
static void op_fcircle(int i)  { (void)i; STDL_FillCircle(dst, 100, 80, 12, 5); }
static void op_circle(int i)   { (void)i; STDL_Circle(dst, 100, 80, 20, 5); }
static void op_hspans(int i)   { (void)i; STDL_HSpans(dst, hsp, 32, 5); }
static void op_vspans(int i)   { (void)i; STDL_VSpans(dst, vsp, 32, 5); }
static void op_points(int i)   { (void)i; STDL_Points(dst, pts, 100, 5); }
static void op_pointsc(int i)  { (void)i; STDL_PointsC(dst, pts, ptcols, 100); }
static void op_pixels(int i)
{
    int k;
    (void)i;
    for (k = 0; k < 100; k++) {
        STDL_PutPixel(dst, pts[k].x, pts[k].y, 5);
    }
}
static void op_i8_16(int i)
{
    STDL_BlitIndexed8(dst, chunky, 32, 67 + (i & 7), 40, 16, 16, map, 0);
}
static void op_i8_32(int i)
{
    STDL_BlitIndexed8(dst, chunky, 32, 67 + (i & 7), 40, 32, 32, map, 0);
}
static void op_xvspans3(int i) { (void)i; STDL_XorVSpans(dst, vsp, 32, 3); }
static void op_xvspans5(int i) { (void)i; STDL_XorVSpans(dst, vsp, 32, 5); }
static void op_xvline(int i)   { (void)i; STDL_XorVLine(dst, 77, 20, 69, 5); }
static void op_xvline3(int i)  { (void)i; STDL_XorVLine(dst, 77, 20, 69, 3); }
static void op_xhline(int i)   { (void)i; STDL_XorHLine(dst, 37, 68, 60, 5); }
static void op_xor(int i)
{
    STDL_Rect r = { 37, 30, 32, 32 };
    (void)i;
    STDL_XorRect(dst, &r, 5);
}

static uint32_t measure(op_fn f)
{
    uint32_t n = 2, t;

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

int main(void)
{
    int k;

    if (STDL_Init(STDL_INIT_VIDEO) < 0
        || STDL_SetVideoMode(320, 200, 4, 0) == NULL) {
        return 1;
    }
    have_blitter = STDL_GetMachineInfo()->has_blitter;
    dst = STDL_CreateSurface(320, 200);
    tsrc = STDL_CreateSurface(32, 32);
    ksrc16 = STDL_CreateSurface(16, 16);
    ksrc32 = STDL_CreateSurface(32, 32);
    randomise(dst, 16);
    randomise(tsrc, 16);
    randomise(ksrc16, 6);
    randomise(ksrc32, 6);
    STDL_SetColourKey(ksrc16, 1, 3);
    STDL_SetColourKey(ksrc32, 1, 3);
    tiles_plain = STDL_TilesetFromSurface(tsrc, 16, 16);
    STDL_SetColourKey(tsrc, 1, 3);
    tiles_masked = STDL_TilesetFromSurface(tsrc, 16, 16);

    /* an 8x8 font over ASCII 32-127, any pattern will do */
    font.cw = 8;
    font.ch = 8;
    font.first = 32;
    font.last = 127;
    font.bytes_per_row = 1;
    font.bits = malloc(96 * 8);
    for (k = 0; k < 96 * 8; k++) {
        font.bits[k] = (uint8_t)rnd();
    }
    for (k = 0; k < 100; k++) {
        pts[k].x = (int16_t)(rnd() % 320);
        pts[k].y = (int16_t)(rnd() % 200);
        ptcols[k] = (uint8_t)(rnd() & 15);
    }
    for (k = 0; k < 32; k++) {
        hsp[k].x = (int16_t)(rnd() % 300);
        hsp[k].y = (int16_t)(rnd() % 200);
        hsp[k].len = 8;
        vsp[k].x = (int16_t)(rnd() % 320);
        vsp[k].y = (int16_t)(rnd() % 190);
        vsp[k].len = 8;
    }
    for (k = 0; k < 32 * 32; k++) {
        chunky[k] = (uint8_t)(rnd() % 5);
    }
    for (k = 0; k < 16; k++) {
        map[k] = (uint8_t)k;
    }
    printf("SC: machine blitter=%d\n", have_blitter);
    printf("SC: units: microseconds per operation\n");
    fflush(stdout);

    ndsp = spans_of(diamond, 4, dsp);
    ntsp = spans_of(tri, 3, tsp);
    for (pass_name = 'A'; pass_name <= 'B'; pass_name++) {
        report("tile 16x16", op_tile);
        report("tile 16x16 masked", op_tilem);
        report("tile row 20 x 16x16", op_tilerow);
        report("keyed blit 16x16 aligned", op_key16);
        report("keyed blit 32x32 aligned", op_key32);
        report("text 10 chars 8x8", op_text);
        report("drawchar x10 8x8", op_chars);
        report("text 10 chars 8x8 x=16", op_text8);
        report("drawchar x10 8x8 x=16", op_chars8);
        report("hline 32", op_hline32);
        report("hline 300", op_hline300);
        report("vline 50", op_vline);
        report("line 100x60", op_line);
        report("fillcircle r12", op_fcircle);
        report("circle r20", op_circle);
        report("hspans 32 x 8", op_hspans);
        report("vspans 32 x 8", op_vspans);
        report("points 100", op_points);
        report("pointsc 100", op_pointsc);
        report("putpixel x100", op_pixels);
        report("indexed8 16x16", op_i8_16);
        report("indexed8 32x32", op_i8_32);
        report("xorrect 32x32", op_xor);
        report("xorvspans 32 x 8 c3", op_xvspans3);
        report("xorvspans 32 x 8 c5", op_xvspans5);
        report("xorvline 50", op_xvline);
        report("xorvline 50 c3", op_xvline3);
        report("xorhline 32", op_xhline);
        report("polygon 160 rows xor", op_poly_d);
        report("xorhspans same 159", op_spans_d);
        report("polygon 22 rows xor", op_poly_t);
        report("xorhspans same 21", op_spans_t);
        report("polygon 160 rows set", op_poly_set);
    }
    printf("SC:done\n");
    fflush(stdout);
    STDL_Quit();
    return 0;
}
