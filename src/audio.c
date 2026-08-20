/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_Audio: STE/Mega STE DMA sample playback.
 *
 * The DMA loops continuously over a ring buffer (repeat mode); the
 * cooperative pump watches the frame address counter and refills
 * whichever half is not being played, pulling data through the user
 * callback in the *desired* format and converting/resampling to the
 * hardware's signed 8-bit at the nearest DMA rate. No interrupts.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stdl_internal.h"

#define DMA_CTRL   (*(volatile uint8_t *)0xFFFF8901UL)
#define DMA_START_H (*(volatile uint8_t *)0xFFFF8903UL)
#define DMA_START_M (*(volatile uint8_t *)0xFFFF8905UL)
#define DMA_START_L (*(volatile uint8_t *)0xFFFF8907UL)
#define DMA_CNT_H  (*(volatile uint8_t *)0xFFFF8909UL)
#define DMA_CNT_M  (*(volatile uint8_t *)0xFFFF890BUL)
#define DMA_CNT_L  (*(volatile uint8_t *)0xFFFF890DUL)
#define DMA_END_H  (*(volatile uint8_t *)0xFFFF890FUL)
#define DMA_END_M  (*(volatile uint8_t *)0xFFFF8911UL)
#define DMA_END_L  (*(volatile uint8_t *)0xFFFF8913UL)
#define DMA_MODE   (*(volatile uint8_t *)0xFFFF8921UL)
#define MW_DATA    (*(volatile uint16_t *)0xFFFF8922UL)
#define MW_MASK    (*(volatile uint16_t *)0xFFFF8924UL)

#define DMA_PLAY_ONCE   0x01
#define DMA_PLAY_REPEAT 0x03    /* enable + loop */

const int stdl_dma_rates[4] = { 6258, 12517, 25033, 50066 };

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
    /* decode parameters, fixed at open: byte offset of the s8-
     * significant byte in a channel sample, xor for sign, bytes
     * per channel sample */
    uint8_t  dec_off;
    uint8_t  dec_xor;
    uint8_t  dec_csize;
} au;

/* per-format decode parameters (offset of the high byte, sign
 * flip, channel sample size) */
static void decode_params(uint16_t format, uint8_t *off,
                          uint8_t *xr, uint8_t *csize)
{
    switch (format) {
        case STDL_AUDIO_U8:
            *off = 0; *xr = 0x80; *csize = 1; break;
        case STDL_AUDIO_S8:
            *off = 0; *xr = 0; *csize = 1; break;
        case STDL_AUDIO_S16LSB:
            *off = 1; *xr = 0; *csize = 2; break;
        default: /* S16MSB */
            *off = 0; *xr = 0; *csize = 2; break;
    }
}

/*
 * Bulk conversion to signed 8-bit at a new rate (nearest
 * neighbour). Used by the SDL_mixer chunk loader; keeps all the
 * format knowledge in one place.
 */
void stdl_audio_convert(int8_t *dst, uint32_t dst_frames,
                        const uint8_t *src, uint32_t src_frames,
                        uint16_t format, int channels, int mono_mix)
{
    uint8_t off, xr, csize;
    uint32_t j, acc = 0;
    int fb;

    decode_params(format, &off, &xr, &csize);
    fb = channels * csize;
    src += off;
    for (j = 0; j < dst_frames; j++) {
        if (mono_mix && channels == 2) {
            int a = (int8_t)(src[0] ^ xr);
            int b = (int8_t)(src[csize] ^ xr);
            *dst++ = (int8_t)((a + b) >> 1);
        } else {
            int c;
            for (c = 0; c < channels; c++) {
                *dst++ = (int8_t)(src[c * csize] ^ xr);
            }
        }
        acc += src_frames;
        while (acc >= dst_frames) {
            acc -= dst_frames;
            src += fb;
        }
    }
}

/* one-shot playback state (STDL_PlaySample); shares the chip with
 * the ring device, which is why only one of them may be open */
static int sample_active;

/* emergency stop for restore_all: the DMA must never keep looping
 * over freed RAM after the program exits */
