/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Audio: one device on STDL's ring (ring.c in the core), with SDL2's
 * contract kept on top of it.
 *
 * - The ring asks for what a half of it needs, resampled to the DMA
 *   rate: about 2,048 frames, a few more or less each time. SDL2 gives
 *   a callback exactly the obtained spec's size, and programs count on
 *   it (one that makes spec.samples frames a call). So the program's
 *   callback fills a buffer of that size, and the ring is fed from it
 *   a stretch at a time: a copy of the stream, a few hundred bytes a
 *   frame, and the callback sees SDL2's sizes.
 * - The ring primes itself at open by calling its callback, and SDL2
 *   never calls one before the program unpauses, when its state may
 *   not be ready yet. Until the first unpause, and while locked, the
 *   ring gets silence.
 * - With no callback, SDL_QueueAudio's queue is the callback.
 *
 * Mixing (audiomix.c) and WAV files (wav.c) are objects of their own,
 * linked only by a program that calls them.
 */

#include "internal.h"

#define LEGACY_ID 1
#define DEVICE_ID 2

static const char device_name[] = "DMA sound";

static struct {
    SDL_AudioDeviceID id;       /* 0: closed */
    SDL_AudioSpec spec;         /* what the callback gets */
    Uint8 *buf;                 /* one callback's worth */
    Uint32 at, left;
    int started;                /* unpaused once */
    int locked;
    Uint8 *q;                   /* the queue, with no callback */
    Uint32 qhead, qlen, qcap;
} dev;

static void queue_feed(void *userdata, Uint8 *stream, int len)
{
    const Uint32 n = dev.qlen < (Uint32)len ? dev.qlen : (Uint32)len;
    (void)userdata;
    memcpy(stream, dev.q + dev.qhead, n);
    dev.qhead += n;
    dev.qlen -= n;
    if (!dev.qlen) {
        dev.qhead = 0;
    }
}

/* STDL's callback: the ring's stretch, from whole callbacks */
static void feed(void *userdata, Uint8 *stream, int len)
{
    (void)userdata;
    if (!dev.started || dev.locked) {
        memset(stream, dev.spec.silence, (size_t)len);
        return;
    }
    while (len > 0) {
        Uint32 n;
        if (!dev.left) {
            /* SDL2 leaves the buffer as it was; silence costs little
             * and keeps a callback that mixes onto it right */
            memset(dev.buf, dev.spec.silence, dev.spec.size);
            dev.spec.callback(dev.spec.userdata, dev.buf, (int)dev.spec.size);
            dev.at = 0;
            dev.left = dev.spec.size;
        }
        n = dev.left < (Uint32)len ? dev.left : (Uint32)len;
        memcpy(stream, dev.buf + dev.at, n);
        stream += n;
        len -= (int)n;
        dev.at += n;
        dev.left -= n;
    }
}

static int is_supported(SDL_AudioFormat f)
{
    return f == AUDIO_U8 || f == AUDIO_S8 || f == AUDIO_S16LSB ||
           f == AUDIO_S16MSB;
}

static void to_stdl(const SDL_AudioSpec *s, STDL_AudioSpec *t,
                    void (*callback)(void *, Uint8 *, int))
{
    memset(t, 0, sizeof(*t));
    t->freq = s->freq;
    t->format = s->format;
    t->channels = s->channels;
    t->samples = s->samples;
    t->callback = callback;
}

static SDL_AudioDeviceID open_device(const char *name, int iscapture,
                                     const SDL_AudioSpec *desired,
                                     SDL_AudioSpec *obtained, int allowed,
                                     SDL_AudioDeviceID id)
{
    SDL_AudioSpec s;
    STDL_AudioSpec t, hw;
    if (!desired) {
        SDL_InvalidParamError("desired");
        return 0;
    }
    if (iscapture) {
        SDL_SetError("No capture device");
        return 0;
    }
    if (name && strcmp(name, device_name) != 0) {
        SDL_SetError("No such device: %s", name);
        return 0;
    }
    if (dev.id) {
        SDL_SetError("Audio device already open");
        return 0;
    }
    /* SDL2's defaults, then what the ring takes */
    s = *desired;
    if (!s.freq) {
        s.freq = 22050;
    }
    if (!s.format) {
        s.format = AUDIO_S16LSB;
    }
    if (!s.channels) {
        s.channels = 2;
    }
    if (s.channels > 2) {
        if (!(allowed & SDL_AUDIO_ALLOW_CHANNELS_CHANGE)) {
            SDL_SetError("%d channels: mono or stereo here", s.channels);
            return 0;
        }
        s.channels = 2;
    }
    if (!is_supported(s.format)) {
        if (!(allowed & SDL_AUDIO_ALLOW_FORMAT_CHANGE)) {
            SDL_SetError("Audio format 0x%04X: 8 or 16 bits here", s.format);
            return 0;
        }
        s.format = AUDIO_S16SYS;
    }
    if (!s.samples) {
        /* SDL2's: a power of two over about 46ms */
        const int want = (s.freq / 1000) * 46;
        int p = 1;
        while (p < want && p < 0x8000) {
            p *= 2;
        }
        s.samples = (Uint16)p;
    }
    /* a rate the DMA has, if the program will take one: no resampling */
    if (allowed & SDL_AUDIO_ALLOW_FREQUENCY_CHANGE) {
        to_stdl(&s, &t, NULL);
        if (STDL_OpenAudio(&t, &hw) < 0) {
            SDL_SetError("%s", STDL_GetError());
            return 0;
        }
        STDL_CloseAudio();
        s.freq = hw.freq;
    }
    s.silence = (Uint8)(s.format == AUDIO_U8 ? 0x80 : 0);
    s.size = (Uint32)s.samples * s.channels * (SDL_AUDIO_BITSIZE(s.format) / 8);
    if (!s.callback) {
        s.callback = queue_feed;
        s.userdata = NULL;
    }
    dev.buf = (Uint8 *)malloc(s.size);
    if (!dev.buf) {
        SDL_OutOfMemory();
        return 0;
    }
    dev.spec = s;
    dev.at = dev.left = 0;
    dev.started = dev.locked = 0;
    to_stdl(&s, &t, feed);
    if (STDL_OpenAudio(&t, &hw) < 0) {
        SDL_SetError("%s", STDL_GetError());
        free(dev.buf);
        memset(&dev, 0, sizeof(dev));
        return 0;
    }
    dev.id = id;
    if (obtained) {
        *obtained = s;
        if (desired->callback == NULL) {
            obtained->callback = NULL;
        }
    }
    return id;
}

