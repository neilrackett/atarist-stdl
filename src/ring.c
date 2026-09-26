/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_OpenAudio: the ring device, a continuous stream from a user
 * callback (SDL_OpenAudio's model).
 *
 * The DMA loops continuously over a ring buffer (repeat mode); the
 * cooperative pump watches the frame address counter and refills
 * whichever half is not being played, pulling data through the user
 * callback in the *desired* format and converting/resampling to the
 * hardware's signed 8-bit at the nearest DMA rate. No interrupts.
 * A callback already in the hardware's format - signed 8-bit at a DMA
 * rate - writes straight into the ring.
 */

#include <stdlib.h>
#include <string.h>
#include "stdl_internal.h"

/* ring of RING_FRAMES sample frames, refilled by halves */
#define RING_FRAMES 4096

static struct {
    int      open;
    int      paused;
    STDL_AudioSpec spec;        /* user's desired spec */
    int      dma_freq;
    uint8_t  dma_mode;          /* rate bits | mono flag */
    int      frame_bytes_dma;   /* 1 mono, 2 stereo */
    int      frame_bytes_user;
    int8_t  *ring;
    void    *ring_alloc;
    uint32_t ring_bytes;
    uint8_t *userbuf;
    uint32_t userbuf_max;
    int      filled_half;
    uint32_t rate_err;
    /* the callback's format is the hardware's (signed 8-bit at the
     * DMA rate): it writes straight into the ring */
    uint8_t  direct;
} au;

/* emergency stop for restore_all: the DMA must never keep looping
 * over freed RAM after the program exits */
static void ring_shutdown(void)
{
    stdl_dma_stop();
    stdl_audio_hook = NULL;
    au.open = 0;
    stdl.dma_owner = STDL_DMA_FREE;
}

/*
 * Pull `frames` frames from the callback and write them as signed
 * 8-bit DMA frames at `dst`, nearest-neighbour resampled. A callback
 * in the hardware's format writes into the ring itself; anything else
 * goes through stdl_audio_convert (wav.c), whose comment tells how
 * this pass once cost 37% of an 8MHz STE with an empty callback.
 */
static void fill_frames(int8_t *dst, int frames)
{
    int user_frames;
    uint32_t want;
    int fb = au.frame_bytes_user;
    uint8_t *buf = au.direct ? (uint8_t *)dst : au.userbuf;

    /* how many source frames cover this stretch of DMA frames; on
     * the direct path, exactly `frames` */
    want = (uint32_t)frames * (uint32_t)au.spec.freq + au.rate_err;
    user_frames = (int)(want / (uint32_t)au.dma_freq);
    au.rate_err = want % (uint32_t)au.dma_freq;

    if (au.spec.callback == NULL || user_frames <= 0) {
        memset(dst, 0, (size_t)frames * au.frame_bytes_dma);
        return;
    }
    if (!au.direct && (uint32_t)user_frames * fb > au.userbuf_max) {
        user_frames = (int)(au.userbuf_max / fb);
    }
    memset(buf, au.spec.silence, (size_t)user_frames * fb);
    au.spec.callback(au.spec.userdata, buf, user_frames * fb);
    if (!au.direct) {
        stdl_audio_convert(dst, (uint32_t)frames, buf, (uint32_t)user_frames,
                           au.spec.format, au.spec.channels, 0);
    }
}

static void stdl_audio_pump(void)
{
    uint32_t off;
    int playing_half;

    if (!au.open || au.paused) {
        return;
    }
    off = stdl_dma_counter() - (uint32_t)(uintptr_t)au.ring;
    if (off >= au.ring_bytes) {
        return;                 /* counter mid-reload; try next pump */
    }
    playing_half = (off < au.ring_bytes / 2) ? 0 : 1;
    if (au.filled_half == playing_half) {
        int half_frames = RING_FRAMES / 2;
        int other = playing_half ^ 1;
        fill_frames(au.ring
                    + (uint32_t)other * (au.ring_bytes / 2),
                    half_frames);
        au.filled_half = other;
    }
}

/* ---------------------------------------------------------------- */

int STDL_OpenAudio(STDL_AudioSpec *desired, STDL_AudioSpec *obtained)
{
    int best;

    if (au.open) {
        STDL_SetError("audio already open");
        return -1;
    }
    STDL_SamplePlaying();       /* expire a finished one-shot */
    if (stdl.dma_owner != STDL_DMA_FREE) {
        STDL_SetError("sound DMA in use");
        return -1;
    }
    if (!stdl.initialised) {
        STDL_Init(STDL_INIT_AUDIO);
    }
    if (!stdl.mach.is_ste) {
        STDL_SetError("no DMA sound hardware (STE/Mega STE only)");
        return -1;
    }
    if (desired == NULL || desired->channels < 1
        || desired->channels > 2) {
        STDL_SetError("bad audio spec");
        return -1;
    }
    switch (desired->format) {
        case STDL_AUDIO_U8:
        case STDL_AUDIO_S8:
        case STDL_AUDIO_S16LSB:
        case STDL_AUDIO_S16MSB:
            break;
        default:
            STDL_SetError("unsupported audio format");
            return -1;
    }

    memset(&au, 0, sizeof(au));
    au.spec = *desired;
    if (au.spec.samples == 0) {
        au.spec.samples = 1024;
    }

    best = stdl_dma_nearest(au.spec.freq);
    au.dma_freq = stdl_dma_rates[best];
    au.dma_mode = (uint8_t)(best
                  | (au.spec.channels == 1 ? 0x80 : 0x00));
    au.frame_bytes_dma = au.spec.channels;
    au.frame_bytes_user = au.spec.channels
        * ((au.spec.format & 0xFF) / 8);
    au.direct = (uint8_t)(au.spec.format == STDL_AUDIO_S8
                          && au.spec.freq == au.dma_freq);

    au.ring_bytes = (uint32_t)RING_FRAMES * au.frame_bytes_dma;
    au.ring_alloc = stdl_stram_alloc(au.ring_bytes + 2);
    /* a half ring's worth of the callback's frames, and one over for
     * the rate's remainder; the direct path needs none */
    if (!au.direct) {
        au.userbuf_max = ((uint32_t)(RING_FRAMES / 2) * (uint32_t)au.spec.freq
                          / (uint32_t)au.dma_freq + 1) * (uint32_t)au.frame_bytes_user;
        au.userbuf = malloc(au.userbuf_max);
    }
    if (au.ring_alloc == NULL || (!au.direct && au.userbuf == NULL)) {
        stdl_stram_free(au.ring_alloc);
        free(au.userbuf);
        STDL_SetError("out of memory for audio buffers");
        return -1;
    }
    /* the DMA needs an even start address */
    au.ring = (int8_t *)(((uintptr_t)au.ring_alloc + 1) & ~(uintptr_t)1);

    au.spec.silence = (au.spec.format == STDL_AUDIO_U8) ? 0x80 : 0;
    au.spec.size = (uint32_t)au.spec.samples
                 * au.frame_bytes_user;
    *desired = au.spec;
    if (obtained != NULL) {
        *obtained = au.spec;
        obtained->freq = au.dma_freq;
        obtained->format = STDL_AUDIO_S8;
        obtained->silence = 0;
    }

    /* prime the whole ring; playback, and the output levels with
     * it, start at STDL_PauseAudio(0) */
    fill_frames(au.ring, RING_FRAMES / 2);
    fill_frames(au.ring + au.ring_bytes / 2, RING_FRAMES / 2);
    au.filled_half = 1;
    au.open = 1;
    au.paused = 1;              /* SDL semantics: starts paused */
    stdl.dma_owner = STDL_DMA_RING;
    stdl_audio_hook = stdl_audio_pump;
    stdl_shutdown_audio = ring_shutdown;
    return 0;
}

void STDL_PauseAudio(int pause_on)
{
    if (!au.open) {
        return;
    }
    if (pause_on && !au.paused) {
        stdl_dma_stop();
        au.paused = 1;
    } else if (!pause_on && au.paused) {
        stdl_dma_start(au.ring, au.ring_bytes, au.dma_mode, 1);
        au.filled_half = 1;
        au.paused = 0;
    }
}

STDL_audiostatus STDL_GetAudioStatus(void)
{
    if (!au.open) {
        return STDL_AUDIO_STOPPED;
    }
    return au.paused ? STDL_AUDIO_PAUSED : STDL_AUDIO_PLAYING;
}

void STDL_CloseAudio(void)
{
    if (!au.open) {
        return;
    }
    stdl_dma_stop();
    stdl_audio_hook = NULL;
    if (stdl.dma_owner == STDL_DMA_RING) {
        stdl.dma_owner = STDL_DMA_FREE;
    }
    free(au.userbuf);
    stdl_stram_free(au.ring_alloc);
    memset(&au, 0, sizeof(au));
}
