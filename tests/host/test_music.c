/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/*
 * STDL_Music's position, seek, memory stream and register read-back,
 * against the host YM register file.
 *
 * A 700-frame stream (three seek keys and part of a fourth) changes
 * a pseudo-random set of registers every frame, and the test replays
 * it alongside to know what the chip should hold. Checked:
 *  - STDL_MusicFrame counts the ticks;
 *  - the chip and STDL_GetMusicRegisters follow the stream;
 *  - a seek to either side of each key leaves the chip holding the
 *    state at that frame, every voice and not just what the next
 *    frame changes, and the frame after it plays on;
 *  - resuming after a pause puts the held notes back without waiting
 *    for the stream to change them (it used to wait);
 *  - a looped stream comes back to its loop frame;
 *  - a file loads to the same thing as memory;
 *  - a stream whose last frame overruns its data is refused.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdl/stdl.h>
#include "stdl_internal.h"

static int failures;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        if (failures++ < 12) { \
            printf("FAIL %s:%d: ", __FILE__, __LINE__); \
            printf(__VA_ARGS__); \
            printf("\n"); \
        } \
    } \
} while (0)

#define NFRAMES 700
#define LOOP    300

static uint8_t stm[12 + NFRAMES * 16];
static uint32_t stm_len;
/* the registers after each frame, as the stream describes them */
static uint8_t want[NFRAMES][14];

static uint32_t rs = 77;
static uint32_t rnd(void)
{
    rs = rs * 1103515245u + 12345u;
    return rs >> 8;
}

static void put16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

/* frame 0 sets every register; later frames a random few. Volumes
 * stay fixed (no bit 4) and register 13 appears only in frame 0, so
 * the chip's 0-12 are comparable throughout. */
static void build(void)
{
    uint8_t regs[14];
    uint8_t *p = stm + 12;
    int f, r;

    memcpy(stm, "STM1", 4);
    put16(stm + 4, 50);
    put16(stm + 6, NFRAMES);
    put16(stm + 8, LOOP);
    put16(stm + 10, 0);
    memset(regs, 0, sizeof regs);
    for (f = 0; f < NFRAMES; f++) {
        uint16_t mask = f == 0 ? 0x3FFF : (uint16_t)(rnd() & 0x0FFF);
        uint8_t *mp = p;

        p += 2;
        for (r = 0; r <= 13; r++) {
            if (mask & (1u << r)) {
                uint8_t v = (uint8_t)rnd();

                if (r == 7) {
                    v &= 0x3F;
                } else if (r >= 8 && r <= 10) {
                    v &= 0x0F;
                } else if (r == 13) {
                    v = 0x0A;
                }
                regs[r] = v;
                *p++ = v;
            }
        }
        put16(mp, mask);
        memcpy(want[f], regs, sizeof regs);
    }
    stm_len = (uint32_t)(p - stm);
}

/* the chip against the stream's state after frame f */
static void check_chip(int f, const char *what)
{
    int r;

    for (r = 0; r <= 12; r++) {
        uint8_t got = stdl_host_ym[r];

        if (r == 7) {
            got &= 0x3F;
        }
        CHECK(got == want[f][r], "%s, frame %d: register %d is %u, want %u",
              what, f, r, got, want[f][r]);
    }
}

static void check_shadow(int f, const char *what)
{
    uint8_t regs[14];

    CHECK(STDL_GetMusicRegisters(regs) == 0, "%s: no registers", what);
    CHECK(memcmp(regs, want[f], 14) == 0, "%s: registers after frame %d",
          what, f);
}