int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
    SDL_AudioSpec got;
    if (!open_device(NULL, 0, desired, obtained ? obtained : &got,
                     obtained ? SDL_AUDIO_ALLOW_ANY_CHANGE : 0, LEGACY_ID)) {
        return -1;
    }
    if (!obtained) {
        desired->size = got.size;
        desired->silence = got.silence;
    }
    return 0;
}

SDL_AudioDeviceID SDL_OpenAudioDevice(const char *device, int iscapture,
                                      const SDL_AudioSpec *desired,
                                      SDL_AudioSpec *obtained,
                                      int allowed_changes)
{
    SDL_AudioSpec got;
    return open_device(device, iscapture, desired, obtained ? obtained : &got,
                       allowed_changes, DEVICE_ID);
}

SDL_AudioStatus SDL_GetAudioDeviceStatus(SDL_AudioDeviceID id)
{
    if (!id || id != dev.id) {
        return SDL_AUDIO_STOPPED;
    }
    return STDL_GetAudioStatus() == STDL_AUDIO_PLAYING ? SDL_AUDIO_PLAYING
                                                       : SDL_AUDIO_PAUSED;
}

SDL_AudioStatus SDL_GetAudioStatus(void)
{
    return SDL_GetAudioDeviceStatus(LEGACY_ID);
}

void SDL_PauseAudioDevice(SDL_AudioDeviceID id, int pause_on)
{
    if (!id || id != dev.id) {
        return;
    }
    if (!pause_on) {
        dev.started = 1;
    }
    STDL_PauseAudio(pause_on);
}

void SDL_PauseAudio(int pause_on)
{
    SDL_PauseAudioDevice(LEGACY_ID, pause_on);
}

void SDL_LockAudioDevice(SDL_AudioDeviceID id)
{
    if (id && id == dev.id) {
        dev.locked++;
    }
}

void SDL_UnlockAudioDevice(SDL_AudioDeviceID id)
{
    if (id && id == dev.id && dev.locked > 0) {
        dev.locked--;
    }
}

void SDL_LockAudio(void)
{
    SDL_LockAudioDevice(LEGACY_ID);
}

void SDL_UnlockAudio(void)
{
    SDL_UnlockAudioDevice(LEGACY_ID);
}

void SDL_CloseAudioDevice(SDL_AudioDeviceID id)
{
    if (!id || id != dev.id) {
        return;
    }
    STDL_CloseAudio();
    free(dev.buf);
    free(dev.q);
    memset(&dev, 0, sizeof(dev));
}

void SDL_CloseAudio(void)
{
    SDL_CloseAudioDevice(LEGACY_ID);
}

int SDL_QueueAudio(SDL_AudioDeviceID id, const void *data, Uint32 len)
{
    if (!id || id != dev.id) {
        return SDL_SetError("Invalid audio device ID");
    }
    if (dev.spec.callback != queue_feed) {
        return SDL_SetError("Audio device has a callback, queueing not allowed");
    }
    if (dev.qhead + dev.qlen + len > dev.qcap) {
        if (dev.qhead) {
            memmove(dev.q, dev.q + dev.qhead, dev.qlen);
            dev.qhead = 0;
        }
        if (dev.qlen + len > dev.qcap) {
            Uint32 cap = dev.qcap ? dev.qcap * 2 : 4096;
            Uint8 *q;
            while (cap < dev.qlen + len) {
                cap *= 2;
            }
            q = (Uint8 *)realloc(dev.q, cap);
            if (!q) {
                return SDL_OutOfMemory();
            }
            dev.q = q;
            dev.qcap = cap;
        }
    }
    memcpy(dev.q + dev.qhead + dev.qlen, data, len);
    dev.qlen += len;
    return 0;
}

Uint32 SDL_GetQueuedAudioSize(SDL_AudioDeviceID id)
{
    return (id && id == dev.id && dev.spec.callback == queue_feed) ? dev.qlen
                                                                   : 0;
}

void SDL_ClearQueuedAudio(SDL_AudioDeviceID id)
{
    if (id && id == dev.id) {
        dev.qhead = dev.qlen = 0;
    }
}

/* ------------------------------------------------------------------ */
/* drivers and devices                                                 */

int SDL_GetNumAudioDrivers(void)
{
    return 1;
}

const char *SDL_GetAudioDriver(int index)
{
    return index == 0 ? "stdl" : NULL;
}

const char *SDL_GetCurrentAudioDriver(void)
{
    return "stdl";
}

int SDL_GetNumAudioDevices(int iscapture)
{
    return iscapture ? 0 : 1;
}

const char *SDL_GetAudioDeviceName(int index, int iscapture)
{
    return (index == 0 && !iscapture) ? device_name : NULL;
}

SDL_AudioFormat stdl_sdl2_audio_format(void)
{
    return dev.id ? dev.spec.format : 0;
}
