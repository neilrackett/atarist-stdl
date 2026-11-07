/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The SDL2 add-on's audio: one output device, STDL's ring on the DMA
 * sound (stdl_audio.h), with SDL2's contract on top - the callback
 * gets exactly the obtained spec's size each time, in the format and
 * at the rate it asked for, and SDL_QueueAudio feeds it instead when
 * there is no callback. STDL resamples to the nearest DMA rate (6258,
 * 12517, 25033 or 50066Hz; a Falcon has no 6258) and converts to the
 * hardware's signed 8 bits.
 *
 * The callback runs where STDL refills its ring: inside SDL_PollEvent,
 * SDL_PumpEvents, the waits and SDL_Delay, on the program's own stack.
 * So there is no audio thread, a program pumps at least every 150ms
 * while audio plays, and SDL_LockAudio keeps the callback from running
 * (a refill while locked plays silence).
 *
 * Formats: AUDIO_U8, AUDIO_S8, AUDIO_S16LSB and AUDIO_S16MSB. A
 * program that asks for another (32-bit, float, unsigned 16) without
 * SDL_AUDIO_ALLOW_FORMAT_CHANGE is refused, as converting each sample
 * from float on a 68030 with no FPU costs more than the sound is
 * worth; with it, it gets AUDIO_S16SYS. Mono or stereo. SDL2's
 * converters (SDL_AudioCVT, SDL_AudioStream) are not here.
 */

#ifndef STDL_SDL2_AUDIO_H
#define STDL_SDL2_AUDIO_H

#include "SDL_stdinc.h"
#include "SDL_rwops.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef Uint16 SDL_AudioFormat;

#define SDL_AUDIO_MASK_BITSIZE   0xFF
#define SDL_AUDIO_MASK_DATATYPE  (1 << 8)
#define SDL_AUDIO_MASK_ENDIAN    (1 << 12)
#define SDL_AUDIO_MASK_SIGNED    (1 << 15)
#define SDL_AUDIO_BITSIZE(x)      ((x) & SDL_AUDIO_MASK_BITSIZE)
#define SDL_AUDIO_ISFLOAT(x)      ((x) & SDL_AUDIO_MASK_DATATYPE)
#define SDL_AUDIO_ISBIGENDIAN(x)  ((x) & SDL_AUDIO_MASK_ENDIAN)
#define SDL_AUDIO_ISSIGNED(x)     ((x) & SDL_AUDIO_MASK_SIGNED)
#define SDL_AUDIO_ISINT(x)        (!SDL_AUDIO_ISFLOAT(x))
#define SDL_AUDIO_ISLITTLEENDIAN(x) (!SDL_AUDIO_ISBIGENDIAN(x))
#define SDL_AUDIO_ISUNSIGNED(x)   (!SDL_AUDIO_ISSIGNED(x))

#define AUDIO_U8     0x0008
#define AUDIO_S8     0x8008
#define AUDIO_U16LSB 0x0010
#define AUDIO_S16LSB 0x8010
#define AUDIO_U16MSB 0x1010
#define AUDIO_S16MSB 0x9010
#define AUDIO_U16    AUDIO_U16LSB
#define AUDIO_S16    AUDIO_S16LSB
#define AUDIO_S32LSB 0x8020
#define AUDIO_S32MSB 0x9020
#define AUDIO_S32    AUDIO_S32LSB
#define AUDIO_F32LSB 0x8120
#define AUDIO_F32MSB 0x9120
#define AUDIO_F32    AUDIO_F32LSB
/* the 68000 is big-endian */
#define AUDIO_U16SYS AUDIO_U16MSB
#define AUDIO_S16SYS AUDIO_S16MSB
#define AUDIO_S32SYS AUDIO_S32MSB
#define AUDIO_F32SYS AUDIO_F32MSB

#define SDL_AUDIO_ALLOW_FREQUENCY_CHANGE 0x00000001
#define SDL_AUDIO_ALLOW_FORMAT_CHANGE    0x00000002
#define SDL_AUDIO_ALLOW_CHANNELS_CHANGE  0x00000004
#define SDL_AUDIO_ALLOW_SAMPLES_CHANGE   0x00000008
#define SDL_AUDIO_ALLOW_ANY_CHANGE \
    (SDL_AUDIO_ALLOW_FREQUENCY_CHANGE | SDL_AUDIO_ALLOW_FORMAT_CHANGE | \
     SDL_AUDIO_ALLOW_CHANNELS_CHANGE | SDL_AUDIO_ALLOW_SAMPLES_CHANGE)

typedef void (*SDL_AudioCallback)(void *userdata, Uint8 *stream, int len);

typedef struct SDL_AudioSpec {
    int freq;
    SDL_AudioFormat format;
    Uint8 channels;
    Uint8 silence;
    Uint16 samples;             /* frames a callback; 0 picks ~46ms */
    Uint16 padding;
    Uint32 size;                /* bytes a callback */
    SDL_AudioCallback callback; /* NULL: SDL_QueueAudio feeds it */
    void *userdata;
} SDL_AudioSpec;

typedef Uint32 SDL_AudioDeviceID;