int main(void)
{
    static const int seeks[] = { 0, 1, 255, 256, 257, 511, 512, 513,
                                 600, NFRAMES - 1 };
    STDL_Music *m, *mf;
    uint8_t regs[14];
    FILE *f;
    int i, k;

    STDL_Init(0);
    stdl_vbl_hz = 50;
    build();

    CHECK(STDL_MusicFrame() == -1, "a frame with no music");
    CHECK(STDL_GetMusicRegisters(regs) == -1, "registers with no music");
    CHECK(STDL_SeekMusic(0) == -1, "a seek with no music");

    m = STDL_MusicFromMemory(stm, stm_len);
    CHECK(m != NULL, "memory stream refused: %s", STDL_GetError());
    if (m == NULL) {
        return 1;
    }
    CHECK(STDL_PlayMusic(m, -1) == 0, "play refused");
    CHECK(STDL_MusicFrame() == 0, "frame %d before the first tick",
          STDL_MusicFrame());

    /* play: the clock, the chip and the read-back follow the stream */
    for (i = 0; i < 40; i++) {
        stdl_music_tick();
        CHECK(STDL_MusicFrame() == i + 1, "frame %d after %d ticks",
              STDL_MusicFrame(), i + 1);
    }
    check_chip(39, "playing");
    check_shadow(39, "playing");

    /* seeks either side of each key; the chip is wiped first so only
     * what the seek writes can pass */
    for (k = 0; k < (int)(sizeof seeks / sizeof seeks[0]); k++) {
        int to = seeks[k];

        memset((void *)stdl_host_ym, 0xEE, 13);
        CHECK(STDL_SeekMusic(to) == 0, "seek to %d refused", to);
        CHECK(STDL_MusicFrame() == to, "frame %d after a seek to %d",
              STDL_MusicFrame(), to);
        if (to > 0) {
            check_chip(to - 1, "after a seek");
            check_shadow(to - 1, "after a seek");
        }
        if (to < NFRAMES - 1) {
            stdl_music_tick();
            check_chip(to, "the frame after a seek");
        }
    }
    CHECK(STDL_SeekMusic(NFRAMES) == -1, "seek past the end accepted");
    CHECK(STDL_SeekMusic(-1) == -1, "seek before the start accepted");

    /* pause silences; resume puts the stream's state straight back */
    STDL_SeekMusic(100);
    stdl_music_tick();
    STDL_PauseMusic();
    CHECK(stdl_host_ym[8] == 0 && stdl_host_ym[9] == 0
          && stdl_host_ym[10] == 0, "pause left a voice sounding");
    STDL_ResumeMusic();
    check_chip(100, "after resume, before a tick");

    /* the loop: the frame after the last is the loop frame */
    STDL_SeekMusic(NFRAMES - 3);
    for (i = 0; i < 3; i++) {
        stdl_music_tick();
    }
    CHECK(STDL_MusicFrame() == LOOP, "frame %d after the end, want %d",
          STDL_MusicFrame(), LOOP);
    stdl_music_tick();
    CHECK(STDL_MusicFrame() == LOOP + 1, "loop did not play on");

    /* a file loads to what memory does */
    f = fopen("/tmp/stdl_test_music.stm", "wb");
    if (f != NULL) {
        fwrite(stm, 1, stm_len, f);
        fclose(f);
    }
    mf = STDL_LoadMusic("/tmp/stdl_test_music.stm");
    CHECK(mf != NULL, "file stream refused: %s", STDL_GetError());
    if (mf != NULL) {
        STDL_PlayMusic(mf, 1);
        STDL_SeekMusic(513);
        stdl_music_tick();
        check_chip(513, "file stream after a seek");
        STDL_FreeMusic(mf);
        CHECK(STDL_MusicFrame() == -1, "freed music still has a frame");
    }

    /* the last frame's bytes run past the end */
    CHECK(STDL_MusicFromMemory(stm, stm_len - 1) == NULL,
          "a truncated stream was accepted");
    CHECK(STDL_MusicFromMemory(stm, 11) == NULL, "a bare header accepted");

    STDL_FreeMusic(m);
    remove("/tmp/stdl_test_music.stm");

    if (failures) {
        printf("test_music: %d failures\n", failures);
        return 1;
    }
    printf("test_music: ok\n");
    return 0;
}
