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

#endif /* STDL_AUDIO_H */