static void audio_shutdown(void)
{
    DMA_CTRL = 0;
    stdl_audio_hook = NULL;
    au.open = 0;
    sample_active = 0;
    stdl.dma_owner = STDL_DMA_FREE;
}

/* index of the DMA rate nearest `freq` */
int stdl_dma_nearest(int freq)
{
    int i, best = 0, bestdiff = 0x7FFFFFFF;

    for (i = 0; i < 4; i++) {
        int diff = stdl_dma_rates[i] - freq;
        if (diff < 0) diff = -diff;
        if (diff < bestdiff) {
            bestdiff = diff;
            best = i;
        }
    }
    return best;
}

void stdl_dma_stop(void)
{
    DMA_CTRL = 0;
}

/* ---------------------------------------------------------------- */

static void microwire_write(uint16_t data);

/* master and both channels to 0dB */
static void set_output_levels(void)
{
    microwire_write(0x4E8);     /* %10 011 101000  master 0dB   */
    microwire_write(0x554);     /* %10 101 010100  left 0dB     */
    microwire_write(0x514);     /* %10 100 010100  right 0dB    */
}

static void microwire_write(uint16_t data)
{
    long timeout = 10000;

    MW_MASK = 0x07FF;
    MW_DATA = data;
    /* the mask rotates during the ~16us transfer; wait it out */
    while (MW_MASK != 0x07FF && --timeout > 0)
        ;
}

uint32_t stdl_dma_counter(void)
{
    uint8_t h, m, l, h2;

    do {
        h = DMA_CNT_H;
        m = DMA_CNT_M;
        l = DMA_CNT_L;
        h2 = DMA_CNT_H;
    } while (h != h2);
    return ((uint32_t)h << 16) | ((uint32_t)m << 8) | l;
}

/*
 * Program and start playback. Programming order matters: stop
 * first, because the address registers latch into the counter when
 * playback starts, and writing them under a running DMA can be
 * picked up mid-frame.
 */
void stdl_dma_start(const void *data, uint32_t bytes, uint8_t mode,
                    int repeat)
{
    uint32_t start = (uint32_t)data;
    uint32_t end = start + bytes;

    DMA_CTRL = 0;
    DMA_MODE = mode;
    DMA_START_H = (uint8_t)(start >> 16);
    DMA_START_M = (uint8_t)(start >> 8);
    DMA_START_L = (uint8_t)start;
    DMA_END_H = (uint8_t)(end >> 16);
    DMA_END_M = (uint8_t)(end >> 8);
    DMA_END_L = (uint8_t)end;
    set_output_levels();
    DMA_CTRL = repeat ? DMA_PLAY_REPEAT : DMA_PLAY_ONCE;
}

/* pull `frames` frames from the callback and write them as signed
 * 8-bit DMA frames at `dst`, nearest-neighbour resampled */
static void fill_frames(int8_t *dst, int frames)
{
    int user_frames;
    uint32_t want;
    int fb = au.frame_bytes_user;
    int j;

    /* how many source frames cover this stretch of DMA frames */
    want = (uint32_t)frames * (uint32_t)au.spec.freq + au.rate_err;
    user_frames = (int)(want / (uint32_t)au.dma_freq);
    au.rate_err = want % (uint32_t)au.dma_freq;

    if (au.spec.callback == NULL || user_frames <= 0) {
        memset(dst, 0, (size_t)frames * au.frame_bytes_dma);
        return;
    }
    if ((uint32_t)user_frames * fb > au.userbuf_max) {
        user_frames = (int)(au.userbuf_max / fb);
    }
    memset(au.userbuf, au.spec.silence, (size_t)user_frames * fb);
    au.spec.callback(au.spec.userdata, au.userbuf, user_frames * fb);

    /* resample with an error accumulator - no divides in the loop,
     * and the format dispatch is two precomputed constants */
    {
        const uint8_t *f = au.userbuf + au.dec_off;
        uint8_t xr = au.dec_xor;
        uint8_t csize = au.dec_csize;
        int ch = au.spec.channels;
        uint32_t acc = 0;

        for (j = 0; j < frames; j++) {
            *dst++ = (int8_t)(f[0] ^ xr);
            if (ch == 2) {
                *dst++ = (int8_t)(f[csize] ^ xr);
            }
            acc += (uint32_t)user_frames;
            while (acc >= (uint32_t)frames) {
                acc -= (uint32_t)frames;
                f += fb;
            }
        }
    }
}

