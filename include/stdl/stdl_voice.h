/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_Voice: a fixed-function sample music device (STE/Mega STE).
 *
 * Four voices of signed 8-bit mono samples - Paula-style period/
 * volume/loop playback - mixed into a small looping DMA ring from
 * the 50Hz VBL. This is the missing middle between STDL_PlaySample
 * (free, but monophonic and unmixed) and the STDL_OpenAudio ring
 * device (a general mixing stream whose callback costs a large
 * slice of an 8MHz machine): the mixer is fixed-function - table-
 * driven volume, no format conversion, no user callback in the
 * audio path - so module-style music becomes affordable.
 *
 * A sequencer drives it from the voice tick: a callback run at 50Hz
 * in VBL context, before each mix, from which the STDL_SetVoice*
 * calls are safe. The tick contract is stdl_vbl.h's: no GEMDOS, no
 * allocation, no drawing - program voices and return.
 *
 * Sample data is read live by the mixer: stop a voice (or close the
 * device) before freeing or rewriting its buffer.
 */

#ifndef STDL_VOICE_H
#define STDL_VOICE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#define STDL_VOICES 4

/*
 * Open the device: `freq` snaps to the nearest DMA rate (6258 /
 * 12517 / 25033 / 50066 - music typically wants 6258 or 12517,
 * mixing cost scales with the rate). Claims the sound DMA: mutually
 * exclusive with STDL_OpenAudio and STDL_PlaySample*. Returns 0, or
 * -1 with STDL_GetError set (no STE DMA hardware, chip in use, out
 * of memory, no free VBL slot).
 */
int  STDL_OpenVoices(int freq);
void STDL_CloseVoices(void);
int  STDL_VoicesOpen(void);

/*
 * Start voice v (0..3) playing `data`: signed 8-bit mono, `len`
 * frames (at most 65535 - longer material belongs on a loop or on
 * STDL_PlaySampleLoop). After the first pass, playback loops the
 * region [loop_off, loop_off + loop_len); loop_len 0 = one-shot.
 * `freq` is the sample's playback rate in Hz (Paula ports:
 * 3546895 / period), resampled to the device rate. `vol` is 0..64;
 * at 64 a voice contributes a quarter of the output range, so four
 * voices at full volume sum without clipping.
 *
 * Callable from the voice tick and from the main line (guarded
 * against the VBL internally).
 */
void STDL_SetVoice(int v, const int8_t *data, uint32_t len,
                   uint32_t loop_off, uint32_t loop_len,
                   uint32_t freq, uint8_t vol);
void STDL_StopVoice(int v);
int  STDL_VoiceActive(int v);

/* Adjust a playing voice without retriggering it. */
void STDL_SetVoiceFreq(int v, uint32_t freq);
void STDL_SetVoiceVolume(int v, uint8_t vol);

/*
 * The sequencer hook: fn(userdata) runs at 50Hz in VBL context,
 * before the mix. NULL uninstalls. See the tick contract above.
 */
void STDL_SetVoiceTick(void (*fn)(void *), void *userdata);

#ifdef __cplusplus
}
#endif

#endif /* STDL_VOICE_H */
