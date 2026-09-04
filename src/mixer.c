/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * SDL_mixer compatibility subset: YM music through STDL_Music,
 * sample chunks mixed in software over the STDL_Audio DMA callback.
 */

#include <stdlib.h>
#include <string.h>
#include "stdl_internal.h"
#include <SDL.h>
#include <SDL_mixer.h>

static int mix_open;            /* Mix_OpenAudio succeeded          */
static int chunks_ok;           /* DMA device opened for chunks     */
static int device_freq;

typedef struct {
    Mix_Chunk *chunk;
    uint32_t   pos;
    int        loops;           /* remaining repeats after this one */
    uint8_t    volume;          /* channel volume 0..128            */
    uint8_t    active;
} mix_channel_t;

static mix_channel_t channels[MIX_CHANNELS];

/* mix all active chunks into the signed 8-bit mono stream */
static void mix_callback(void *userdata, uint8_t *stream, int len)
{
    int i, c;

    (void)userdata;
    memset(stream, 0, (size_t)len);
    for (c = 0; c < MIX_CHANNELS; c++) {
        mix_channel_t *ch = &channels[c];
        int8_t *out = (int8_t *)stream;
        const int8_t *abuf;
        uint32_t pos, alen;
        int gain, full;

        if (!ch->active || ch->chunk == NULL) {
            continue;
        }
        abuf = (const int8_t *)ch->chunk->abuf;
        alen = ch->chunk->alen;
        pos = ch->pos;
        gain = ch->chunk->volume * ch->volume;   /* 0..16384 */
        full = (gain == 128 * 128);

        for (i = 0; i < len; i++) {
            int s, v;
            if (pos >= alen) {
                if (ch->loops != 0) {
                    if (ch->loops > 0) {
                        ch->loops--;
                    }
                    pos = 0;
                } else {
                    ch->active = 0;
                    break;
                }
            }
            s = abuf[pos++];
            if (!full) {
                s = stdl_mul16(s, gain) >> 14;    /* muls.w, not __mulsi3 */
            }
            v = out[i] + s;
            if (v > 127) v = 127;
            if (v < -128) v = -128;
            out[i] = (int8_t)v;
        }
        ch->pos = pos;
    }
}

int Mix_OpenAudio(int frequency, uint16_t format, int channelcount,
                  int chunksize)
{
    STDL_AudioSpec desired, obtained;
    int c;

    (void)format; (void)channelcount; (void)chunksize;
    if (mix_open) {
        return 0;
    }
    STDL_Init(STDL_INIT_AUDIO);
    for (c = 0; c < MIX_CHANNELS; c++) {
        channels[c].active = 0;
        channels[c].volume = MIX_MAX_VOLUME;
    }

    /* the chunk device is mono signed 8-bit at the nearest DMA
     * rate; on a plain ST this fails and only music is available */
    memset(&desired, 0, sizeof(desired));
    desired.freq = frequency > 0 ? frequency : MIX_DEFAULT_FREQUENCY;
    desired.format = STDL_AUDIO_S8;
    desired.channels = 1;
    desired.samples = 1024;
    desired.callback = mix_callback;
    if (STDL_OpenAudio(&desired, &obtained) == 0) {
        chunks_ok = 1;
        device_freq = obtained.freq;
        STDL_PauseAudio(0);
    } else {
        chunks_ok = 0;
        device_freq = 0;
    }
    mix_open = 1;
    return 0;
}

void Mix_CloseAudio(void)
{
    if (!mix_open) {
        return;
    }
    STDL_HaltMusic();
    if (chunks_ok) {
        STDL_CloseAudio();
        chunks_ok = 0;
    }
    mix_open = 0;
}

/* ---------------------------------------------------------------- */
/* music                                                            */

Mix_Music *Mix_LoadMUS(const char *file)
{
    return STDL_LoadMusic(file);
}

void Mix_FreeMusic(Mix_Music *music)
{
    STDL_FreeMusic(music);
}

int Mix_PlayMusic(Mix_Music *music, int loops)
{
    return STDL_PlayMusic(music, loops);
}

int Mix_HaltMusic(void)
{
    STDL_HaltMusic();
    return 0;
}

void Mix_PauseMusic(void)   { STDL_PauseMusic(); }
void Mix_ResumeMusic(void)  { STDL_ResumeMusic(); }
int  Mix_PausedMusic(void)  { return STDL_PausedMusic(); }
int  Mix_PlayingMusic(void) { return STDL_PlayingMusic(); }