typedef enum {
    SDL_AUDIO_STOPPED = 0,
    SDL_AUDIO_PLAYING,
    SDL_AUDIO_PAUSED
} SDL_AudioStatus;

#define SDL_MIX_MAXVOLUME 128

#define SDL_GetNumAudioDrivers   stdl_sdl2_GetNumAudioDrivers
#define SDL_GetAudioDriver       stdl_sdl2_GetAudioDriver
#define SDL_GetCurrentAudioDriver stdl_sdl2_GetCurrentAudioDriver
#define SDL_GetNumAudioDevices   stdl_sdl2_GetNumAudioDevices
#define SDL_GetAudioDeviceName   stdl_sdl2_GetAudioDeviceName
#define SDL_OpenAudio            stdl_sdl2_OpenAudio
#define SDL_OpenAudioDevice      stdl_sdl2_OpenAudioDevice
#define SDL_GetAudioStatus       stdl_sdl2_GetAudioStatus
#define SDL_GetAudioDeviceStatus stdl_sdl2_GetAudioDeviceStatus
#define SDL_PauseAudio           stdl_sdl2_PauseAudio
#define SDL_PauseAudioDevice     stdl_sdl2_PauseAudioDevice
#define SDL_LockAudio            stdl_sdl2_LockAudio
#define SDL_LockAudioDevice      stdl_sdl2_LockAudioDevice
#define SDL_UnlockAudio          stdl_sdl2_UnlockAudio
#define SDL_UnlockAudioDevice    stdl_sdl2_UnlockAudioDevice
#define SDL_CloseAudio           stdl_sdl2_CloseAudio
#define SDL_CloseAudioDevice     stdl_sdl2_CloseAudioDevice
#define SDL_QueueAudio           stdl_sdl2_QueueAudio
#define SDL_GetQueuedAudioSize   stdl_sdl2_GetQueuedAudioSize
#define SDL_ClearQueuedAudio     stdl_sdl2_ClearQueuedAudio
#define SDL_MixAudio             stdl_sdl2_MixAudio
#define SDL_MixAudioFormat       stdl_sdl2_MixAudioFormat
#define SDL_LoadWAV_RW           stdl_sdl2_LoadWAV_RW
#define SDL_FreeWAV              stdl_sdl2_FreeWAV

/* "stdl", the one driver */
extern int SDL_GetNumAudioDrivers(void);
extern const char *SDL_GetAudioDriver(int index);
extern const char *SDL_GetCurrentAudioDriver(void);
/* one output device, "DMA sound"; no capture */
extern int SDL_GetNumAudioDevices(int iscapture);
extern const char *SDL_GetAudioDeviceName(int index, int iscapture);

/* device 1, every change allowed if obtained is given; otherwise
 * desired's size and silence are filled in */
extern int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained);
/* device 2; one device open at a time. Opened paused, as in SDL2. */
extern SDL_AudioDeviceID SDL_OpenAudioDevice(const char *device,
                                             int iscapture,
                                             const SDL_AudioSpec *desired,
                                             SDL_AudioSpec *obtained,
                                             int allowed_changes);
extern SDL_AudioStatus SDL_GetAudioStatus(void);
extern SDL_AudioStatus SDL_GetAudioDeviceStatus(SDL_AudioDeviceID dev);
extern void SDL_PauseAudio(int pause_on);
extern void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on);
extern void SDL_LockAudio(void);
extern void SDL_LockAudioDevice(SDL_AudioDeviceID dev);
extern void SDL_UnlockAudio(void);
extern void SDL_UnlockAudioDevice(SDL_AudioDeviceID dev);
extern void SDL_CloseAudio(void);
extern void SDL_CloseAudioDevice(SDL_AudioDeviceID dev);

/* a device opened with no callback plays what is queued, then silence */
extern int SDL_QueueAudio(SDL_AudioDeviceID dev, const void *data,
                          Uint32 len);
extern Uint32 SDL_GetQueuedAudioSize(SDL_AudioDeviceID dev);
extern void SDL_ClearQueuedAudio(SDL_AudioDeviceID dev);

/* src added to dst at volume (0-128), clamped; SDL_MixAudio in the
 * open device's format */
extern void SDL_MixAudio(Uint8 *dst, const Uint8 *src, Uint32 len,
                         int volume);
extern void SDL_MixAudioFormat(Uint8 *dst, const Uint8 *src,
                               SDL_AudioFormat format, Uint32 len,
                               int volume);

/* an uncompressed PCM WAV, 8 or 16 bits, mono or stereo; the buffer
 * is freed with SDL_FreeWAV */
extern SDL_AudioSpec *SDL_LoadWAV_RW(SDL_RWops *src, int freesrc,
                                     SDL_AudioSpec *spec, Uint8 **audio_buf,
                                     Uint32 *audio_len);
extern void SDL_FreeWAV(Uint8 *audio_buf);

#define SDL_LoadWAV(file, spec, audio_buf, audio_len) \
    SDL_LoadWAV_RW(SDL_RWFromFile(file, "rb"), 1, spec, audio_buf, audio_len)

#ifdef __cplusplus
}
#endif

#endif /* STDL_SDL2_AUDIO_H */
