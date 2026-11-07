/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * What scrolling a tile map costs where there is no hardware to do it:
 * a plain ST, an STE that leaves its scroll registers alone, and the
 * 8-plane screen of a TT or a Falcon (stdl_responsive.h), which has no
 * hardware scrolling at all. Each case is one frame's work on a
 * 320x200 screen of 16x16 tiles, timed in milliseconds a frame:
 *
 *   - a whole redraw from the tile map, the baseline;
 *   - a coarse scroll in place: the page shifted a group sideways or
 *     N rows up or down, and the strip that comes in drawn;
 *   - a window copied from a background held as a ring (the tiles of
 *     the world at their positions modulo its size), split where the
 *     window crosses the ring's seams;
 *   - smooth scrolling: the window copied at any x through the shift
 *     chain; 16 rings each pre-shifted a pixel further, an aligned
 *     copy from whichever matches x, with the column a ring lacks
 *     drawn by unaligned blits from the tiles, copied shifted from
 *     ring 0, or drawn as the tiles' pre-shifted sprites; a
 *     whole-page 1-pixel ROXL; and a whole redraw from pre-shifted
 *     tile sprites;
 *   - a whole-page copy, and a frame's sprites drawn and restored.
 *
 * Everything goes through STDL's public calls but three kinds of
 * routine marked "raw", which a game would have to write itself:
 * memmove for a shift the library does not promise, a movem copy of a
 * window from a wider buffer, and the ROXL. The movem copy is the
 * floor the library's own window copy is held to: since November 2026
 * STDL_BlitSurface copies whole-group rows with the same movem loop
 * (src/rowcopy.c), where it had a memcpy call a row (38.0ms on a plain
 * ST against this loop's 19.5), so the two lines should now agree to
 * within the library's call. The ROXL is the same for the 8-plane
 * shifted copy (src/planes8row.c).
 * In-place shifts read and write the page through surfaces borrowed
 * over it (STDL_CreateSurfaceFrom, ...From8Planes) offset by a group
 * or by N rows.
 *
 * The method is SPRCOST's and PRIMCOST's: each case runs until it has
 * taken MINTICKS of the 200Hz clock, twice per process (passes A and
 * B, the same-binary floor), with the BLiTTER allowed ("lib") beside
 * the CPU forced ("cpu") where there is one; tests/hatari/sccmp.py
 * compares two logs. A scroller a pixel a frame costs differently
 * frame by frame (a new column every sixteenth, a window across the
 * ring's seam or not), so those cases time each frame on the 38400Hz
 * clock over the ring's whole period, 336 frames, and report the mean
 * and the worst frame. Then every technique is checked: it is
 * run from a known picture for several frames and the page compared,
 * pixel by pixel through STDL_GetPixel, with the world it should show
 * (a table of the tiles' pixels and the map, no blit involved). Twice
 * on a machine with a BLiTTER, as the two paths are different code.
 * Two screenshots of scrolled pictures follow ("SC:shot", a key goes
 * on), and on a TT what Flip's present costs after a frame that
 * changed the whole screen. -DSC_BREAK leaves a row out of two of the
 * techniques, to see their checks fail. A blit of a surface onto
 * itself goes the way its overlap needs (docs/limits.md), so every
 * check counts on every path: rightwards and downwards by a group or
 * rows, and rightwards by a pixel and by 8 within the same rows, which
 * go in columns from the right no wider than the move.
 *
 * Build by hand; keep the stem to eight characters:
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Iinclude -Ilib/xpad/src -o dist/SCRLCOST.TOS \
 *       tests/hatari/scrlcost.c libstdl.a
 *
 * and run it cycle-exact, MACHINE=st first, then ste and megaste (the
 * sixteen rings take 602K on 4 planes and 1.2MB on 8, hence 4MB; each
 * waitfor gives up after two minutes of host time, hence the chain):
 *
 *   SDL_VIDEODRIVER=dummy TOS=$PWD/tmp/tos/etos256uk.img MACHINE=st \
 *       EXTRA="--memsize 4 --cpu-exact on --compatible on \
 *       --confirm-quit false" tests/hatari/run.sh scst dist/SCRLCOST.TOS \
 *       8 "waitfor SC:A 16 rings;waitfor SC:B pass;waitfor SC:B 16 rings;\
 *       waitfor SC:checks;waitfor SC:shot 1;shot;key 0x39;\
 *       waitfor SC:shot 2;shot;key 0x39;waitfor SC:done"
 *
 * On a TT (TOS 3.06 or etos512uk.img, MACHINE=tt) and a Falcon (TOS
 * 4.04 or etos512uk.img, MACHINE=falcon MONITOR=vga; run.sh adds --mmu
 * off and --fpu none) it asks for the colour and runs the same cases on
 * 8 planes. The numbers in AGENTS.md and the skill are EmuTOS's on
 * every machine: a TT's moved by up to 10% between the two TOSes.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdl/stdl.h>
#include <stdl/stdl_bench.h>
#include "../../examples/hold.h"

#define MINTICKS 100            /* 0.5s of the 200Hz clock per case */

#define MX 64                   /* the world, in tiles: 1024 x 512 */
#define MY 32
#define RW 336                  /* a ring: 21 tile columns ...      */
#define RH 224                  /* ... by 14 tile rows              */
#define RCOLS (RW / 16)
#define RROWS (RH / 16)
#define NRINGS 16

static STDL_Surface *page, *sheet, *flat, *wide, *bobsrc;
static STDL_Tileset *tiles;
static STDL_Sprite *ptiles, *bob, *bobp;
static STDL_Surface *ring[NRINGS];
static int ring_o[NRINGS];
/* the tile columns and rows a ring holds; pc0 > pc1 when it holds none */
static int pc0[NRINGS], pc1[NRINGS], pr0[NRINGS], pr1[NRINGS];
static uint8_t map[MY][MX];
static uint8_t tpix[16][16][16];
static int planes, gbytes, have_blitter;
static int camx, camy;
static char pass_name;
static unsigned long fails;

/* surfaces borrowed over the page, for the shifts in place */
static STDL_Surface *al_ls, *al_ld, *al_rs, *al_rd;
static STDL_Surface *al_us[3], *al_ud[3], *al_ds[3], *al_dd[3];
static const int vn[3] = { 1, 4, 16 };

static uint32_t rng = 0x2468ACE1UL;
static uint32_t rnd(void)
{
    rng = rng * 1103515245UL + 12345UL;
    return (rng >> 16) & 0x7FFF;
}

/* ------------------------------------------------------------------ */
/* the raw routines                                                   */

#ifdef __m68k__
/*
 * Rows of 160 or 320 bytes copied ten longs a movem, unrolled a row:
 * (uint8_t *dst, const uint8_t *src, long dstride, long sskip, long
 * rows), sskip the source's stride less the row. 4.6 cycles a byte on
 * a 68000, as mintlib's memcpy of one block, without a call a row.
 */
#define SC_PAIR(off) \
    "\tmovem.l (%a0)+,%d1-%d7/%a2-%a4\n" \
    "\tmovem.l %d1-%d7/%a2-%a4," #off "(%a1)\n"
#define SC_COPYFN(name, body) \
    __asm__("\t.text\n\t.even\n\t.globl _" name "\n_" name ":\n" \
            "\tmovem.l %d2-%d7/%a2-%a6,-(%sp)\n" \
            "\tmove.l 48(%sp),%a1\n" \
            "\tmove.l 52(%sp),%a0\n" \
            "\tmove.l 56(%sp),%a6\n" \
            "\tmove.l 60(%sp),%a5\n" \
            "\tmove.l 64(%sp),%d0\n" \
            "\tsubq.w #1,%d0\n" \
            "1:\n" body \
            "\tadda.l %a5,%a0\n" \
            "\tadda.l %a6,%a1\n" \
            "\tdbf %d0,1b\n" \
            "\tmovem.l (%sp)+,%d2-%d7/%a2-%a6\n" \
            "\trts\n");
SC_COPYFN("sc_copy160",
          SC_PAIR(0) SC_PAIR(40) SC_PAIR(80) SC_PAIR(120))
SC_COPYFN("sc_copy320",
          SC_PAIR(0) SC_PAIR(40) SC_PAIR(80) SC_PAIR(120)
          SC_PAIR(160) SC_PAIR(200) SC_PAIR(240) SC_PAIR(280))

/*
 * The page one pixel to the left in place: each plane's 20 words of a
 * row rotated through X from the right, a memory ROXL each (16 cycles
 * on a 68000). (uint8_t *pix, long stride, long rows); the new
 * rightmost column comes in as colour 0.
 */
#define SC_R(off) "\troxl.w " #off "(%a1)\n"
#define SC_ROXLFN(name, nplanes, body) \
    __asm__("\t.text\n\t.even\n\t.globl _" name "\n_" name ":\n" \
            "\tmove.l %d2,-(%sp)\n" \
            "\tmove.l 8(%sp),%a0\n" \
            "\tmove.l 12(%sp),%d1\n" \
            "\tmove.l 16(%sp),%d0\n" \
            "\tsubq.w #1,%d0\n" \
            "1:\n" \
            "\tmove.l %a0,%a1\n" \
            "\tmoveq #" nplanes "-1,%d2\n" \
            "2:\n" \
            "\tandi.b #0xef,%ccr\n" body \
            "\taddq.l #2,%a1\n" \
            "\tdbf %d2,2b\n" \
            "\tadda.l %d1,%a0\n" \
            "\tdbf %d0,1b\n" \
            "\tmove.l (%sp)+,%d2\n" \
            "\trts\n");
SC_ROXLFN("sc_roxl4", "4",
          SC_R(152) SC_R(144) SC_R(136) SC_R(128) SC_R(120) SC_R(112)
          SC_R(104) SC_R(96) SC_R(88) SC_R(80) SC_R(72) SC_R(64)
          SC_R(56) SC_R(48) SC_R(40) SC_R(32) SC_R(24) SC_R(16)
          SC_R(8) SC_R(0))
SC_ROXLFN("sc_roxl8", "8",
          SC_R(304) SC_R(288) SC_R(272) SC_R(256) SC_R(240) SC_R(224)
          SC_R(208) SC_R(192) SC_R(176) SC_R(160) SC_R(144) SC_R(128)
          SC_R(112) SC_R(96) SC_R(80) SC_R(64) SC_R(48) SC_R(32)
          SC_R(16) SC_R(0))
void sc_copy160(uint8_t *dst, const uint8_t *src, long dstride,
                long sskip, long rows);
void sc_copy320(uint8_t *dst, const uint8_t *src, long dstride,
                long sskip, long rows);
void sc_roxl4(uint8_t *pix, long stride, long rows);
void sc_roxl8(uint8_t *pix, long stride, long rows);
#else
/* C twins, for reading: this probe is built for the machine only */
static void sc_copyrow(uint8_t *dst, const uint8_t *src, long dstride,
                       long sskip, long rows, int bytes)
{
    for (; rows > 0; rows--) {
        memcpy(dst, src, (size_t)bytes);
        src += bytes + sskip;
        dst += dstride;
    }
}
static void sc_copy160(uint8_t *d, const uint8_t *s, long ds, long sk,
                       long rows)
{
    sc_copyrow(d, s, ds, sk, rows, 160);
}
static void sc_copy320(uint8_t *d, const uint8_t *s, long ds, long sk,
                       long rows)
{
    sc_copyrow(d, s, ds, sk, rows, 320);
}
static void sc_roxl(uint8_t *pix, long stride, long rows, int np)
{
    int p, g;

    for (; rows > 0; rows--, pix += stride) {
        for (p = 0; p < np; p++) {
            unsigned x = 0;

            for (g = 19; g >= 0; g--) {
                uint8_t *w = pix + g * np * 2 + p * 2;
                const unsigned v = ((unsigned)w[0] << 8) | w[1];
                const unsigned n = ((v << 1) | x) & 0xFFFFu;

                x = v >> 15;
                w[0] = (uint8_t)(n >> 8);
                w[1] = (uint8_t)n;
            }
        }
    }
}
static void sc_roxl4(uint8_t *p, long s, long r) { sc_roxl(p, s, r, 4); }
static void sc_roxl8(uint8_t *p, long s, long r) { sc_roxl(p, s, r, 8); }
#endif

/* ------------------------------------------------------------------ */
/* the world                                                          */

static int tile_at(int tx, int ty)
{
    return map[ty & (MY - 1)][tx & (MX - 1)];
}

static uint8_t world_pix(int wx, int wy)
{
    return tpix[tile_at(wx >> 4, wy >> 4)][wy & 15][wx & 15];
}

/* a grid line on the top and left of each tile, an X, and a 4-pixel
 * chequer in two colours a tile: every pixel of a scrolled picture is
 * checked, and a group, a pixel or a row out shows */
static uint8_t tile_colour(int t, int x, int y)
{
    const int top = planes == 8 ? 255 : 15;

    if (x == 0 || y == 0) {
        return 0;
    }
    if (x == y || x + y == 16) {
        return (uint8_t)top;
    }
    if ((((x >> 2) ^ (y >> 2)) & 1) != 0) {
        return (uint8_t)(1 + (t * 37) % (top - 1));
    }
    return (uint8_t)(1 + (t * 91 + 7) % (top - 1));
}

/* v mod n for v >= 0, with the 68000's own divide */
static int slot(int v, int n)
{
    return v - (int)STDL_MulU16(STDL_DivU16((uint32_t)v, (unsigned)n),
                                (unsigned)n);
}

static void redraw_at(STDL_Surface *d, int cx, int cy)
{
    const int tx0 = cx >> 4, ty0 = cy >> 4;
    int r, c, y;

    for (r = 0, y = -(cy & 15); y < d->h; r++, y += 16) {
        for (c = 0; c < (d->w + 15) >> 4; c++) {
            STDL_BlitTile(tiles, tile_at(tx0 + c, ty0 + r), d, c * 16, y);
        }
    }
}

/* one column of tiles, world column tx, at page x */
static void column(int x, int tx)
{
    const int ty0 = camy >> 4;
    int r, y;

    for (r = 0, y = -(camy & 15); y < 200; r++, y += 16) {
        STDL_BlitTile(tiles, tile_at(tx, ty0 + r), page, x, y);
    }
}

/* page rows [sy, sy + n), from the world at the camera */
static void strip(int sy, int n)
{
    const STDL_Rect clip = { 0, (int16_t)sy, 320, (uint16_t)n };
    const int tx0 = camx >> 4;
    int ty, c;

    STDL_SetClipRect(page, &clip);
    for (ty = (camy + sy) >> 4; ty <= (camy + sy + n - 1) >> 4; ty++) {
        for (c = 0; c < 20; c++) {
            STDL_BlitTile(tiles, tile_at(tx0 + c, ty), page, c * 16,
                          ty * 16 - camy);
        }
    }
    STDL_SetClipRect(page, NULL);
}

/* an 8-plane screen hears what was written to it behind its back */
static void touch_screen(void)
{
    if (planes == 8) {
        STDL_UpdateRects(0, NULL);
    }
}

/* ------------------------------------------------------------------ */
/* surfaces over the page                                             */

static STDL_Surface *alias(uint8_t *p, int w, int h)
{
    return planes == 8
        ? STDL_CreateSurfaceFrom8Planes(p, w, h, page->stride, NULL, 0)
        : STDL_CreateSurfaceFrom(p, w, h, page->stride, NULL, 0);
}

static void free_aliases(void)
{
    int k;

    STDL_FreeSurface(al_ls);
    STDL_FreeSurface(al_ld);
    STDL_FreeSurface(al_rs);
    STDL_FreeSurface(al_rd);
    for (k = 0; k < 3; k++) {
        STDL_FreeSurface(al_us[k]);
        STDL_FreeSurface(al_ud[k]);
        STDL_FreeSurface(al_ds[k]);
        STDL_FreeSurface(al_dd[k]);
    }
}

/*
 * Left: the page from its second group onto itself, 320 wide, so that
 * on 4 planes it is the whole-surface copy (one memcpy, or the
 * BLiTTER); it reads a group past the page's end, never writes there.
 * Right: 304 wide, rows moved right in place (src/overlap.c). Up and
 * down: the page's rows below N onto those above, and back.
 */
static void make_aliases(void)
{
    uint8_t *p;
    int k;

    page = STDL_GetVideoSurface();
    p = page->pixels;
    al_ls = alias(p + gbytes, 320, 200);
#ifdef SC_BREAK
    al_ld = alias(p, 320, 199);
#else
    al_ld = alias(p, 320, 200);
#endif
    al_rs = alias(p, 304, 200);
    al_rd = alias(p + gbytes, 304, 200);
    for (k = 0; k < 3; k++) {
        const long off = (long)vn[k] * page->stride;

        al_us[k] = alias(p + off, 320, 200 - vn[k]);
        al_ud[k] = alias(p, 320, 200 - vn[k]);
        al_ds[k] = alias(p, 320, 200 - vn[k]);
        al_dd[k] = alias(p + off, 320, 200 - vn[k]);
    }
}

/* ------------------------------------------------------------------ */
/* coarse scrolling in place: one frame each                          */

static void shift_left(void)
{
    STDL_BlitSurface(al_ls, NULL, al_ld, NULL);
    touch_screen();
}

static void step_left(void)
{
    shift_left();
    camx += 16;
    column(304, (camx >> 4) + 19);
}

static void step_right_blit(void)
{
    STDL_BlitSurface(al_rs, NULL, al_rd, NULL);
    touch_screen();
    camx -= 16;
    column(0, camx >> 4);
}

static void step_right_raw(void)
{
    uint8_t *p = page->pixels;

    if (page->stride == 20 * gbytes) {
        memmove(p + gbytes, p, (size_t)(200 * page->stride - gbytes));
    } else {
        int r;

        for (r = 0; r < 200; r++, p += page->stride) {
            memmove(p + gbytes, p, (size_t)(19 * gbytes));
        }
    }
    touch_screen();
    camx -= 16;
    column(0, camx >> 4);
}

/* 304 wide from x = 8 onto itself k pixels right, the page one
 * surface: columns from the right, none wider than k */
static void shift_right_by(int k)
{
    STDL_Rect r, at;

    r.x = 8;
    r.y = 0;
    r.w = 304;
    r.h = 200;
    at.x = (int16_t)(8 + k);
    at.y = 0;
    STDL_BlitSurface(page, &r, page, &at);
    touch_screen();
}

static void step_up(int k)
{
    STDL_BlitSurface(al_us[k], NULL, al_ud[k], NULL);
    touch_screen();
    camy += vn[k];
    strip(200 - vn[k], vn[k]);
}

static void step_down_blit(int k)
{
    STDL_BlitSurface(al_ds[k], NULL, al_dd[k], NULL);
    touch_screen();
    camy -= vn[k];
    strip(0, vn[k]);
}

static void step_down_raw(int k)
{
    const int n = vn[k];
    const long off = (long)n * page->stride;
    uint8_t *p = page->pixels;

    if (page->stride == 20 * gbytes) {
        memmove(p + off, p, (size_t)((200 - n) * page->stride));
    } else {
        /* rows apart: each a copy of its own, the bottom first */
        uint8_t *d = p + 199L * page->stride;
        int r;

        for (r = 199; r >= n; r--, d -= page->stride) {
            memcpy(d, d - off, (size_t)(20 * gbytes));
        }
    }
    touch_screen();
    camy -= n;
    strip(0, n);
}

/* ------------------------------------------------------------------ */
/* rings                                                              */

static void ring_reset(int k)
{
    pc0[k] = 1;
    pc1[k] = 0;
    pr0[k] = 1;
    pr1[k] = 0;
}

/* how a ring offset by ring_o gets its tiles */
#define BY_BLIT    0            /* an unaligned 16x16 blit from the sheet */
#define BY_RING0   1            /* a column copied, shifted, from ring 0 */
#define BY_SPRITE  2            /* the tile as a pre-shifted sprite */

/* world tile (tx, ty) into ring k: an aligned tile on ring 0; on a
 * ring offset by ring_o, an unaligned blit or a pre-shifted sprite,
 * twice where it straddles the ring's left seam */
static void ring_put(int k, int tx, int ty, int how)
{
    const int rx = slot(tx, RCOLS) * 16 - ring_o[k];
    const int ry = slot(ty, RROWS) * 16;
    STDL_Rect s, d;

    if (ring_o[k] == 0) {
        STDL_BlitTile(tiles, tile_at(tx, ty), ring[k], rx, ry);
        return;
    }
    if (how == BY_SPRITE) {
        STDL_BlitSprite(ptiles, tile_at(tx, ty), ring[k], rx, ry);
        if (rx < 0) {
            STDL_BlitSprite(ptiles, tile_at(tx, ty), ring[k], rx + RW, ry);
        }
        return;
    }
    s.x = (int16_t)(tile_at(tx, ty) * 16);
    s.y = 0;
    s.w = 16;
    s.h = 16;
    d.x = (int16_t)rx;
    d.y = (int16_t)ry;
    STDL_BlitSurface(sheet, &s, ring[k], &d);
    if (rx < 0) {
        d.x = (int16_t)(rx + RW);
        d.y = (int16_t)ry;
        STDL_BlitSurface(sheet, &s, ring[k], &d);
    }
}

/* the same for a whole column of rows r0..r1 (not wrapping in the
 * ring), as one unaligned blit from ring 0, which holds it */
static void ring_col_from0(int k, int tx, int r0, int r1)
{
    const int sx = slot(tx, RCOLS) * 16, rx = sx - ring_o[k];
    STDL_Rect s, d;

    s.x = (int16_t)sx;
    s.y = (int16_t)(slot(r0, RROWS) * 16);
    s.w = 16;
    s.h = (uint16_t)((r1 - r0 + 1) * 16);
    d.x = (int16_t)rx;
    d.y = s.y;
    STDL_BlitSurface(ring[0], &s, ring[k], &d);
    if (rx < 0) {
        d.x = (int16_t)(rx + RW);
        d.y = s.y;
        STDL_BlitSurface(ring[0], &s, ring[k], &d);
    }
}

/* bring ring k up to tile columns c0..c1, rows r0..r1: draw what it
 * does not hold yet (at most 21 x 14, so a slot holds one tile) */
static void ring_update(int k, int c0, int c1, int r0, int r1, int how)
{
    const int from0 = how == BY_RING0 && k != 0;
    int c, r;

    if (from0) {
        ring_update(0, c0, c1, r0, r1, BY_BLIT);
    }
    for (c = c0; c <= c1; c++) {
        const int hasc = c >= pc0[k] && c <= pc1[k];

        if (from0) {
            /* rows fixed: a column is there or not */
            if (!hasc || r0 != pr0[k] || r1 != pr1[k]) {
                ring_col_from0(k, c, r0, r1);
            }
            continue;
        }
        for (r = r0; r <= r1; r++) {
            if (hasc && r >= pr0[k] && r <= pr1[k]) {
                continue;
            }
            ring_put(k, c, r, how);
        }
    }
    pc0[k] = c0;
    pc1[k] = c1;
    pr0[k] = r0;
    pr1[k] = r1;
}

static void piece(STDL_Surface *src, int sx, int sy, int w, int h,
                  int dx, int dy)
{
    STDL_Rect s, d;

    if (w <= 0 || h <= 0) {
        return;
    }
    s.x = (int16_t)sx;
    s.y = (int16_t)sy;
    s.w = (uint16_t)w;
    s.h = (uint16_t)h;
    d.x = (int16_t)dx;
    d.y = (int16_t)dy;
    STDL_BlitSurface(src, &s, page, &d);
}

/* the window at world (wx, wy) from ring k into the page: up to four
 * pieces, split where it crosses the ring's seams */
static void ring_copy(int k, int wx, int wy)
{
    const int rx = slot(wx - ring_o[k], RW), ry = slot(wy, RH);
    const int w1 = RW - rx < 320 ? RW - rx : 320;
#ifdef SC_BREAK
    const int h1 = (RH - ry < 200 ? RH - ry : 200) - 1;
#else
    const int h1 = RH - ry < 200 ? RH - ry : 200;
#endif

    piece(ring[k], rx, ry, w1, h1, 0, 0);
    piece(ring[k], 0, ry, 320 - w1, h1, w1, 0);
    piece(ring[k], rx, 0, w1, 200 - h1, 0, h1);
    piece(ring[k], 0, 0, 320 - w1, 200 - h1, w1, h1);
}

/* the frame of a game drawing from ring 0: what came into view drawn
 * into the ring, then the window copied, at any x */
static void ring_show(int wx, int wy)
{
    ring_update(0, wx >> 4, (wx + 319) >> 4, wy >> 4, (wy + 199) >> 4,
                BY_BLIT);
    ring_copy(0, wx, wy);
}

/* the frame of a game with a ring for each pixel phase: the one whose
 * offset matches x, an aligned copy; its new column drawn as pre_how
 * says */
static int pre_how = BY_RING0;

static void preshift_show(int wx)
{
    const int k = wx & 15;

    ring_update(k, wx >> 4, (wx + 319) >> 4, 0, 12, pre_how);
    ring_copy(k, wx, 0);
}

/* ------------------------------------------------------------------ */
/* the timed cases                                                    */

typedef void (*op_fn)(int i);

static void op_redraw(int i) { (void)i; camx = 0; camy = 0; redraw_at(page, 0, 0); }
static void op_column(int i) { camy = 0; column(304, i & 63); }
static void op_strip16(int i) { camy = (i & 7) * 16; strip(184, 16); }
static void op_strip4(int i) { camy = (i & 7) * 16; strip(196, 4); }
static void op_strip1(int i) { camy = (i & 7) * 16; strip(199, 1); }
static void op_shleft(int i) { (void)i; shift_left(); }
static void op_left(int i) { (void)i; camy = 0; step_left(); }
static void op_right_blit(int i) { (void)i; camx = 512; camy = 0; step_right_blit(); }
static void op_right_raw(int i) { (void)i; camx = 512; camy = 0; step_right_raw(); }
static void op_right1(int i) { (void)i; shift_right_by(1); }
static void op_right8(int i) { (void)i; shift_right_by(8); }
static void op_up1(int i) { (void)i; camx = 0; step_up(0); }
static void op_up4(int i) { (void)i; camx = 0; step_up(1); }
static void op_up16(int i) { (void)i; camx = 0; step_up(2); }
static void op_down1(int i) { (void)i; camx = 0; camy = 256; step_down_blit(0); }
static void op_down4(int i) { (void)i; camx = 0; camy = 256; step_down_blit(1); }
static void op_down16(int i) { (void)i; camx = 0; camy = 256; step_down_blit(2); }
static void op_down1r(int i) { (void)i; camx = 0; camy = 256; step_down_raw(0); }
static void op_down4r(int i) { (void)i; camx = 0; camy = 256; step_down_raw(1); }
static void op_down16r(int i) { (void)i; camx = 0; camy = 256; step_down_raw(2); }

static void op_whole(int i) { (void)i; STDL_BlitSurface(flat, NULL, page, NULL); }
static void op_win(int i) { (void)i; piece(wide, 16, 16, 320, 200, 0, 0); }
static void op_win_p1(int i) { (void)i; piece(wide, 17, 16, 320, 200, 0, 0); }
static void op_win_p8(int i) { (void)i; piece(wide, 24, 16, 320, 200, 0, 0); }
/* a panel at the same phase but off the groups, 8 to 311: its rows have
 * partial edges at both ends, as a playfield inset in a frame has */
static void op_win_pp(int i) { (void)i; piece(wide, 24, 16, 304, 200, 8, 0); }
static void op_win_raw(int i)
{
    const uint8_t *s = wide->pixels + 16L * wide->stride + gbytes;
    const long sskip = (long)wide->stride - 20 * gbytes;

    (void)i;
    if (planes == 8) {
        sc_copy320(page->pixels, s, page->stride, sskip, 200);
    } else {
        sc_copy160(page->pixels, s, page->stride, sskip, 200);
    }
    touch_screen();
}
/* the ring, at a place where the window crosses both seams */
static void op_ring4(int i) { (void)i; ring_copy(0, 160 + 4 * RW, 112 + RH); }
/* a ring scrolled right a pixel a frame, as a game would: its new
 * column of tiles every sixteenth frame, the window through the shift
 * chain fifteen frames in sixteen */
static int ring_x;
static void fr_ring(void) { ring_show(ring_x++, 0); }
/* sixteen rings, a pixel a frame: an aligned copy, and each frame one
 * ring's new column */
static int pre_x;
static void fr_pre_from0(void) { pre_how = BY_RING0; preshift_show(pre_x++); }
static void fr_pre_tiles(void) { pre_how = BY_BLIT; preshift_show(pre_x++); }
static void fr_pre_spr(void) { pre_how = BY_SPRITE; preshift_show(pre_x++); }
/* a shifted ring's column alone, each way */
static void op_col_tiles(int i)
{
    int r;

    for (r = 0; r < 13; r++) {
        ring_put(5, 30 + (i & 15), r, BY_BLIT);
    }
}
static void op_col_spr(int i)
{
    int r;

    for (r = 0; r < 13; r++) {
        ring_put(5, 30 + (i & 15), r, BY_SPRITE);
    }
}
static void op_col_from0(int i) { ring_col_from0(5, 30 + (i & 15), 0, 12); }

static int roxl_x;
static void roxl_frame(void)
{
    const int wx = camx + 319;
    STDL_Rect s, d;
    int r;

    if (planes == 8) {
        sc_roxl8(page->pixels, page->stride, 200);
    } else {
        sc_roxl4(page->pixels, page->stride, 200);
    }
    touch_screen();
    /* the new pixel column: a 1x16 cut of each tile it crosses */
    for (r = 0; r < 13; r++) {
        s.x = (int16_t)(tile_at(wx >> 4, r) * 16 + (wx & 15));
        s.y = 0;
        s.w = 1;
        s.h = 16;
        d.x = 319;
        d.y = (int16_t)(r * 16);
        STDL_BlitSurface(sheet, &s, page, &d);
    }
}
static void step_roxl(void) { camx++; roxl_frame(); }
static void op_roxl(int i) { (void)i; camx = roxl_x++; camy = 0; roxl_frame(); }
static void op_roxl_only(int i)
{
    (void)i;
    if (planes == 8) {
        sc_roxl8(page->pixels, page->stride, 200);
    } else {
        sc_roxl4(page->pixels, page->stride, 200);
    }
}

/* the tiles as pre-shifted sprites, the whole screen at any x */
static void redraw_sprites(int cx)
{
    const int p = cx & 15, tx0 = cx >> 4;
    int r, c;

    for (r = 0; r < 13; r++) {
        for (c = 0; c < 21; c++) {
            const int x = c * 16 - p;

            if (x < 320) {
                STDL_BlitSprite(ptiles, tile_at(tx0 + c, r), page, x,
                                r * 16);
            }
        }
    }
    touch_screen();
}
static void op_spr_redraw(int i) { (void)i; redraw_sprites(5); }

static const int16_t bobx[8] = { 21, 60, 99, 134, 170, 203, 241, 280 };
static const int16_t boby[8] = { 20, 41, 63, 87, 111, 133, 152, 170 };
static void op_bobs(int i)
{
    int k;

    (void)i;
    for (k = 0; k < 8; k++) {
        STDL_BlitSprite(bob, 0, page, bobx[k], boby[k]);
    }
}
static void op_bobsp(int i)
{
    int k;

    (void)i;
    for (k = 0; k < 8; k++) {
        STDL_BlitSprite(bobp, 0, page, bobx[k], boby[k]);
    }
}
static void op_bobs_dirty(int i)
{
    int k;

    (void)i;
    STDL_DirtyRestore(page);
    for (k = 0; k < 8; k++) {
        const STDL_Rect r = { bobx[k], boby[k], 16, 16 };

        STDL_BlitSprite(bobp, 0, page, bobx[k], boby[k]);
        STDL_DirtyPush(&r);
    }
}

/* until MINTICKS have gone from a tick's edge; hundredths of a ms */
static uint32_t measure(op_fn f)
{
    uint32_t n = 0, t0, t;

    f(0);
    t0 = STDL_GetHz200();
    while (STDL_GetHz200() == t0) {
    }
    t0 = STDL_GetHz200();
    do {
        f((int)n);
        n++;
        t = STDL_GetHz200() - t0;
    } while (t < MINTICKS);
    return (t * 500UL + 250UL) / n;
}

static void report(const char *label, op_fn f)
{
    uint32_t cpu, lib;

    STDL_UseBlitter(0);
    cpu = measure(f);
    if (have_blitter) {
        STDL_UseBlitter(1);
        lib = measure(f);
        printf("SC:%c %-34s lib=%4lu.%02lu cpu=%4lu.%02lu\n", pass_name,
               label, (unsigned long)(lib / 100),
               (unsigned long)(lib % 100), (unsigned long)(cpu / 100),
               (unsigned long)(cpu % 100));
    } else {
        printf("SC:%c %-34s cpu=%4lu.%02lu\n", pass_name, label,
               (unsigned long)(cpu / 100), (unsigned long)(cpu % 100));
    }
    fflush(stdout);
    STDL_UseBlitter(1);
}

/*
 * A scroller a pixel a frame: each frame timed alone on the 38400Hz
 * clock over a whole period of the ring (RW frames: every column in
 * every slot, every place of the seams), after a cycle's warming.
 * Hundredths of a millisecond, the mean; the worst frame in cyc_max.
 */
typedef void (*frame_fn)(void);
static uint32_t cyc_max;

static uint32_t hms(uint32_t counts)
{
    return counts * 125UL / 48UL;
}

static uint32_t measure_cycle(frame_fn f)
{
    uint32_t total = 0, mx = 0, t0, t;
    int k;

    for (k = 0; k < 16; k++) {
        f();
    }
    for (k = 0; k < RW; k++) {
        t0 = STDL_GetHz38400();
        f();
        t = STDL_GetHz38400() - t0;
        total += t;
        if (t > mx) {
            mx = t;
        }
    }
    cyc_max = hms(mx);
    return hms(total) / RW;
}

static void report_cycle(const char *label, frame_fn f)
{
    uint32_t cpu, lib, cmax, lmax;

    STDL_UseBlitter(0);
    cpu = measure_cycle(f);
    cmax = cyc_max;
    if (have_blitter) {
        STDL_UseBlitter(1);
        lib = measure_cycle(f);
        lmax = cyc_max;
        printf("SC:%c %-34s lib=%4lu.%02lu cpu=%4lu.%02lu\n", pass_name,
               label, (unsigned long)(lib / 100),
               (unsigned long)(lib % 100), (unsigned long)(cpu / 100),
               (unsigned long)(cpu % 100));
        printf("SC:%c %-34s worst frame lib=%lu.%02lu cpu=%lu.%02lu\n",
               pass_name, label, (unsigned long)(lmax / 100),
               (unsigned long)(lmax % 100), (unsigned long)(cmax / 100),
               (unsigned long)(cmax % 100));
    } else {
        printf("SC:%c %-34s cpu=%4lu.%02lu\n", pass_name, label,
               (unsigned long)(cpu / 100), (unsigned long)(cpu % 100));
        printf("SC:%c %-34s worst frame cpu=%lu.%02lu\n", pass_name,
               label, (unsigned long)(cmax / 100),
               (unsigned long)(cmax % 100));
    }
    fflush(stdout);
    STDL_UseBlitter(1);
}

/* ------------------------------------------------------------------ */
/* the checks                                                         */

/* the page against the world at wx,wy, but for columns x0..x1, which
 * show it k pixels further left (a move right in place, not redrawn) */
static void check_x(const char *what, int wx, int wy, int x0, int x1,
                    int k)
{
    unsigned long bad = 0;
    int x, y, fx = -1, fy = -1;

    for (y = 0; y < 200; y++) {
        for (x = 0; x < 320; x++) {
            const int o = x >= x0 && x <= x1 ? k : 0;

            if (STDL_GetPixel(page, x, y)
                != world_pix(wx + x - o, wy + y)) {
                if (bad++ == 0) {
                    fx = x;
                    fy = y;
                }
            }
        }
    }
    if (bad == 0) {
        printf("SC:check %-30s ok\n", what);
    } else {
        printf("SC:check %-30s BAD %lu pixels, first at %d,%d\n", what,
               bad, fx, fy);
        fails++;
    }
    fflush(stdout);
}

static void check(const char *what, int wx, int wy)
{
    check_x(what, wx, wy, 0, -1, 0);
}

static void wait_key(void)
{
    const uint32_t t0 = STDL_GetTicks();
    STDL_Event e;

    while (STDL_GetTicks() - t0 < 60000UL) {
        while (STDL_PollEvent(&e)) {
            if (e.type == STDL_KEYDOWN) {
                return;
            }
        }
        STDL_WaitVBL();
    }
}

/* the window just checked onto the screen for a screenshot: copied
 * again from its ring after the marker, which the console draws on the
 * page being drawn */
static void show(int n, int k, int wx, int wy)
{
    printf("SC:shot %d\n", n);
    fflush(stdout);
    ring_copy(k, wx, wy);
    STDL_Flip();
    wait_key();
    free_aliases();
    make_aliases();
}

static void checks(const char *path, int shots)
{
    char name[48];
    int k, s;

    printf("SC:checks with the %s\n", path);
    fflush(stdout);

    camx = 32;
    camy = 48;
    redraw_at(page, camx, camy);
    check("redraw", camx, camy);

    camx = 0;
    camy = 0;
    redraw_at(page, 0, 0);
    for (s = 0; s < 5; s++) {
        step_left();
    }
    check("left 16 (blit)", camx, camy);

    camx = 160;
    redraw_at(page, camx, camy);
    for (s = 0; s < 5; s++) {
        step_right_blit();
    }
    check("right 16 (blit, rows)", camx, camy);

    camx = 160;
    redraw_at(page, camx, camy);
    for (s = 0; s < 5; s++) {
        step_right_raw();
    }
    check("right 16 (memmove)", camx, camy);

    /* 8-311 moved right within its rows, by less than a group */
    redraw_at(page, camx, camy);
    shift_right_by(1);
    check_x("right 1 (blit, columns)", camx, camy, 9, 312, 1);
    redraw_at(page, camx, camy);
    shift_right_by(8);
    check_x("right 8 (blit, columns)", camx, camy, 16, 319, 8);

    for (k = 0; k < 3; k++) {
        camx = 16;
        camy = 0;
        redraw_at(page, camx, camy);
        for (s = 0; s < 5; s++) {
            step_up(k);
        }
        sprintf(name, "up %d (blit)", vn[k]);
        check(name, camx, camy);

        camy = 160;
        redraw_at(page, camx, camy);
        for (s = 0; s < 5; s++) {
            step_down_blit(k);
        }
        sprintf(name, "down %d (blit)", vn[k]);
        check(name, camx, camy);

        camy = 160;
        redraw_at(page, camx, camy);
        for (s = 0; s < 5; s++) {
            step_down_raw(k);
        }
        sprintf(name, "down %d (memmove)", vn[k]);
        check(name, camx, camy);
    }

    /* the ring: a walk over the world, aligned x, any y, through its
     * seams, then a pixel at a time both ways */
    ring_reset(0);
    {
        static const int16_t walk[][2] = {
            { 16, 0 }, { 48, 37 }, { 400, 37 }, { 400, 300 },
            { 352, 290 }, { 32, 290 }, { 32, 40 }, { 640, 8 }
        };

        for (s = 0; s < (int)(sizeof walk / sizeof walk[0]); s++) {
            ring_show(walk[s][0], walk[s][1]);
        }
        check("ring aligned walk", 640, 8);
        for (s = 0; s < 45; s++) {
            ring_show(641 + s, 8 + s);
        }
        check("ring 1px right/down", 685, 52);
        for (s = 0; s < 37; s++) {
            ring_show(685 - s, 52 - s);
        }
        check("ring 1px left/up", 649, 16);
        if (shots) {
            show(1, 0, 649, 16);
        }
    }

    for (k = 0; k < NRINGS; k++) {
        ring_reset(k);
    }
    pre_how = BY_RING0;
    for (s = 16; s <= 56; s++) {
        preshift_show(s);
    }
    check("16 rings 1px (col from ring 0)", 56, 0);
    for (s = 56; s >= 23; s--) {
        preshift_show(s);
    }
    check("16 rings 1px back", 23, 0);
    if (shots) {
        show(2, 23 & 15, 23, 0);
    }
    for (k = 0; k < NRINGS; k++) {
        ring_reset(k);
    }
    pre_how = BY_BLIT;
    for (s = 16; s <= 56; s++) {
        preshift_show(s);
    }
    check("16 rings 1px (col from tiles)", 56, 0);
    for (k = 0; k < NRINGS; k++) {
        ring_reset(k);
    }
    pre_how = BY_SPRITE;
    for (s = 16; s <= 56; s++) {
        preshift_show(s);
    }
    check("16 rings 1px (col tile sprites)", 56, 0);
    for (s = 56; s >= 23; s--) {
        preshift_show(s);
    }
    check("16 rings 1px back (sprites)", 23, 0);

    camx = 0;
    camy = 0;
    redraw_at(page, 0, 0);
    for (s = 0; s < 21; s++) {
        step_roxl();
    }
    check("roxl 1px (raw)", camx, camy);

    redraw_sprites(37);
    check("pre-shifted tile sprites", 37, 0);

    STDL_BlitSurface(flat, NULL, page, NULL);
    check("whole copy (blit)", 0, 0);

    op_win(0);
    check("window copy (blit)", 16, 16);
    op_win_p1(0);
    check("window copy x+1 (blit)", 17, 16);
    /* the panel over a page cleared where it lands: its edges must
     * keep the window's 8 pixels each side */
    op_win(0);
    {
        STDL_Rect r = { 8, 0, 304, 200 };

        STDL_FillRect(page, &r, 0);
    }
    op_win_pp(0);
    check("panel 8-311 at its phase (blit)", 16, 16);
    op_win_raw(0);
    check("window copy (raw movem)", 16, 16);
}

/* what a TT's Flip copies to the display's second lines after a frame
 * that changed the whole screen, as every scrolling frame does, and
 * after one that changed a column */
static void present_cost(void)
{
    const STDL_PresentStats *st = STDL_GetPresentStats();
    uint32_t f0, t0;
    int k;

    if (st == NULL || !STDL_GetMachineInfo()->is_tt) {
        return;
    }
    STDL_Flip();
    f0 = st->flips;
    t0 = st->present_time;
    for (k = 0; k < 20; k++) {
        page = STDL_GetVideoSurface();
        STDL_BlitSurface(flat, NULL, page, NULL);
        STDL_Flip();
    }
    printf("SC: TT present, whole screen: %lu.%02lu ms a frame\n",
           (unsigned long)((st->present_time - t0) * 1000UL
                           / 38400UL / (st->flips - f0)),
           (unsigned long)((st->present_time - t0) * 100000UL
                           / 38400UL / (st->flips - f0) % 100));
    f0 = st->flips;
    t0 = st->present_time;
    for (k = 0; k < 20; k++) {
        page = STDL_GetVideoSurface();
        camy = 0;
        column(304, k);
        STDL_Flip();
    }
    printf("SC: TT present, one column: %lu.%02lu ms a frame\n",
           (unsigned long)((st->present_time - t0) * 1000UL
                           / 38400UL / (st->flips - f0)),
           (unsigned long)((st->present_time - t0) * 100000UL
                           / 38400UL / (st->flips - f0) % 100));
    fflush(stdout);
}

int main(void)
{
    const STDL_MachineInfo *mi;
    STDL_Colour pal[256];
    int k, x, y, t;

    hold_on_exit("SCRLCOST");

    if (STDL_Init(STDL_INIT_VIDEO) < 0) {
        return 1;
    }
    STDL_UseResponsive(STDL_RESPONSIVE_COLOUR);
    if (STDL_SetVideoMode(320, 200, 8, STDL_DOUBLEBUF) == NULL) {
        printf("SC: no screen: %s\nSC:done\n", STDL_GetError());
        return 1;
    }
    page = STDL_GetVideoSurface();
    planes = page->format->BitsPerPixel == 8 ? 8 : 4;
    gbytes = planes * 2;
    mi = STDL_GetMachineInfo();
    have_blitter = mi->has_blitter;
    if (planes == 8) {
        for (k = 0; k < 256; k++) {
            pal[k].r = (uint8_t)(k * 37);
            pal[k].g = (uint8_t)(k * 91);
            pal[k].b = (uint8_t)(k * 53);
            pal[k].unused = 0;
        }
        pal[0].r = pal[0].g = pal[0].b = 0;
        pal[255].r = pal[255].g = pal[255].b = 255;
        STDL_SetColours(page, pal, 0, 256);
    }

    for (t = 0; t < 16; t++) {
        for (y = 0; y < 16; y++) {
            for (x = 0; x < 16; x++) {
                tpix[t][y][x] = tile_colour(t, x, y);
            }
        }
    }
    for (y = 0; y < MY; y++) {
        for (x = 0; x < MX; x++) {
            map[y][x] = (uint8_t)(rnd() & 15);
        }
    }
    sheet = STDL_CreateSurface(256, 16);
    for (t = 0; t < 16; t++) {
        for (y = 0; y < 16; y++) {
            for (x = 0; x < 16; x++) {
                STDL_PutPixel(sheet, t * 16 + x, y, tpix[t][y][x]);
            }
        }
    }
    tiles = STDL_TilesetFromSurface(sheet, 16, 16);
    ptiles = STDL_SpriteFromSurface(sheet, 16, STDL_PRESHIFT);
    bobsrc = STDL_CreateSurface(16, 16);
    for (y = 0; y < 16; y++) {
        for (x = 0; x < 16; x++) {
            const int dx = 2 * x - 15, dy = 2 * y - 15;

            if (dx * dx + dy * dy < 225) {
                STDL_PutPixel(bobsrc, x, y, (uint8_t)(1 + ((x ^ y) & 7)));
            }
        }
    }
    STDL_SetColourKey(bobsrc, 1, 0);
    bob = STDL_SpriteFromSurface(bobsrc, 16, 0);
    bobp = STDL_SpriteFromSurface(bobsrc, 16, STDL_PRESHIFT);
    flat = STDL_CreateSurface(320, 200);
    wide = STDL_CreateSurface(352, 232);
    for (k = 0; k < NRINGS; k++) {
        ring[k] = STDL_CreateSurface(RW, RH);
        ring_o[k] = k;
        ring_reset(k);
    }
    if (tiles == NULL || ptiles == NULL || bob == NULL || bobp == NULL
        || flat == NULL || wide == NULL || ring[NRINGS - 1] == NULL) {
        printf("SC: out of memory: %s\nSC:done\n", STDL_GetError());
        STDL_Quit();
        return 1;
    }
    redraw_at(flat, 0, 0);
    redraw_at(wide, 0, 0);
    STDL_DirtyInit(flat, 32);
    make_aliases();

    printf("SC: machine planes=%d stride=%u blitter=%d tt=%d falcon=%d "
           "cpu=%u\n", planes, (unsigned)page->stride, have_blitter,
           mi->is_tt, mi->is_falcon, (unsigned)mi->cpu);
    printf("SC: a ring %dx%d is %lu bytes; 16 of them %lu\n", RW, RH,
           (unsigned long)ring[0]->stride * RH,
           (unsigned long)ring[0]->stride * RH * NRINGS);
    printf("SC: units: milliseconds per frame\n");
    fflush(stdout);

    /* the rings' steady state before they are timed */
    for (k = 0; k < 40; k++) {
        fr_ring();
        fr_pre_from0();
    }

    for (pass_name = 'A'; pass_name <= 'B'; pass_name++) {
        printf("SC:%c pass\n", pass_name);
        fflush(stdout);
        report("redraw 20x13 tiles", op_redraw);
        report("column 13 tiles", op_column);
        report("strip 20 tiles x16 rows", op_strip16);
        report("strip 20 tiles x4 rows", op_strip4);
        report("strip 20 tiles x1 row", op_strip1);
        report("left 16 shift only (blit)", op_shleft);
        report("left 16 + column (blit)", op_left);
        report("right 16 + column (blit rows)", op_right_blit);
        report("right 16 + column (memmove)", op_right_raw);
        report("right 1 shift only (blit, cols)", op_right1);
        report("right 8 shift only (blit, cols)", op_right8);
        report("up 1 + strip (blit)", op_up1);
        report("up 4 + strip (blit)", op_up4);
        report("up 16 + strip (blit)", op_up16);
        report("down 1 + strip (blit)", op_down1);
        report("down 4 + strip (blit)", op_down4);
        report("down 16 + strip (blit)", op_down16);
        report("down 1 + strip (memmove)", op_down1r);
        report("down 4 + strip (memmove)", op_down4r);
        report("down 16 + strip (memmove)", op_down16r);
        report("whole copy 320x200 (blit)", op_whole);
        report("window copy 320x200 (blit)", op_win);
        report("window copy (raw movem)", op_win_raw);
        report("ring copy 4 pieces (blit)", op_ring4);
        report("window copy x&15=1 (blit)", op_win_p1);
        report("window copy x&15=8 (blit)", op_win_p8);
        report("panel 304 at x&15=8 onto 8 (blit)", op_win_pp);
        report_cycle("ring 1px/frame (blit)", fr_ring);
        report_cycle("16 rings 1px, col from ring 0", fr_pre_from0);
        report_cycle("16 rings 1px, col from tiles", fr_pre_tiles);
        report_cycle("16 rings 1px, col tile sprites", fr_pre_spr);
        report("shifted col 13 tile blits", op_col_tiles);
        report("shifted col 13 tile sprites", op_col_spr);
        report("shifted col 16x208 from ring 0", op_col_from0);
        report("roxl 1px only (raw)", op_roxl_only);
        report("roxl 1px + column (raw)", op_roxl);
        report("pre-shifted tile sprites x&15=5", op_spr_redraw);
        report("8 sprites 16x16", op_bobs);
        report("8 sprites 16x16 preshifted", op_bobsp);
        report("8 sprites preshifted + restore", op_bobs_dirty);
    }

    fails = 0;
    checks(have_blitter ? "BLiTTER allowed" : "CPU", 1);
    if (have_blitter) {
        STDL_UseBlitter(0);
        checks("CPU forced", 0);
        STDL_UseBlitter(1);
    }
    present_cost();
    printf("SC:done %lu check(s) failed\n", fails);
    fflush(stdout);
    STDL_Quit();
    return 0;
}
