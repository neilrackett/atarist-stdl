/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: CC0-1.0
 *
 * Demo of the engine-porting APIs:
 *
 *  - STDL_CreateSurfaceFrom: the playfield is a plain caller-owned
 *    block (with a caller-owned mask plane), restored between
 *    frames with one memcpy from a pristine copy - the pattern an
 *    engine with its own framebuffers uses
 *  - STDL_BlitSurfaceEx: the third ball is baked into planar words
 *    once at startup and then composed every frame, the pattern for
 *    a frame you redraw often - STDL_BLIT_UNDER gives it the same
 *    "pass behind the pillars" behaviour the chunky blits get
 *  - STDL_BlitIndexed8: all the art here is byte-per-pixel (chunky)
 *    data generated at startup and drawn at frame rate through a
 *    16-entry colour map; the pillars are drawn once with
 *    STDL_I8_MARK so the bouncing sprites, drawn with
 *    STDL_I8_UNDER, pass behind them. One sprite is drawn
 *    STDL_I8_XFLIP, one STDL_I8_COLMAJOR (column-major storage)
 *  - STDL_Voice: a three-voice arpeggio sequenced from the 50Hz
 *    voice tick, playing a generated sawtooth sample (STE only;
 *    a plain ST runs silent)
 *  - STDL_GetHz200: the frame-rate bar at the bottom
 *
 * ESC or a joystick button exits.
 */

#include <stdio.h>
#include <string.h>
#include <stdl/stdl.h>

#define PF_W 320
#define PF_H 192
#define PF_STRIDE ((PF_W / 16) * 8)
#define PF_MASKSTRIDE ((PF_W / 16) * 2)
#define PF_BYTES (PF_STRIDE * PF_H)
#define PF_MASKBYTES (PF_MASKSTRIDE * PF_H)

/* caller-owned playfield: pixels + mask in single blocks, plus a
 * pristine copy for the per-frame restore */
static uint8_t pf_pixels[PF_BYTES];
static uint8_t pf_mask[PF_MASKBYTES];
static uint8_t bg_pixels[PF_BYTES];
static uint8_t bg_mask[PF_MASKBYTES];

/*
 * The baked ball: 24 pixels spans two 16-pixel groups, so a row is
 * 2*4 plane words plus 2 mask words. One group of slack sits at each
 * end because the unaligned blit path reads one group either side of
 * a row (see STDL_CreateSurfaceFrom).
 */
#define BALL_G 2
#define BALL_H 24
#define BALL_GUARD 4
static uint16_t baked[BALL_GUARD + BALL_G * 4 * BALL_H + BALL_GUARD];
static uint16_t baked_mask[BALL_GUARD + BALL_G * BALL_H + BALL_GUARD];

/* chunky art, generated at startup */
static uint8_t ball[24 * 24];
static uint8_t arrow[32 * 16];      /* stored column-major: 16 cols */
static uint8_t pillar[24 * 160];

static const uint8_t map_id[16] =
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
/* the baked ball wears greys and yellow so it is obvious which
 * sprite came from the pre-baked path */
static const uint8_t map_baked[16] =
    { 0, 1, 2, 3, 12, 13, 15, 7, 8, 9, 10, 11, 12, 13, 14, 15 };
static const uint8_t map_warm[16] =
    { 0, 9, 10, 11, 9, 10, 11, 9, 10, 11, 9, 10, 11, 9, 10, 11 };

static void make_art(void)
{
    int x, y;

    for (y = 0; y < 24; y++) {
        for (x = 0; x < 24; x++) {
            int dx = x - 12, dy = y - 12;
            int d2 = dx * dx + dy * dy;
            ball[y * 24 + x] = (uint8_t)(d2 < 121
                ? (d2 < 36 ? 6 : (d2 < 81 ? 5 : 4)) : 0);
        }
    }
    /* arrow, written column-major: arrow[col * 16 + row] */
    for (x = 0; x < 32; x++) {
        for (y = 0; y < 16; y++) {
            int half = y < 8 ? y : 15 - y;
            arrow[x * 16 + y] =
                (uint8_t)((x >= 24 - half * 3 && x < 32 - half * 2)
                          || (x < 20 && half > 4) ? 1 + (x & 3) : 0);
        }
    }
    for (y = 0; y < 160; y++) {
        for (x = 0; x < 24; x++) {
            int edge = (x < 2 || x >= 22);
            pillar[y * 24 + x] =
                (uint8_t)(edge ? 12 : 13 + ((x + y) & 1));
        }
    }
}

/* --- three-voice arpeggio, driven from the voice tick ------------ */

static int8_t saw[128];