int Mix_VolumeMusic(int volume)
{
    return STDL_VolumeMusic(volume);
}

/* ---------------------------------------------------------------- */
/* chunks                                                           */

/* convert any loadable WAV to signed 8-bit mono at the device rate */
Mix_Chunk *Mix_LoadWAV(const char *file)
{
    STDL_AudioSpec spec;
    uint8_t *raw;
    uint32_t rawlen;
    Mix_Chunk *chunk;
    uint32_t in_frames, out_frames;
    int frame_bytes;

    if (!chunks_ok) {
        STDL_SetError("no DMA sound hardware for sample chunks");
        return NULL;
    }
    if (STDL_LoadWAV(file, &spec, &raw, &rawlen) == NULL) {
        return NULL;
    }
    frame_bytes = spec.channels * ((spec.format & 0xFF) / 8);
    in_frames = rawlen / (uint32_t)frame_bytes;
    out_frames = (uint32_t)((uint64_t)in_frames * (uint32_t)device_freq
                            / (uint32_t)spec.freq);
    chunk = calloc(1, sizeof(Mix_Chunk));
    if (chunk == NULL || out_frames == 0) {
        free(chunk);
        STDL_FreeWAV(raw);
        STDL_SetError("out of memory");
        return NULL;
    }
    chunk->abuf = malloc(out_frames);
    if (chunk->abuf == NULL) {
        free(chunk);
        STDL_FreeWAV(raw);
        STDL_SetError("out of memory");
        return NULL;
    }
    stdl_audio_convert((int8_t *)chunk->abuf, out_frames, raw,
                       in_frames, spec.format, spec.channels, 1);
    chunk->alen = out_frames;
    chunk->allocated = 1;
    chunk->volume = MIX_MAX_VOLUME;
    STDL_FreeWAV(raw);
    return chunk;
}

void Mix_FreeChunk(Mix_Chunk *chunk)
{
    int c;

    if (chunk == NULL) {
        return;
    }
    for (c = 0; c < MIX_CHANNELS; c++) {
        if (channels[c].chunk == chunk) {
            channels[c].active = 0;
            channels[c].chunk = NULL;
        }
    }
    free(chunk->abuf);
    free(chunk);
}

int Mix_PlayChannel(int channel, Mix_Chunk *chunk, int loops)
{
    int c;

    if (!chunks_ok) {
        STDL_SetError("no DMA sound hardware for sample chunks");
        return -1;
    }
    if (chunk == NULL) {
        return -1;
    }
    if (channel < 0) {
        for (c = 0; c < MIX_CHANNELS; c++) {
            if (!channels[c].active) {
                channel = c;
                break;
            }
        }
        if (channel < 0) {
            channel = 0;        /* all busy: steal channel 0 */
        }
    }
    if (channel >= MIX_CHANNELS) {
        STDL_SetError("no such mixer channel");
        return -1;
    }
    channels[channel].chunk = chunk;
    channels[channel].pos = 0;
    channels[channel].loops = loops;
    channels[channel].active = 1;
    return channel;
}

int Mix_HaltChannel(int channel)
{
    int c;

    for (c = 0; c < MIX_CHANNELS; c++) {
        if (channel < 0 || channel == c) {
            channels[c].active = 0;
        }
    }
    return 0;
}

int Mix_Playing(int channel)
{
    int c, n = 0;

    if (channel >= 0) {
        return (channel < MIX_CHANNELS && channels[channel].active)
            ? 1 : 0;
    }
    for (c = 0; c < MIX_CHANNELS; c++) {
        n += channels[c].active ? 1 : 0;
    }
    return n;
}

static uint8_t clamp_vol(int volume)
{
    return (uint8_t)(volume > MIX_MAX_VOLUME ? MIX_MAX_VOLUME
                                             : volume);
}

int Mix_Volume(int channel, int volume)
{
    int c, old = 0;

    if (channel >= MIX_CHANNELS) {
        return 0;
    }
    for (c = 0; c < MIX_CHANNELS; c++) {
        if (channel < 0 || channel == c) {
            old = channels[c].volume;
            if (volume >= 0) {
                channels[c].volume = clamp_vol(volume);
            }
        }
    }
    return old;
}

int Mix_VolumeChunk(Mix_Chunk *chunk, int volume)
{
    int old;

    if (chunk == NULL) {
        return 0;
    }
    old = chunk->volume;
    if (volume >= 0) {
        chunk->volume = clamp_vol(volume);
    }
    return old;
}