static void stdl_audio_pump(void)
{
    uint32_t off;
    int playing_half;

    if (!au.open || au.paused) {
        return;
    }
    off = stdl_dma_counter() - (uint32_t)au.ring;
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
    decode_params(au.spec.format, &au.dec_off, &au.dec_xor,
                  &au.dec_csize);

    au.ring_bytes = (uint32_t)RING_FRAMES * au.frame_bytes_dma;
    au.ring_alloc = malloc(au.ring_bytes + 2);
    au.userbuf_max = ((uint32_t)(RING_FRAMES / 2)
        * ((uint32_t)au.spec.freq / au.dma_freq + 2))
        * au.frame_bytes_user;
    au.userbuf = malloc(au.userbuf_max);
    if (au.ring_alloc == NULL || au.userbuf == NULL) {
        free(au.ring_alloc);
        free(au.userbuf);
        STDL_SetError("out of memory for audio buffers");
        return -1;
    }
    /* the DMA needs an even start address */
    au.ring = (int8_t *)(((uint32_t)au.ring_alloc + 1) & ~1UL);

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

    set_output_levels();

    /* prime the whole ring, then start looping playback */
    fill_frames(au.ring, RING_FRAMES / 2);
    fill_frames(au.ring + au.ring_bytes / 2, RING_FRAMES / 2);
    au.filled_half = 1;
    au.open = 1;
    au.paused = 1;              /* SDL semantics: starts paused */
    stdl.dma_owner = STDL_DMA_RING;
    stdl_audio_hook = stdl_audio_pump;
    stdl_shutdown_audio = audio_shutdown;
    return 0;
}

