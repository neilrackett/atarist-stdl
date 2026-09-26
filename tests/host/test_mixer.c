/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/*
 * The SDL_mixer compatibility layer's chunks on the voice mixer,
 * driven through the SDL_mixer API and read back from the fake DMA
 * ring (as tests/host/test_voice.c does): the rate a PC game asks for
 * landing on 12517, a chunk converted to that rate as it loads, the
 * two volumes becoming a voice's, a volume change heard at once, a
 * finite loop count, halting, freeing a playing chunk, and close.
 */
#include <stdio.h>
#include <string.h>
#include <stdl/stdl.h>
#include "stdl_internal.h"
#include <SDL_mixer.h>
#include "ymtest.h"

static int failures;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        failures++; \
        printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } \
} while (0)

/* an unsigned 8-bit mono WAV of `frames` frames of one value */
static void write_wav(const char *name, int rate, int frames, int value)
{
    FILE *f = fopen(name, "wb");
    const unsigned long size = 36 + (unsigned long)frames;
    uint8_t h[44];
    int i;

    memcpy(h, "RIFF", 4);
    h[4] = size & 0xFF; h[5] = (size >> 8) & 0xFF; h[6] = 0; h[7] = 0;
    memcpy(h + 8, "WAVEfmt ", 8);
    h[16] = 16; h[17] = h[18] = h[19] = 0;
    h[20] = 1; h[21] = 0; h[22] = 1; h[23] = 0;
    h[24] = rate & 0xFF; h[25] = (rate >> 8) & 0xFF; h[26] = h[27] = 0;
    h[28] = rate & 0xFF; h[29] = (rate >> 8) & 0xFF; h[30] = h[31] = 0;
    h[32] = 1; h[33] = 0; h[34] = 8; h[35] = 0;
    memcpy(h + 36, "data", 4);
    h[40] = frames & 0xFF; h[41] = (frames >> 8) & 0xFF; h[42] = h[43] = 0;
    fwrite(h, 1, 44, f);
    for (i = 0; i < frames; i++) {
        fputc(0x80 + value, f);
    }
    fclose(f);
}

int main(void)
{
    Mix_Chunk *a, *b;
    int i;

    STDL_Init(0);
    stdl.mach.is_ste = 1;

    /* 22050, a PC game's usual ask, becomes 12517: a 12517Hz chunk
     * keeps its length and a 6258Hz one doubles */
    CHECK(Mix_OpenAudio(22050, AUDIO_S8, 1, 1024) == 0, "open");
    CHECK(STDL_VoicesOpen(), "chunks should be on the voice device");
    find_vbl();
    CHECK(vbl_fn != NULL, "no VBL slot claimed");
    write_wav("test_mixer_a.tmp", 12517, 40, 100);
    write_wav("test_mixer_b.tmp", 6258, 20, -60);
    a = Mix_LoadWAV("test_mixer_a.tmp");
    b = Mix_LoadWAV("test_mixer_b.tmp");
    CHECK(a != NULL && a->alen == 40, "a: %u frames", a ? (unsigned)a->alen : 0);
    CHECK(b != NULL && b->alen == 40, "b: %u frames", b ? (unsigned)b->alen : 0);

    /* one-shot at full volume: the voice's half scale, then silence */
    CHECK(Mix_PlayChannel(-1, a, 0) == 0, "first free channel");
    CHECK(Mix_Playing(0) == 1 && Mix_Playing(-1) == 1, "playing count");
    tick_at(0);
    CHECK(ring_base()[BLOCK] == 50 && ring_base()[BLOCK + 39] == 50 && ring_base()[BLOCK + 40] == 0,
          "one-shot: %d %d %d", ring_base()[BLOCK], ring_base()[BLOCK + 39], ring_base()[BLOCK + 40]);
    CHECK(Mix_Playing(0) == 0, "one-shot should have ended");

    /* a loop count: two more passes, 120 frames, then silence; and the
     * chunk's volume change heard on the channel playing it */
    CHECK(Mix_PlayChannel(1, a, 2) == 1, "channel 1");
    Mix_VolumeChunk(a, 64);
    tick_at(2 * BLOCK);                 /* refills blocks 0 and 1 */
    for (i = 0; i < BLOCK; i++) {
        const int want = (i < 120) ? 25 : 0;
        if (ring_base()[i] != want) {
            CHECK(0, "loops[%d]: got %d want %d", i, ring_base()[i], want);
            break;
        }
    }
    CHECK(Mix_Playing(1) == 0, "the loop count should have ended it");
    Mix_VolumeChunk(a, MIX_MAX_VOLUME);

    /* looping until halted, a channel volume heard at once, halt */
    Mix_PlayChannel(2, b, -1);
    Mix_Volume(2, 64);
    tick_at(3 * BLOCK);                 /* refills block 2 */
    CHECK(ring_base()[2 * BLOCK] == -15 && ring_base()[3 * BLOCK - 1] == -15,
          "b at half volume: %d %d", ring_base()[2 * BLOCK], ring_base()[3 * BLOCK - 1]);
    CHECK(Mix_Playing(2) == 1, "an endless loop should still play");
    Mix_HaltChannel(-1);
    CHECK(Mix_Playing(-1) == 0, "halted");

    /* freeing a chunk that plays stops it first */
    Mix_PlayChannel(3, b, -1);
    Mix_FreeChunk(b);
    CHECK(Mix_Playing(3) == 0, "freeing a playing chunk should stop it");

    Mix_FreeChunk(a);
    Mix_CloseAudio();
    CHECK(!STDL_VoicesOpen(), "close should release the voices");
    remove("test_mixer_a.tmp");
    remove("test_mixer_b.tmp");

    if (failures == 0) {
        printf("test_mixer: all checks passed\n");
    }
    return failures ? 1 : 0;
}
