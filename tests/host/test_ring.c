/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/*
 * The ring device (src/ring.c) refilled by hand: the test puts the
 * fake DMA's play head in one half of the ring and calls the pump
 * hook, as STDL_PumpEvents would. Covers the direct path - a signed
 * 8-bit callback at a DMA rate handed the ring itself - and the
 * converted path, where the callback's frames per refill must follow
 * the rate exactly across refills and never outgrow the buffer sized
 * for them (AddressSanitizer watches that buffer's edge).
 */
#include <stdio.h>
#include <string.h>
#include <stdl/stdl.h>
#include "stdl_internal.h"

extern uint32_t stdl_host_dma_pos;
extern int stdl_host_dma_running;
extern const void *stdl_host_dma_buf;
extern uint32_t stdl_host_dma_bytes;

static int failures;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        failures++; \
        printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } \
} while (0)

static int calls, max_len;
static long total_bytes;
static uint8_t *last_stream;
static unsigned next;

/* a count that does not divide the ring, so a refill is told from
 * the data it replaced */
static void counting(void *ud, uint8_t *stream, int len)
{
    int i;

    (void)ud;
    calls++;
    last_stream = stream;
    for (i = 0; i < len; i++) {
        stream[i] = (uint8_t)(next++ % 101);
    }
}

/* every frame the same: unsigned 8-bit mono, or 16-bit LE stereo */
static void u8_level(void *ud, uint8_t *stream, int len)
{
    (void)ud;
    calls++;
    total_bytes += len;
    if (len > max_len) {
        max_len = len;
    }
    memset(stream, 0x80 + 40, (size_t)len);
}

static void s16_stereo(void *ud, uint8_t *stream, int len)
{
    int i;

    (void)ud;
    calls++;
    if (len > max_len) {
        max_len = len;
    }
    for (i = 0; i + 3 < len; i += 4) {
        stream[i] = 0x34; stream[i + 1] = 0x12;         /* left  0x1234 */
        stream[i + 2] = 0xDC; stream[i + 3] = 0xFE;     /* right 0xFEDC */
    }
}

/* the play head at byte `off` of the ring, then the pump */
static void pump_at(uint32_t off)
{
    stdl_host_dma_pos = (uint32_t)(uintptr_t)stdl_host_dma_buf + off;
    stdl_audio_hook();
}

static int open_spec(int freq, uint16_t format, int channels,
                     void (*cb)(void *, uint8_t *, int),
                     STDL_AudioSpec *got)
{
    STDL_AudioSpec want;

    memset(&want, 0, sizeof(want));
    want.freq = freq;
    want.format = format;
    want.channels = (uint8_t)channels;
    want.callback = cb;
    calls = 0;
    max_len = 0;
    total_bytes = 0;
    return STDL_OpenAudio(&want, got);
}

int main(void)
{
    STDL_AudioSpec got;
    const int8_t *r;
    uint8_t *first;
    int i, fills;

    STDL_Init(0);
    stdl.mach.is_ste = 1;

    /* direct: S8 at 6258 primes both halves in place, 2048 frames
     * each, straight into the ring the DMA is then handed */
    CHECK(open_spec(6258, STDL_AUDIO_S8, 1, counting, &got) == 0,
          "open S8: %s", STDL_GetError());
    CHECK(calls == 2, "priming: %d calls", calls);
    first = last_stream - 2048;
    CHECK(STDL_GetAudioStatus() == STDL_AUDIO_PAUSED, "should open paused");
    STDL_PauseAudio(0);
    CHECK(stdl_host_dma_running && stdl_host_dma_buf == first
          && stdl_host_dma_bytes == 4096,
          "the callback should have written the ring the DMA plays");
    r = (const int8_t *)stdl_host_dma_buf;
    for (i = 0; i < 4096; i++) {
        if (r[i] != (int8_t)(i % 101)) {
            CHECK(0, "primed[%d]: got %d want %d", i, r[i], i % 101);
            break;
        }
    }
    pump_at(100);                       /* playing the first half */
    CHECK(calls == 2, "no refill while the filled half is ahead");
    pump_at(3000);                      /* into the second: refill the first */
    CHECK(calls == 3 && last_stream == first, "refill of the first half");
    CHECK(r[0] == (int8_t)(4096 % 101) && r[2047] == (int8_t)(6143 % 101)
          && r[2048] == (int8_t)(2048 % 101),
          "refilled: %d %d %d", r[0], r[2047], r[2048]);
    STDL_CloseAudio();
    CHECK(stdl_audio_hook == NULL && !stdl_host_dma_running
          && stdl.dma_owner == STDL_DMA_FREE, "close");

    /* converted: U8 at 11025 plays at 12517; across refills the
     * callback's frames add up to the rate exactly, and no refill
     * asks for more than a half ring's worth and one over */
    CHECK(open_spec(11025, STDL_AUDIO_U8, 1, u8_level, &got) == 0,
          "open U8: %s", STDL_GetError());
    CHECK(got.freq == 12517 && got.format == STDL_AUDIO_S8,
          "obtained %d Hz, format %04x", got.freq, got.format);
    STDL_PauseAudio(0);
    for (fills = 2; fills < 22; fills++) {
        pump_at((fills & 1) ? 100 : 3000);
    }
    CHECK(calls == 22, "%d calls for 22 refills", calls);
    CHECK(total_bytes == (long)((uint64_t)22 * 2048 * 11025 / 12517),
          "%ld frames from the callback, want %ld", total_bytes,
          (long)((uint64_t)22 * 2048 * 11025 / 12517));
    CHECK(max_len <= 2048 * 11025 / 12517 + 1, "a refill asked for %d", max_len);
    r = (const int8_t *)stdl_host_dma_buf;
    CHECK(r[0] == 40 && r[4095] == 40, "u8 level: %d %d", r[0], r[4095]);
    STDL_CloseAudio();

    /* converted stereo: 16-bit at 22050 plays at 25033 in stereo,
     * the high byte of each channel kept */
    CHECK(open_spec(22050, STDL_AUDIO_S16LSB, 2, s16_stereo, &got) == 0,
          "open S16 stereo: %s", STDL_GetError());
    CHECK(got.freq == 25033 && got.channels == 2, "obtained %d Hz, %d ch",
          got.freq, got.channels);
    STDL_PauseAudio(0);
    CHECK(stdl_host_dma_bytes == 8192, "stereo ring %u bytes",
          (unsigned)stdl_host_dma_bytes);
    pump_at(5000);
    r = (const int8_t *)stdl_host_dma_buf;
    CHECK(calls == 3 && r[0] == 0x12 && r[1] == -2 && r[8190] == 0x12 && r[8191] == -2,
          "stereo: %d calls, %d %d %d %d", calls, r[0], r[1], r[8190], r[8191]);
    CHECK(max_len <= (2048 * 22050 / 25033 + 1) * 4, "a refill asked for %d", max_len);
    CHECK(open_spec(22050, STDL_AUDIO_S16LSB, 2, s16_stereo, &got) < 0,
          "a second open should fail");
    STDL_CloseAudio();

    if (failures == 0) {
        printf("test_ring: all checks passed\n");
    }
    return failures ? 1 : 0;
}
