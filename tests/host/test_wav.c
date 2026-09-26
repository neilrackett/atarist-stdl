/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/*
 * Sample conversion and WAV loading (src/wav.c) against worked
 * expectations: each format to signed 8-bit, stereo kept and stereo
 * mixed to mono, nearest-neighbour rate changes up and down landing
 * on floor(j * src / dst) for the sizes tried (the step's fraction is
 * truncated, so other sizes can land a frame early), a long source
 * stepped without its position overflowing, and a WAV header read
 * back.
 */
#include <stdio.h>
#include <string.h>
#include <stdl/stdl.h>
#include "stdl_internal.h"

static int failures;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        failures++; \
        printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } \
} while (0)

static uint8_t big[100000];

int main(void)
{
    int8_t out[16];
    uint32_t i;

    /* unsigned 8-bit: the sign flips */
    {
        static const uint8_t u8[4] = { 0x00, 0x80, 0xFF, 0x40 };
        stdl_audio_convert(out, 4, u8, 4, STDL_AUDIO_U8, 1, 0);
        CHECK(out[0] == -128 && out[1] == 0 && out[2] == 127 && out[3] == -64,
              "u8: %d %d %d %d", out[0], out[1], out[2], out[3]);
    }
    /* 16-bit little-endian stereo: the high byte of each, both kept */
    {
        static const uint8_t s16[8] = { 0x34, 0x12, 0xDC, 0xFE, 0x00, 0x80, 0xFF, 0x7F };
        stdl_audio_convert(out, 2, s16, 2, STDL_AUDIO_S16LSB, 2, 0);
        CHECK(out[0] == 0x12 && out[1] == -2 && out[2] == -128 && out[3] == 127,
              "s16lsb stereo: %d %d %d %d", out[0], out[1], out[2], out[3]);
    }
    /* 16-bit big-endian mono */
    {
        static const uint8_t s16[4] = { 0x12, 0x34, 0xF0, 0x00 };
        stdl_audio_convert(out, 2, s16, 2, STDL_AUDIO_S16MSB, 1, 0);
        CHECK(out[0] == 0x12 && out[1] == -16, "s16msb: %d %d", out[0], out[1]);
    }
    /* stereo mixed to mono: the mean of the two */
    {
        static const uint8_t u8[4] = { 0x80 + 40, 0x80 + 20, 0x80 - 10, 0x80 - 30 };
        stdl_audio_convert(out, 2, u8, 2, STDL_AUDIO_U8, 2, 1);
        CHECK(out[0] == 30 && out[1] == -20, "mono mix: %d %d", out[0], out[1]);
    }
    /* up 3 -> 5 and down 5 -> 3: frame j reads floor(j * src / dst) */
    {
        static const uint8_t s8[5] = { 1, 2, 3, 4, 5 };
        stdl_audio_convert(out, 5, s8, 3, STDL_AUDIO_S8, 1, 0);
        CHECK(out[0] == 1 && out[1] == 1 && out[2] == 2 && out[3] == 2 && out[4] == 3,
              "up: %d %d %d %d %d", out[0], out[1], out[2], out[3], out[4]);
        stdl_audio_convert(out, 3, s8, 5, STDL_AUDIO_S8, 1, 0);
        CHECK(out[0] == 1 && out[1] == 2 && out[2] == 4,
              "down: %d %d %d", out[0], out[1], out[2]);
    }
    /* a long source: 100000 frames into 1000, where a 16.16 position
     * would have overflowed a long past frame 65535 */
    {
        static int8_t small[1000];
        for (i = 0; i < sizeof(big); i++) {
            big[i] = (uint8_t)(i / 400);
        }
        stdl_audio_convert(small, 1000, big, 100000, STDL_AUDIO_S8, 1, 0);
        CHECK(small[500] == (int8_t)big[50000] && small[999] == (int8_t)big[99900],
              "long: %d want %d, %d want %d", small[500], (int8_t)big[50000],
              small[999], (int8_t)big[99900]);
    }
    /* a WAV header read back */
    {
        static const uint8_t wav[] = {
            'R','I','F','F', 40,0,0,0, 'W','A','V','E',
            'f','m','t',' ', 16,0,0,0, 1,0, 1,0, 0x11,0x2B,0,0, 0x11,0x2B,0,0, 1,0, 8,0,
            'd','a','t','a', 4,0,0,0, 0x80, 0x90, 0x70, 0xFF
        };
        STDL_AudioSpec spec;
        uint8_t *buf = NULL;
        uint32_t len = 0;
        FILE *f = fopen("test_wav.tmp", "wb");
        fwrite(wav, 1, sizeof(wav), f);
        fclose(f);
        CHECK(STDL_LoadWAV("test_wav.tmp", &spec, &buf, &len) != NULL,
              "load: %s", STDL_GetError());
        CHECK(spec.freq == 11025 && spec.format == STDL_AUDIO_U8 && spec.channels == 1,
              "spec: %d Hz, format %04x, %d channels", spec.freq, spec.format, spec.channels);
        CHECK(len == 4 && buf != NULL && buf[1] == 0x90, "data: %u bytes", (unsigned)len);
        STDL_FreeWAV(buf);
        remove("test_wav.tmp");
    }

    if (failures == 0) {
        printf("test_wav: all checks passed\n");
    }
    return failures ? 1 : 0;
}