void STDL_PauseAudio(int pause_on)
{
    if (!au.open) {
        return;
    }
    if (pause_on && !au.paused) {
        DMA_CTRL = 0;
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

/* ---------------------------------------------------------------- */
/* one-shot playback: the DMA reads the caller's buffer once        */

static int sample_start(const void *data, uint32_t bytes, int freq,
                        int repeat)
{
    if (!stdl.initialised) {
        STDL_Init(STDL_INIT_AUDIO);
    }
    if (!stdl.mach.is_ste) {
        STDL_SetError("no DMA sound hardware (STE/Mega STE only)");
        return -1;
    }
    STDL_SamplePlaying();       /* expire a finished one-shot */
    if (stdl.dma_owner != STDL_DMA_FREE
        && stdl.dma_owner != STDL_DMA_SAMPLE) {
        STDL_SetError("sound DMA in use");
        return -1;
    }
    bytes &= ~1UL;              /* the counter works in words */
    if (data == NULL || bytes < 2 || ((uint32_t)data & 1) != 0) {
        STDL_SetError("bad sample buffer");
        return -1;
    }
    stdl_dma_start(data, bytes,
                   (uint8_t)(stdl_dma_nearest(freq) | 0x80), /* mono */
                   repeat);
    sample_active = 1;
    stdl.dma_owner = STDL_DMA_SAMPLE;
    stdl_shutdown_audio = audio_shutdown;
    return 0;
}

int STDL_PlaySample(const void *data, uint32_t bytes, int freq)
{
    return sample_start(data, bytes, freq, 0);
}

/*
 * Looping variant: the DMA replays the buffer until STDL_StopSample
 * (or another Play*) - ambient loops and simple music beds at zero
 * per-frame CPU cost. As with STDL_PlaySample the hardware reads the
 * buffer live: stop playback before freeing or rewriting it.
 */
int STDL_PlaySampleLoop(const void *data, uint32_t bytes, int freq)
{
    return sample_start(data, bytes, freq, 1);
}

void STDL_StopSample(void)
{
    if (sample_active) {
        DMA_CTRL = 0;
        sample_active = 0;
        if (stdl.dma_owner == STDL_DMA_SAMPLE) {
            stdl.dma_owner = STDL_DMA_FREE;
        }
    }
}

int STDL_SamplePlaying(void)
{
    if (!sample_active) {
        return 0;
    }
    /* in play-once mode the hardware clears the enable bit when it
     * reaches the end address - no polling of our own required (a
     * looping sample keeps the bit set until STDL_StopSample) */
    if ((DMA_CTRL & 1) == 0) {
        sample_active = 0;
        if (stdl.dma_owner == STDL_DMA_SAMPLE) {
            stdl.dma_owner = STDL_DMA_FREE;
        }
    }
    return sample_active;
}

/* ---------------------------------------------------------------- */

void STDL_CloseAudio(void)
{
    if (!au.open) {
        return;
    }
    DMA_CTRL = 0;
    stdl_audio_hook = NULL;
    if (stdl.dma_owner == STDL_DMA_RING) {
        stdl.dma_owner = STDL_DMA_FREE;
    }
    free(au.userbuf);
    free(au.ring_alloc);
    memset(&au, 0, sizeof(au));
}

/* ---------------------------------------------------------------- */
/* WAV loading (uncompressed PCM only)                              */

STDL_AudioSpec *STDL_LoadWAV(const char *file, STDL_AudioSpec *spec,
                             uint8_t **audio_buf, uint32_t *audio_len)
{
    FILE *f;
    uint8_t head[12], chunk[8], fmt[16];
    int have_fmt = 0;
    uint8_t *data = NULL;
    uint32_t datalen = 0;

    if (spec == NULL || audio_buf == NULL || audio_len == NULL) {
        return NULL;
    }
    f = stdl_fopen_ci(file, "rb");
    if (f == NULL) {
        STDL_SetError("cannot open WAV file");
        return NULL;
    }
    if (fread(head, 1, 12, f) != 12 || memcmp(head, "RIFF", 4) != 0
        || memcmp(head + 8, "WAVE", 4) != 0) {
        STDL_SetError("not a WAV file");
        fclose(f);
        return NULL;
    }
    while (fread(chunk, 1, 8, f) == 8) {
        uint32_t sz = stdl_le32(chunk + 4);
        if (memcmp(chunk, "fmt ", 4) == 0 && sz >= 16) {
            if (fread(fmt, 1, 16, f) != 16) {
                break;
            }
            have_fmt = 1;
            if (fseek(f, (long)(sz - 16 + (sz & 1)), SEEK_CUR) != 0) {
                break;
            }
        } else if (memcmp(chunk, "data", 4) == 0) {
            data = malloc(sz);
            if (data == NULL || fread(data, 1, sz, f) != sz) {
                free(data);
                data = NULL;
                break;
            }
            datalen = sz;
            break;
        } else {
            if (fseek(f, (long)(sz + (sz & 1)), SEEK_CUR) != 0) {
                break;
            }
        }
    }
    fclose(f);

    if (!have_fmt || data == NULL) {
        free(data);
        STDL_SetError("truncated WAV file");
        return NULL;
    }
    if (stdl_le16(fmt) != 1) {
        free(data);
        STDL_SetError("compressed WAV; convert with stdlconv wav");
        return NULL;
    }
    memset(spec, 0, sizeof(*spec));
    spec->channels = (uint8_t)stdl_le16(fmt + 2);
    spec->freq = (int)stdl_le32(fmt + 4);
    switch (stdl_le16(fmt + 14)) {
        case 8:
            spec->format = STDL_AUDIO_U8;
            spec->silence = 0x80;
            break;
        case 16:
            spec->format = STDL_AUDIO_S16LSB;
            break;
        default:
            free(data);
            STDL_SetError("unsupported WAV bit depth");
            return NULL;
    }
    if (spec->channels < 1 || spec->channels > 2) {
        free(data);
        STDL_SetError("unsupported WAV channel count");
        return NULL;
    }
    spec->samples = 1024;
    *audio_buf = data;
    *audio_len = datalen;
    return spec;
}

void STDL_FreeWAV(uint8_t *audio_buf)
{
    free(audio_buf);
}
