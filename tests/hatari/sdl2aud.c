/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * On-target check of the SDL2 add-on's audio (addons/sdl2/src/audio.c)
 * on STDL's ring and a TT's or a Falcon's DMA sound. For each spec it
 * opens the device, unpauses it and pumps with SDL_Delay for eight
 * seconds, then reports whether every callback got the obtained size,
 * and the rate the callback was drained at against the rate it asked
 * for: the DMA plays at its own rate and the ring resamples, so the
 * program's bytes must still go at its own. The ring is refilled a
 * half (2,048 DMA frames) at a time, so the rate is counted in whole
 * halves: 1-2% short over eight seconds, and it must be within 3%. A 16-bit stereo PC game's
 * spec at 22050, the hardware's signed 8 bits at a DMA rate, a rate
 * a Falcon lacks (6258), and the queue.
 *
 *   STCMD_NO_TTY=1 stcmd make sdl2
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -m68020-60 -msoft-float -Iinclude -Iaddons/sdl2/include/SDL2 \
 *       -c -o obj/sdl2aud.o tests/hatari/sdl2aud.c
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -m68000 \
 *       -o dist/SDL2AUD.TOS obj/sdl2aud.o libstdl-sdl2.a libstdl.a
 *   TOS="<TOS 3.06>" MACHINE=tt LATE="--mmu off" \
 *       EXTRA="--cpu-exact on --compatible on --memsize 4" \
 *       tests/hatari/run.sh s2a dist/SDL2AUD.TOS 8 "waitfor S2A:done"
 */

#include "SDL.h"

#include <stdio.h>
#include "../../examples/hold.h"

static Uint32 bytes, calls, wrong, expect;
static Uint8 phase;

static void cb(void *userdata, Uint8 *stream, int len)
{
    int i;
    (void)userdata;
    calls++;
    wrong += (Uint32)len != expect;
    bytes += (Uint32)len;
    /* a quiet ramp, whatever the format */
    for (i = 0; i < len; i++) {
        stream[i] = (Uint8)((phase++ >> 3) & 7);
    }
}

static int failures;

/* eight seconds' pumping; the rate in frames a second */
static long drain(SDL_AudioDeviceID id, int frame)
{
    Uint32 t0, b0, t1;
    SDL_PauseAudioDevice(id, 0);
    /* past the primed silence */
    SDL_Delay(500);
    t0 = SDL_GetTicks();
    b0 = bytes;
    while (SDL_GetTicks() - t0 < 8000) {
        SDL_Delay(20);
    }
    t1 = SDL_GetTicks();
    return (long)((bytes - b0) / (Uint32)frame) * 1000L / (long)(t1 - t0);
}

static void one(const char *name, int freq, SDL_AudioFormat fmt, int ch,
                int samples, int allowed)
{
    SDL_AudioSpec want, got;
    SDL_AudioDeviceID id;
    long rate, err;
    int frame;
    SDL_zero(want);
    want.freq = freq;
    want.format = fmt;
    want.channels = (Uint8)ch;
    want.samples = (Uint16)samples;
    want.callback = cb;
    id = SDL_OpenAudioDevice(NULL, 0, &want, &got, allowed);
    if (!id) {
        printf("S2A: %s FAIL open: %s\n", name, SDL_GetError());
        failures++;
        return;
    }
    bytes = calls = wrong = 0;
    expect = got.size;
    frame = got.channels * (SDL_AUDIO_BITSIZE(got.format) / 8);
    rate = drain(id, frame);
    err = (rate - got.freq) * 1000L / got.freq;
    if (err < 0) {
        err = -err;
    }
    printf("S2A: %s got %dHz %04X %dch %u samples %lu bytes: %lu calls, "
           "%lu wrong, drained at %ldHz (%ld.%ld%% off) %s\n",
           name, got.freq, got.format, got.channels, got.samples,
           (unsigned long)got.size, (unsigned long)calls,
           (unsigned long)wrong, rate, err / 10, err % 10,
           (wrong || calls < 4 || err > 30) ? "FAIL" : "ok");
    failures += wrong || calls < 4 || err > 30;
    SDL_CloseAudioDevice(id);
}

static void queued(void)
{
    static Uint8 data[12517];
    SDL_AudioSpec want, got;
    SDL_AudioDeviceID id;
    Uint32 left, t0;
    unsigned i;
    SDL_zero(want);
    want.freq = 12517;
    want.format = AUDIO_U8;
    want.channels = 1;
    want.samples = 512;
    id = SDL_OpenAudioDevice(NULL, 0, &want, &got, 0);
    if (!id) {
        printf("S2A: queue FAIL open: %s\n", SDL_GetError());
        failures++;
        return;
    }
    for (i = 0; i < sizeof(data); i++) {
        data[i] = (Uint8)(0x80 + ((i >> 4) & 7));
    }
    /* a second's worth, half of it gone after half a second (and the
     * ring's lead), all of it after a second and a half */
    SDL_QueueAudio(id, data, sizeof(data));
    SDL_PauseAudioDevice(id, 0);
    t0 = SDL_GetTicks();
    while (SDL_GetTicks() - t0 < 500) {
        SDL_Delay(20);
    }
    left = SDL_GetQueuedAudioSize(id);
    while (SDL_GetTicks() - t0 < 1500) {
        SDL_Delay(20);
    }
    printf("S2A: queue of %u bytes: %lu left at 0.5s, %lu at 1.5s %s\n",
           (unsigned)sizeof(data), (unsigned long)left,
           (unsigned long)SDL_GetQueuedAudioSize(id),
           (left > 2000 && left < 9000 && !SDL_GetQueuedAudioSize(id))
               ? "ok" : "FAIL");
    failures += !(left > 2000 && left < 9000 && !SDL_GetQueuedAudioSize(id));
    SDL_CloseAudioDevice(id);
}

int main(int argc, char *argv[])
{
    (void)argc;
    (void)argv;

    hold_on_exit("SDL2AUD");
    if (SDL_Init(SDL_INIT_AUDIO) < 0) {
        printf("S2A: %s\nS2A:done\n", SDL_GetError());
        return 1;
    }
    one("s16 stereo 22050", 22050, AUDIO_S16LSB, 2, 1024, 0);
    one("s8 mono, a DMA rate", 12000, AUDIO_S8, 1, 512,
        SDL_AUDIO_ALLOW_FREQUENCY_CHANGE);
    one("u8 mono 6258", 6258, AUDIO_U8, 1, 256, 0);
    queued();
    printf("S2A: %d failures\nS2A:done\n", failures);
    SDL_Quit();
    return failures ? 1 : 0;
}