/* an original little I-vi-IV-V loop, three notes per step */
static const uint16_t chords[4][3] = {
    { 262, 330, 392 }, { 220, 262, 330 },
    { 175, 220, 262 }, { 196, 247, 294 },
};

static void music_tick(void *ud)
{
    static int frame, step;
    int v;

    (void)ud;
    if (frame++ % 25 != 0) {        /* new chord twice a second */
        return;
    }
    for (v = 0; v < 3; v++) {
        /* sample plays its 128 bytes per period: freq = note * 128 */
        STDL_SetVoice(v, saw, sizeof(saw), 0, sizeof(saw),
                      (uint32_t)chords[step][v] * sizeof(saw),
                      (uint8_t)(40 - v * 8));
    }
    step = (step + 1) & 3;
}

int main(void)
{
    STDL_Surface *pf, *pf_bare, *screen, *ballspr;
    STDL_Rect r;
    int bx = 40, by = 30, bdx = 2, bdy = 2;
    int cx = 96, cy = 20, cdx = 3, cdy = 2;
    int ax = 240, ay = 150, adx = -2;
    int music, i;
    uint32_t fps_t0;
    int fps_frames = 0, fps = 0;

    STDL_Init(STDL_INIT_VIDEO | STDL_INIT_AUDIO | STDL_INIT_JOYSTICK);
    screen = STDL_SetVideoMode(320, 200, 4, 0);
    if (screen == NULL) {
        return 1;
    }
    make_art();
    for (i = 0; i < (int)sizeof(saw); i++) {
        saw[i] = (int8_t)(i - 64);
    }

    /* the playfield over caller-owned memory */
    pf = STDL_CreateSurfaceFrom(pf_pixels, PF_W, PF_H, PF_STRIDE,
                                pf_mask, PF_MASKSTRIDE);
    /*
     * A second, maskless view of the same pixels for the copy to
     * the screen: the masked view colour-keys on its mask, so a
     * whole-content copy through it would skip every MARKed pixel.
     * Two views of one block is the working pattern for an engine
     * framebuffer - compose through the masked one, present
     * through the bare one.
     */
    pf_bare = STDL_CreateSurfaceFrom(pf_pixels, PF_W, PF_H,
                                     PF_STRIDE, NULL, 0);
    if (pf == NULL || pf_bare == NULL) {
        fprintf(stderr, "CreateSurfaceFrom: %s\n", STDL_GetError());
        return 1;
    }

    /* backdrop: sky bands, ground, and MARKed foreground pillars */
    for (i = 0; i < 6; i++) {
        r.x = 0;
        r.y = (int16_t)(i * 24);
        r.w = PF_W;
        r.h = 24;
        STDL_FillRect(pf, &r, (uint8_t)(8 - i));
    }
    r.y = 144;
    r.h = PF_H - 144;
    STDL_FillRect(pf, &r, 9);
    for (i = 0; i < 3; i++) {
        STDL_BlitIndexed8(pf, pillar, 24, 60 + i * 90, 16,
                          24, 160, map_id, STDL_I8_MARK);
    }
    /* pristine copy for the restore: the surface is plain memory */
    memcpy(bg_pixels, pf_pixels, sizeof(bg_pixels));
    memcpy(bg_mask, pf_mask, sizeof(bg_mask));

    {
        STDL_Colour cols[16] = {
            { 0x00, 0x00, 0x22, 0 }, { 0xEE, 0x66, 0x22, 0 },
            { 0xEE, 0x88, 0x22, 0 }, { 0xEE, 0xAA, 0x44, 0 },
            { 0x66, 0x00, 0x00, 0 }, { 0xAA, 0x22, 0x22, 0 },
            { 0xEE, 0x66, 0x66, 0 }, { 0xFF, 0xFF, 0xFF, 0 },
            { 0x22, 0x44, 0x88, 0 }, { 0x33, 0x66, 0x33, 0 },
            { 0x77, 0xAA, 0x55, 0 }, { 0xAA, 0xEE, 0x88, 0 },
            { 0x11, 0x11, 0x11, 0 }, { 0x55, 0x55, 0x66, 0 },
            { 0x44, 0x44, 0x55, 0 }, { 0xEE, 0xEE, 0x44, 0 },
        };
        STDL_SetColours(screen, cols, 0, 16);
    }

    /*
     * Bake the ball once. The mask starts all ones (every pixel
     * transparent) and STDL_BlitIndexed8's default maintenance
     * clears a bit under each pixel it draws, which leaves exactly
     * the source convention: bit set = destination preserved.
     * Baking costs about what one chunky draw costs, so it only
     * pays for frames drawn more than once - which is this one.
     */
    ballspr = STDL_CreateSurfaceFrom(baked + BALL_GUARD, BALL_G * 16,
                                     BALL_H, BALL_G * 8,
                                     (uint8_t *)(baked_mask + BALL_GUARD),
                                     BALL_G * 2);
    if (ballspr == NULL) {
        fprintf(stderr, "bake: %s\n", STDL_GetError());
        return 1;
    }
    memset(baked_mask, 0xFF, sizeof(baked_mask));
    STDL_BlitIndexed8(ballspr, ball, 24, 0, 0, 24, 24, map_baked, 0);

    /* STE only; a plain ST just stays silent */
    music = (STDL_OpenVoices(12517) == 0);
    if (music) {
        STDL_SetVoiceTick(music_tick, NULL);
    }
    STDL_JoyKeyEmulation(1);

    fps_t0 = STDL_GetHz200();
    for (;;) {
        STDL_Event ev;
        int quit = 0;

        while (STDL_PollEvent(&ev)) {
            if (ev.type == STDL_QUIT
                || (ev.type == STDL_KEYDOWN
                    && ev.key.keysym.sym == STDLK_ESCAPE)) {
                quit = 1;
            }
        }
        if (quit) {
            break;
        }

        /* restore, move, draw behind the marked pillars */
        memcpy(pf_pixels, bg_pixels, sizeof(pf_pixels));
        memcpy(pf_mask, bg_mask, sizeof(pf_mask));

        bx += bdx;
        by += bdy;
        if (bx < 0 || bx > PF_W - 24) { bdx = -bdx; bx += bdx; }
        if (by < 0 || by > PF_H - 24) { bdy = -bdy; by += bdy; }
        ax += adx;
        if (ax < 0 || ax > PF_W - 32) { adx = -adx; ax += adx; }

        STDL_BlitIndexed8(pf, ball, 24, bx, by, 24, 24,
                          map_id, STDL_I8_UNDER);
        /* same ball art, flipped and recoloured through the map */
        STDL_BlitIndexed8(pf, ball + 23, 24, PF_W - 24 - bx, by,
                          24, 24, map_warm,
                          STDL_I8_UNDER | STDL_I8_XFLIP);
        /*
         * The baked ball: no per-pixel work left, just plane words
         * moved into place. STDL_BLIT_UNDER reads the playfield mask
         * as foreground, so it passes behind the pillars exactly as
         * the chunky blits above do.
         */
        cx += cdx;
        cy += cdy;
        if (cx < 0 || cx > PF_W - 24) {
            cdx = -cdx;
            cx += cdx;
        }
        if (cy < 0 || cy > PF_H - BALL_H) {
            cdy = -cdy;
            cy += cdy;
        }
        r.x = (int16_t)cx;
        r.y = (int16_t)cy;
        r.w = 24;
        r.h = BALL_H;
        {
            STDL_Rect sr;
            sr.x = 0;
            sr.y = 0;
            sr.w = 24;
            sr.h = BALL_H;
            STDL_BlitSurfaceEx(ballspr, &sr, pf, &r, STDL_BLIT_UNDER);
        }

        /* the arrow is stored column-major */
        STDL_BlitIndexed8(pf, adx < 0 ? arrow : arrow + 31 * 16, 16,
                          ax, ay, 32, 16, map_id,
                          adx < 0
                          ? STDL_I8_UNDER | STDL_I8_COLMAJOR
                          : STDL_I8_UNDER | STDL_I8_COLMAJOR
                            | STDL_I8_XFLIP);

        /* playfield to screen: an ordinary aligned blit, through
         * the maskless view */
        r.x = 0;
        r.y = 0;
        r.w = PF_W;
        r.h = PF_H;
        STDL_BlitSurface(pf_bare, NULL, screen, &r);

        /* frame-rate bar from the raw 200Hz counter */
        if (++fps_frames == 16) {
            uint32_t now = STDL_GetHz200();
            uint32_t ticks = now - fps_t0;
            fps = ticks ? (int)(16 * 200 / ticks) : 0;
            fps_t0 = now;
            fps_frames = 0;
        }
        r.x = 0;
        r.y = 194;
        r.w = 320;
        r.h = 4;
        STDL_FillRect(screen, &r, 0);
        r.w = (uint16_t)(fps > 64 ? 320 : fps * 5);
        STDL_FillRect(screen, &r, 15);

        STDL_FrameLimit(50);
    }

    if (music) {
        STDL_CloseVoices();
    }
    STDL_FreeSurface(pf);       /* headers only: the memory is ours */
    STDL_FreeSurface(pf_bare);
    STDL_FreeSurface(ballspr);
    STDL_Quit();
    return 0;
}
