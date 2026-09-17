/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/*
 * STDL_Voice mixer against hand-computed expectations.
 *
 * The host stubs stand in for the hardware: the fake DMA records the
 * ring the device programs and the test steers the fake frame
 * counter, then invokes the device's VBL callback through the fake
 * TOS queue - the same call path as the interrupt. Covers the
 * volume table, resampling steps, one-shot end, loop confinement,
 * multi-voice summing, ring chase logic and the sequencer tick.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdl/stdl.h>
#include "stdl_internal.h"

extern uint32_t stdl_host_dma_pos;
extern const void *stdl_host_dma_buf;
extern int stdl_host_dma_running;

extern const int8_t *stdl_host_voltab(void);

static int failures;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        failures++; \
        printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } \
} while (0)

#define RING 512
#define BLOCK 128

static void (*vbl_fn)(void);

static void find_vbl(void)
{
    int i;
    vbl_fn = NULL;
    for (i = 0; i < 8; i++) {
        if (STDL_VBLQUEUE[i] != NULL) {
            vbl_fn = STDL_VBLQUEUE[i];
        }
    }
}

static const int8_t *ring_base(void)
{
    return (const int8_t *)stdl_host_dma_buf;
}

/* put the fake play head at frame `f` of the ring and tick */
static void tick_at(uint32_t f)
{
    stdl_host_dma_pos = (uint32_t)(uintptr_t)stdl_host_dma_buf + f;
    vbl_fn();
}

static int ticks_seen;
static void count_tick(void *ud)
{
    (void)ud;
    ticks_seen++;
}

static void open_fresh(void)
{
    STDL_CloseVoices();
    CHECK(STDL_OpenVoices(6258) == 0, "open: %s", STDL_GetError());
    find_vbl();
    CHECK(vbl_fn != NULL, "no VBL slot claimed");
    CHECK(stdl_host_dma_running, "DMA not started");
}

