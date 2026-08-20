/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_Audio: STE/Mega STE DMA sample playback.
 *
 * Cooperative model, like the timers: the DMA loops over a ring
 * buffer in hardware, and the refill (which invokes the user
 * callback) happens inside STDL_PumpEvents / STDL_Delay / the
 * compat SDL_Delay. Keep the pump running at least every ~150ms
 * while audio plays. On a plain ST (no DMA hardware) STDL_OpenAudio
 * fails cleanly - most ports can ship silent first.
 */

#ifndef STDL_AUDIO_H
#define STDL_AUDIO_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdl/stdl_types.h>

/* format words match SDL 1.2 */
#define STDL_AUDIO_U8     0x0008
#define STDL_AUDIO_S8     0x8008
#define STDL_AUDIO_S16LSB 0x8010
#define STDL_AUDIO_S16MSB 0x9010
#define STDL_AUDIO_S16SYS STDL_AUDIO_S16MSB   /* 68000 is big-endian */

typedef struct STDL_AudioSpec {
    int      freq;
    uint16_t format;
    uint8_t  channels;      /* 1 or 2 */
    uint8_t  silence;
    uint16_t samples;       /* callback granularity hint */
    uint16_t padding;
    uint32_t size;
    void (*callback)(void *userdata, uint8_t *stream, int len);
    void    *userdata;
} STDL_AudioSpec;

typedef enum {
    STDL_AUDIO_STOPPED = 0,
    STDL_AUDIO_PLAYING,
    STDL_AUDIO_PAUSED
} STDL_audiostatus;

/*
 * Playback runs at the STE DMA rate nearest desired->freq (6258,
 * 12517, 25033 or 50066 Hz) with nearest-neighbour resampling from
 * the desired rate; feed data at an exact DMA rate (stdlconv wav)
 * for a pass-through. If `obtained` is non-NULL it receives the
 * actual spec; the callback always sees the *desired* format.
 */
int  STDL_OpenAudio(STDL_AudioSpec *desired, STDL_AudioSpec *obtained);
void STDL_CloseAudio(void);
void STDL_PauseAudio(int pause_on);
STDL_audiostatus STDL_GetAudioStatus(void);

/* Load an uncompressed PCM WAV (u8 / s16le, mono or stereo). For
 * anything else - ADPCM included - convert offline with
 * `stdlconv wav`. */
STDL_AudioSpec *STDL_LoadWAV(const char *file, STDL_AudioSpec *spec,
                             uint8_t **audio_buf, uint32_t *audio_len);
void STDL_FreeWAV(uint8_t *audio_buf);

/*
 * One-shot playback, the cheap path.
 *
 * STDL_OpenAudio is a mixing device: it loops a ring and the pump
 * refills it through your callback, which costs real CPU - on an
 * 8MHz machine, mixing four channels at 6258 Hz costs tens of
 * percent. A game whose frame budget is already spent cannot pay
 * that. STDL_PlaySample instead points the DMA straight at a buffer
 * you already hold and lets the hardware read it once, so playback
 * costs six register writes and nothing per frame.
 *
 * The price is that it is monophonic: a second call replaces
 * whatever is playing (do your own priority arbitration). `data`
 * must be signed 8-bit mono at an even address, stay valid until
 * playback ends, and be sampled at an exact DMA rate - `freq`
 * snaps to the nearest of 6258/12517/25033/50066. `bytes` is
 * rounded down to even.
 *
 * Returns 0, or -1 with STDL_GetError set: no DMA hardware, or
 * another device (STDL_OpenAudio ring, STDL_OpenVoices) currently
 * owns the chip - the DMA has one channel and its users cannot
 * coexist.
 *
 * Ownership sharp edge: the hardware reads the buffer live for the
 * whole playback. Call STDL_StopSample (or start a replacement)
 * BEFORE freeing or rewriting a buffer handed to either call - on
 * real hardware a freed block may be reused and scribbled while the
 * DMA is still fetching it.
 */
int  STDL_PlaySample(const void *data, uint32_t bytes, int freq);

/* Looping variant: replays the buffer until STDL_StopSample (or a
 * replacement Play*). Ambient loops and simple music beds at zero
 * per-frame CPU cost; STDL_SamplePlaying stays true while looping. */
int  STDL_PlaySampleLoop(const void *data, uint32_t bytes, int freq);

void STDL_StopSample(void);
int  STDL_SamplePlaying(void);

#ifdef __cplusplus
}
#endif

#endif /* STDL_AUDIO_H */
