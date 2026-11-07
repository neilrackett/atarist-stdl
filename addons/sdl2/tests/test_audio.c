/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The add-on's audio on the host, on fake_stdl.c's ring device, which
 * asks for what ring.c asks: a half ring of DMA frames at a time in
 * the callback's format, a few bytes more or less each time, and two
 * at open. The program's callback must get exactly the obtained size
 * every call, never before the first unpause, and its bytes must reach
 * the ring in order, none lost or repeated, through pauses and locks.
 * Then the spec's negotiation, the queue, SDL2's mixing arithmetic
 * against a reference, and WAV files.
 */

#include "SDL.h"

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

/* the program's callback: a running count, its sizes checked */
static Uint8 counter;
static int calls, wrong_len;
static Uint32 expect_len;

static void count_cb(void *userdata, Uint8 *stream, int len)
{
    int i;
    calls++;
    wrong_len += (Uint32)len != expect_len || userdata != &counter;
    for (i = 0; i < len; i++) {
        stream[i] = counter++;
    }
}

/* the ring's output from `from`, as the running count from `start`,
 * skipping `silent` bytes of silence where the count stood still */
static int in_order(Uint32 from, Uint8 start)
{
    Uint32 i;
    for (i = from; i < fake_audio_outlen; i++) {
        if (fake_audio_out[i] != start++) {
            return 0;
        }
    }
    return 1;
}

static void feed(void)
{
    SDL_AudioSpec want, got;
    SDL_AudioDeviceID id;
    Uint32 mark;
    int i, silent;

    memset(&want, 0, sizeof(want));
    want.freq = 22050;
    want.format = AUDIO_S16LSB;
    want.channels = 2;
    want.samples = 512;
    want.callback = count_cb;
    want.userdata = &counter;
    id = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    CHECK(id == 2);
    CHECK(got.freq == 22050 && got.format == AUDIO_S16LSB &&
          got.channels == 2 && got.samples == 512 && got.size == 2048 &&
          got.silence == 0 && got.callback == count_cb);
    expect_len = got.size;
    /* the ring primed itself, and the program was not asked */
    CHECK(calls == 0);
    CHECK(SDL_GetAudioDeviceStatus(id) == SDL_AUDIO_PAUSED);
    CHECK(SDL_GetAudioStatus() == SDL_AUDIO_STOPPED);   /* device 1 */
    fake_audio_refill();
    CHECK(fake_audio_outlen == 0);                      /* paused */

    SDL_PauseAudioDevice(id, 0);
    CHECK(SDL_GetAudioDeviceStatus(id) == SDL_AUDIO_PLAYING);
    fake_audio_outlen = 0;
    for (i = 0; i < 40; i++) {
        fake_audio_refill();
    }
    /* 40 refills of 1,803 or 1,804 frames: not whole callbacks */
    CHECK(fake_audio_outlen > 40 * 7200 && fake_audio_outlen % 2048 != 0);
    CHECK(calls > 0 && wrong_len == 0);
    CHECK(in_order(0, 0));

    /* locked: silence, and the count carries on after it */
    mark = fake_audio_outlen;
    SDL_LockAudioDevice(id);
    fake_audio_refill();
    silent = (int)(fake_audio_outlen - mark);
    for (i = 0; i < silent; i++) {
        CHECK(fake_audio_out[mark + i] == 0);
        if (fake_audio_out[mark + i]) {
            break;
        }
    }
    SDL_UnlockAudioDevice(id);
    {
        const Uint8 next = fake_audio_out[mark - 1] + 1;
        const Uint32 resume = fake_audio_outlen;
        fake_audio_refill();
        fake_audio_refill();
        CHECK(fake_audio_out[resume] == next);
        CHECK(in_order(resume, next));
    }
    CHECK(wrong_len == 0);

    /* paused again: nothing asked */
    SDL_PauseAudioDevice(id, 1);
    i = calls;
    fake_audio_refill();
    CHECK(calls == i);

    /* one device at a time, then closed */
    CHECK(SDL_OpenAudioDevice(NULL, 0, &want, &got, 0) == 0);
    SDL_CloseAudioDevice(id);
    CHECK(SDL_GetAudioDeviceStatus(id) == SDL_AUDIO_STOPPED);
    CHECK(!fake_audio_open);
}