int main(void)
{
    static int8_t ramp[256];
    static int8_t looped[16];
    static int8_t plus[64], minus[64];
    const int8_t *r;
    int i;

    STDL_Init(0);
    stdl.mach.is_ste = 1;

    for (i = 0; i < 256; i++) {
        ramp[i] = (int8_t)((i & 63) - 32);
    }
    for (i = 0; i < 16; i++) {
        looped[i] = (int8_t)(10 + i * 4);
    }
    memset(plus, 64, sizeof(plus));
    memset(minus, -64, sizeof(minus));

    CHECK(!STDL_VoicesOpen(), "voices open before OpenVoices");
    open_fresh();
    CHECK(STDL_VoicesOpen(), "VoicesOpen after open");
    CHECK(STDL_OpenVoices(6258) < 0, "double open accepted");

    /* one-shot ramp at the device rate, full volume: mixed value is
     * the sample halved toward zero, and the voice ends exactly at
     * len. Halved, not quartered, since v1.8.1 reserves headroom
     * for two voices rather than four; and toward zero rather than
     * >>1, because an arithmetic shift is a floor and the table is
     * symmetric about zero. */
    STDL_SetVoice(0, ramp, 256, 0, 0, 6258, 64);
    CHECK(STDL_VoiceActive(0), "voice 0 not active");
    tick_at(0);                 /* play in block 0: fills 1,2,3 */
    r = ring_base();
    for (i = 0; i < 256; i++) {
        const int rv = ramp[i];
        const int want = rv < 0 ? -((-rv) >> 1) : (rv >> 1);
        if (r[BLOCK + i] != want) {
            CHECK(0, "ramp[%d]: got %d want %d", i, r[BLOCK + i], want);
            break;
        }
    }
    for (i = 256; i < 384; i++) {
        if (r[BLOCK + i] != 0) {
            CHECK(0, "tail[%d] not silent: %d", i, r[BLOCK + i]);
            break;
        }
    }
    CHECK(!STDL_VoiceActive(0), "one-shot still active after end");

    /* ring chase: play head into block 2 -> block 0 gets filled
     * (silence now), blocks stop at the play block */
    tick_at(2 * BLOCK);
    for (i = 0; i < BLOCK; i++) {
        if (r[i] != 0) {
            CHECK(0, "block 0 not refilled silent at %d: %d", i, r[i]);
            break;
        }
    }

    /* loop confinement: after the first pass, playback stays inside
     * [loop_off, loop_off+loop_len) */
    open_fresh();
    STDL_SetVoice(1, looped, 16, 8, 8, 6258, 64);
    tick_at(0);
    r = ring_base();
    for (i = 0; i < 384 - BLOCK; i++) {
        int src = (i < 16) ? i : 8 + ((i - 16) & 7);
        const int lv = looped[src];
        const int want = lv < 0 ? -((-lv) >> 1) : (lv >> 1);
        if (r[BLOCK + i] != want) {
            CHECK(0, "loop[%d]: got %d want %d", i, r[BLOCK + i], want);
            break;
        }
    }
    CHECK(STDL_VoiceActive(1), "looping voice went inactive");

    /* two voices sum; equal and opposite cancel */
    open_fresh();
    STDL_SetVoice(0, plus, 64, 0, 0, 6258, 64);
    STDL_SetVoice(1, minus, 64, 0, 0, 6258, 64);
    tick_at(0);
    r = ring_base();
    for (i = 0; i < 64; i++) {
        if (r[BLOCK + i] != 0) {
            CHECK(0, "sum[%d]: got %d want 0", i, r[BLOCK + i]);
            break;
        }
    }

    /* fractional resampling: expectations computed with the same
     * 16.16 step arithmetic the mixer uses */
    open_fresh();
    STDL_SetVoice(2, looped, 16, 0, 0, 3129, 64);
    tick_at(0);
    r = ring_base();
    {
        uint32_t step = (3129u << 16) / 6258u;
        uint32_t pos = 0;
        for (i = 0; i < 32 && (pos >> 16) < 16; i++) {
            const int lv = looped[pos >> 16];
            const int want = lv < 0 ? -((-lv) >> 1) : (lv >> 1);
            if (r[BLOCK + i] != want) {
                CHECK(0, "frac[%d]: got %d want %d",
                      i, r[BLOCK + i], want);
                break;
            }
            pos += step;
        }
    }

    /* volume: level 32 halves the level-64 contribution. plus[] is
     * +64, so 32 at full volume and 16 at half. */
    open_fresh();
    STDL_SetVoice(0, plus, 64, 0, 0, 6258, 32);
    tick_at(0);
    r = ring_base();
    CHECK(r[BLOCK] == 16, "vol 32: got %d want 16", r[BLOCK]);

    /* sequencer tick runs once per VBL, before mixing */
    open_fresh();
    STDL_SetVoiceTick(count_tick, NULL);
    ticks_seen = 0;
    tick_at(0);
    tick_at(BLOCK);
    CHECK(ticks_seen == 2, "tick ran %d times, want 2", ticks_seen);
    STDL_SetVoiceTick(NULL, NULL);

    /*
     * The volume table is symmetric about zero, and quiet content
     * at a low volume is silent rather than biased. An arithmetic
     * shift is a floor, so before this was rounded toward zero a
     * sample under the rounding threshold came out 0 on its
     * positive half and -1 on its negative one - a DC-biased square
     * wave at the sample's own zero crossings, audible on hardware
     * as a tick under a quiet effect.
     */
    {
        const int8_t *tab = stdl_host_voltab();
        int lvl, v;

        for (lvl = 0; lvl <= 64; lvl++) {
            const int8_t *row = tab + lvl * 256;

            for (v = 1; v < 128; v++) {
                CHECK(row[v & 255] == -row[(-v) & 255],
                      "vol %d: %d -> %d but %d -> %d",
                      lvl, v, row[v & 255], -v, row[(-v) & 255]);
            }
            CHECK(row[0] == 0, "vol %d: silence is not silent", lvl);
        }
        /* and the specific case heard on the machine: an ambient
         * effect at vol 16 whose content sits inside +/-4. The
         * threshold is half what it was, which is the point of the
         * headroom change - +/-8 is now audible rather than
         * rounded away. */
        for (v = -4; v <= 4; v++) {
            CHECK(tab[16 * 256 + (v & 255)] == 0,
                  "vol 16: %d -> %d, want 0", v,
                  tab[16 * 256 + (v & 255)]);
        }
        CHECK(tab[16 * 256 + (8 & 255)] == 1,
              "vol 16: 8 -> %d, want 1 (it was rounded away before)",
              tab[16 * 256 + (8 & 255)]);
        /* two voices at full volume reach the ends of the range
         * exactly, which is what the reserve now buys */
        CHECK(tab[64 * 256 + (127 & 255)] == 63, "vol 64: 127 -> %d",
              tab[64 * 256 + (127 & 255)]);
        CHECK(tab[64 * 256 + ((-128) & 255)] == -64,
              "vol 64: -128 -> %d", tab[64 * 256 + ((-128) & 255)]);
    }

    /* Four loud voices clamp instead of wrapping. Without the
     * clamp this sums to -256 and comes back as 0 - silence where
     * the loudest possible moment should be, which is the failure
     * that matters: a wrap inverts the waveform, a clip flattens
     * it. */
    {
        int i2;

        open_fresh();
        for (i2 = 0; i2 < STDL_VOICES; i2++) {
            STDL_SetVoice(i2, minus, sizeof minus, 0, 0, 6258, 64);
        }
        tick_at(0);
        r = ring_base();
        CHECK(r[BLOCK] == -128, "four voices at -64 gave %d, want -128",
              r[BLOCK]);
        for (i2 = 0; i2 < STDL_VOICES; i2++) {
            STDL_StopVoice(i2);
        }
    }

    STDL_CloseVoices();
    CHECK(!STDL_VoicesOpen(), "still open after close");
    CHECK(!stdl_host_dma_running, "DMA still running after close");
    CHECK(stdl.dma_owner == STDL_DMA_FREE, "DMA owner not released");

    if (failures) {
        printf("%d FAILURES\n", failures);
        return 1;
    }
    printf("test_voice: OK\n");
    return 0;
}