static void negotiation(void)
{
    SDL_AudioSpec want, got;
    SDL_AudioDeviceID id;

    memset(&want, 0, sizeof(want));
    want.freq = 44100;
    want.format = AUDIO_F32SYS;
    want.channels = 6;
    want.callback = count_cb;

    /* float and six channels, refused, then taken as 16 bits stereo */
    CHECK(SDL_OpenAudioDevice(NULL, 0, &want, &got, 0) == 0);
    CHECK(SDL_OpenAudioDevice(NULL, 0, &want, &got,
                              SDL_AUDIO_ALLOW_FORMAT_CHANGE) == 0);
    id = SDL_OpenAudioDevice(NULL, 0, &want, &got,
                             SDL_AUDIO_ALLOW_FORMAT_CHANGE |
                             SDL_AUDIO_ALLOW_CHANNELS_CHANGE);
    CHECK(id != 0);
    CHECK(got.format == AUDIO_S16SYS && got.channels == 2 &&
          got.freq == 44100 && got.samples == 2048 && got.size == 8192);
    SDL_CloseAudioDevice(id);

    /* a DMA rate, given a frequency change */
    want.format = AUDIO_U8;
    want.channels = 1;
    want.freq = 22050;
    want.samples = 0;
    id = SDL_OpenAudioDevice(NULL, 0, &want, &got,
                             SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    CHECK(got.freq == 25033 && got.silence == 0x80 && got.samples == 1024 &&
          got.size == 1024 && fake_audio_spec.freq == 25033);
    SDL_CloseAudioDevice(id);
    fake_audio_falcon = 1;
    want.freq = 6000;
    id = SDL_OpenAudioDevice(NULL, 0, &want, &got,
                             SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    CHECK(got.freq == 12517);
    SDL_CloseAudioDevice(id);
    fake_audio_falcon = 0;

    /* SDL2's defaults */
    memset(&want, 0, sizeof(want));
    want.callback = count_cb;
    id = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    CHECK(got.freq == 22050 && got.format == AUDIO_S16LSB &&
          got.channels == 2 && got.samples == 1024);
    SDL_CloseAudioDevice(id);

    /* the legacy device: desired filled in, or anything allowed */
    want.freq = 12517;
    want.format = AUDIO_S8;
    want.channels = 1;
    want.samples = 256;
    CHECK(SDL_OpenAudio(&want, NULL) == 0);
    CHECK(want.size == 256 && want.silence == 0);
    CHECK(SDL_GetAudioStatus() == SDL_AUDIO_PAUSED);
    SDL_PauseAudio(0);
    CHECK(SDL_GetAudioStatus() == SDL_AUDIO_PLAYING);
    SDL_CloseAudio();
    want.format = AUDIO_S32SYS;
    CHECK(SDL_OpenAudio(&want, NULL) < 0);
    CHECK(SDL_OpenAudio(&want, &got) == 0 && got.format == AUDIO_S16SYS);
    SDL_CloseAudio();

    /* no capture, no other device, of a spec that would open */
    want.format = AUDIO_S8;
    CHECK(SDL_OpenAudioDevice(NULL, 1, &want, &got, 0) == 0);
    CHECK(SDL_OpenAudioDevice("Speaker", 0, &want, &got, 0) == 0);
    CHECK(SDL_GetNumAudioDevices(0) == 1 && SDL_GetNumAudioDevices(1) == 0);
    id = SDL_OpenAudioDevice(SDL_GetAudioDeviceName(0, 0), 0, &want, &got, 0);
    CHECK(id != 0);
    SDL_CloseAudioDevice(id);
    CHECK(strcmp(SDL_GetCurrentAudioDriver(), "stdl") == 0 &&
          SDL_GetNumAudioDrivers() == 1);
}

static void queue(void)
{
    SDL_AudioSpec want, got;
    SDL_AudioDeviceID id;
    Uint8 data[3000];
    Uint32 i;
    int bad = 0;

    memset(&want, 0, sizeof(want));
    want.freq = 12517;
    want.format = AUDIO_U8;
    want.channels = 1;
    want.samples = 256;
    id = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    CHECK(id && got.callback == NULL);
    for (i = 0; i < sizeof(data); i++) {
        /* never the same 2K apart, so stale bytes show */
        data[i] = (Uint8)(i * 7 + i / 251 + 1);
    }
    CHECK(SDL_QueueAudio(id, data, 1000) == 0);
    CHECK(SDL_QueueAudio(id, data + 1000, 2000) == 0);
    CHECK(SDL_GetQueuedAudioSize(id) == 3000);
    SDL_PauseAudioDevice(id, 0);
    fake_audio_outlen = 0;
    fake_audio_refill();                /* 2,048 frames at 12517 */
    CHECK(fake_audio_outlen == 2048);
    CHECK(SDL_GetQueuedAudioSize(id) == 3000 - 2048);
    /* more while it plays, past the end of the queue's 4K, so what is
     * left moves to its front; then the rest, and silence */
    CHECK(SDL_QueueAudio(id, data, 2000) == 0);
    CHECK(SDL_GetQueuedAudioSize(id) == 3000 - 2048 + 2000);
    fake_audio_refill();
    fake_audio_refill();
    for (i = 0; i < fake_audio_outlen; i++) {
        const Uint8 want8 = i < 3000 ? data[i] : i < 5000 ? data[i - 3000]
                                                          : 0x80;
        bad += fake_audio_out[i] != want8;
    }
    CHECK(bad == 0 && fake_audio_outlen == 3 * 2048);
    CHECK(SDL_GetQueuedAudioSize(id) == 0);
    SDL_QueueAudio(id, data, 100);
    SDL_ClearQueuedAudio(id);
    CHECK(SDL_GetQueuedAudioSize(id) == 0);
    SDL_CloseAudioDevice(id);

    /* a callback's device takes no queue */
    want.callback = count_cb;
    id = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    CHECK(SDL_QueueAudio(id, data, 10) < 0);
    CHECK(SDL_QueueAudio(id + 1, data, 10) < 0);
    SDL_CloseAudioDevice(id);
}

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static void mixing(void)
{
    static const SDL_AudioFormat fmts[4] = { AUDIO_U8, AUDIO_S8, AUDIO_S16LSB,
                                             AUDIO_S16MSB };
    static const int vols[5] = { 0, 1, 64, 127, 128 };
    Uint8 src[256], dst[256], want[256];
    unsigned long seed = 99;
    int f, v, t, i, bad = 0;
    for (f = 0; f < 4; f++) {
        for (v = 0; v < 5; v++) {
            for (t = 0; t < 20; t++) {
                for (i = 0; i < 256; i++) {
                    seed = seed * 1103515245UL + 12345UL;
                    src[i] = (Uint8)(seed >> 16);
                    /* loud ones, to clamp */
                    dst[i] = (Uint8)(t & 1 ? (seed >> 24) | 0x60 : seed >> 8);
                }
                memcpy(want, dst, sizeof(dst));
                for (i = 0; i < 256; i++) {
                    if (fmts[f] == AUDIO_U8) {
                        /* SDL2: ((s - 128) * v) / 128 + 128, then
                         * d + s - 128 clamped */
                        const int s = ((src[i] - 128) * vols[v]) / 128 + 128;
                        want[i] = (Uint8)clamp(dst[i] + s - 128, 0, 255);
                    } else if (fmts[f] == AUDIO_S8) {
                        const int s = ((Sint8)src[i] * vols[v]) / 128;
                        want[i] = (Uint8)clamp((Sint8)dst[i] + s, -128, 127);
                    }
                }
                if (fmts[f] == AUDIO_S16LSB || fmts[f] == AUDIO_S16MSB) {
                    const int b = fmts[f] == AUDIO_S16MSB;
                    for (i = 0; i < 256; i += 2) {
                        const int s = (Sint16)(b ? src[i] << 8 | src[i + 1]
                                                 : src[i + 1] << 8 | src[i]);
                        const int d = (Sint16)(b ? dst[i] << 8 | dst[i + 1]
                                                 : dst[i + 1] << 8 | dst[i]);
                        const int m = clamp(d + (s * vols[v]) / 128, -32768,
                                            32767);
                        want[i + !b] = (Uint8)(m >> 8);
                        want[i + b] = (Uint8)m;
                    }
                }
                if (vols[v] == 0) {
                    memcpy(want, dst, sizeof(dst));
                }
                SDL_MixAudioFormat(dst, src, fmts[f], 256, vols[v]);
                bad += memcmp(dst, want, sizeof(dst)) != 0;
            }
        }
    }
    CHECK(bad == 0);
    /* an odd length leaves the half sample */
    dst[2] = 0x55;
    src[2] = 0x7F;
    SDL_MixAudioFormat(dst, src, AUDIO_S16LSB, 3, 128);
    CHECK(dst[2] == 0x55);
}

static void put32(Uint8 *p, Uint32 v)
{
    p[0] = (Uint8)v;
    p[1] = (Uint8)(v >> 8);
    p[2] = (Uint8)(v >> 16);
    p[3] = (Uint8)(v >> 24);
}

/* a WAV with a LIST chunk of odd size before its fmt, and a data chunk
 * holding two and a half frames */
static int make_wav(Uint8 *w, int bits, int tag)
{
    int n = 0;
    memcpy(w, "RIFF\0\0\0\0WAVE", 12);
    n = 12;
    memcpy(w + n, "LIST\3\0\0\0abc\0", 12);
    n += 12;
    memcpy(w + n, "fmt ", 4);
    put32(w + n + 4, 16);
    w[n + 8] = (Uint8)tag;
    w[n + 9] = 0;
    w[n + 10] = 2;                      /* stereo */
    w[n + 11] = 0;
    put32(w + n + 12, 22050);
    put32(w + n + 16, 22050 * 4);
    w[n + 20] = 4;
    w[n + 21] = 0;
    w[n + 22] = (Uint8)bits;
    w[n + 23] = 0;
    n += 24;
    memcpy(w + n, "data", 4);
    put32(w + n + 4, 10);
    memcpy(w + n + 8, "0123456789", 10);
    n += 18;
    put32(w + 4, (Uint32)n - 8);
    return n;
}

static void wavs(void)
{
    Uint8 w[128], *buf = NULL;
    Uint32 len = 0;
    SDL_AudioSpec spec;
    FILE *fp;
    int n = make_wav(w, 16, 1);
    CHECK(SDL_LoadWAV_RW(SDL_RWFromConstMem(w, n), 1, &spec, &buf, &len) ==
          &spec);
    CHECK(spec.freq == 22050 && spec.format == AUDIO_S16LSB &&
          spec.channels == 2 && spec.samples == 4096);
    CHECK(len == 8 && buf && memcmp(buf, "01234567", 8) == 0);
    SDL_FreeWAV(buf);

    n = make_wav(w, 24, 1);
    CHECK(SDL_LoadWAV_RW(SDL_RWFromConstMem(w, n), 1, &spec, &buf, &len) ==
          NULL && buf == NULL && strstr(SDL_GetError(), "24 bits"));
    n = make_wav(w, 16, 2);             /* ADPCM */
    CHECK(SDL_LoadWAV_RW(SDL_RWFromConstMem(w, n), 1, &spec, &buf, &len) ==
          NULL && strstr(SDL_GetError(), "PCM"));
    n = make_wav(w, 8, 1);
    w[8] = 'X';
    CHECK(SDL_LoadWAV_RW(SDL_RWFromConstMem(w, n), 1, &spec, &buf, &len) ==
          NULL && strstr(SDL_GetError(), "WAVE"));
    /* cut before its data */
    n = make_wav(w, 8, 1);
    CHECK(SDL_LoadWAV_RW(SDL_RWFromConstMem(w, 40), 1, &spec, &buf, &len) ==
          NULL);

    /* from a file, by SDL2's macro */
    n = make_wav(w, 8, 1);
    fp = fopen("test_wav.tmp", "wb");
    fwrite(w, 1, (size_t)n, fp);
    fclose(fp);
    CHECK(SDL_LoadWAV("test_wav.tmp", &spec, &buf, &len) == &spec);
    CHECK(spec.format == AUDIO_U8 && len == 10 && buf[9] == '9');
    SDL_FreeWAV(buf);
    remove("test_wav.tmp");
    CHECK(SDL_LoadWAV("test_wav.tmp", &spec, &buf, &len) == NULL &&
          strstr(SDL_GetError(), "Couldn't open"));
}

int main(void)
{
    fake_mach.mch_cookie = 0x00020000UL;
    CHECK(SDL_Init(SDL_INIT_AUDIO) == 0);
    feed();
    negotiation();
    queue();
    mixing();
    wavs();
    SDL_Quit();
    if (failures) {
        printf("test_audio: %d failures\n", failures);
        return 1;
    }
    printf("test_audio: ok\n");
    return 0;
}
