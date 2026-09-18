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
 * An open device is not quite silent on real hardware. With the
 * DMA looping a buffer of pure silence and no software touching it
 * at all, a real STE clicks about once every thirty seconds -
 * measured with a probe that only opens the device and holds, so
 * there is nothing else it could be. It is the DMA unit or the
 * analogue path, no software reaches it, and closing the device is
 * the only thing that stops it.
 *
 * Stated here because a port hearing it will otherwise go looking
 * for a fault in its own code, which is what happened: the same
 * probe clicked every 5-10 seconds until v1.8.2, and that part was
 * ours - a torn read of the DMA position put the mixer's chase two
 * blocks early and let it write the block the hardware was
 * reading. Identical geometry either way, 512 frames at 6258Hz, so
 * the pair is a clean before and after: 5-10 seconds against the
 * hardware's 30.
 *
 * If a port cannot accept that baseline, the device has to be
 * closed when it is not wanted rather than left open and idle -
 * or paused, which is the cheap half of closing it.
 */

/*
 * Stop and restart the DMA without giving up the device. Everything
 * STDL_OpenVoices built stays: the ring, the 16640-entry volume
 * table (about a frame's work on an 8MHz machine, plus two mallocs)
 * and the voice state. A port whose device is idle for long
 * stretches - menus, cutscenes, a title screen - can have the
 * hardware's ~30-second click without paying to rebuild any of that
 * on the next effect.
 *
 * STDL_PauseVoices asks; it does not stop anything itself. The DMA
 * stops from the tick, once four consecutive quarters have mixed to
 * pure silence and the ring holds nothing but zeroes - up to 82ms at
 * 6258Hz. Stopping the DMA parks the DAC on the last byte it read,
 * so a stop taken mid-waveform would leave a DC step at the pause
 * and its mirror at the resume, which is a click bought from an API
 * that exists to avoid one. Draining first makes both transitions
 * land on zero.
 *
 * Two consequences of "asks":
 *
 *  - While a voice is still playing, the ring never drains and the
 *    device keeps running. It is not a mute. To pause a sequencer,
 *    stop its voices first (STDL_StopVoice on each) and the drain
 *    follows within a ring.
 *  - Pause then resume inside that window costs nothing at all: the
 *    DMA never stopped, so the resume is a flag write and the play
 *    head never moved. A port can pause the moment STDL_VoiceActive
 *    reports all four idle and resume from its next STDL_SetVoice
 *    without thinking about hysteresis.
 *
 * Resume restarts playback at the head of a cleared ring, so the
 * first audio is one quarter (about 20ms at 6258Hz, one frame) after
 * the call.
 *
 * Call it unconditionally - never behind `if (STDL_VoicesPaused())`.
 * Cancelling a pause that has not been taken yet is the greater
 * part of its job, and a port that skips the call during the drain
 * leaves the request standing: the device then stops at the next
 * four blocks of silence and stays stopped, with no error and no
 * sound. When the device is neither paused nor pausing it does
 * nothing, so the unconditional call is free.
 *
 * While the device is actually paused the sequencer tick does not
 * run either - a paused song must not advance in silence - and a
 * voice programmed with STDL_SetVoice stays silent until the resume.
 * That last one is the failure to watch for: a port that forgets its
 * resume gets no sound at all and no error.
 *
 * STDL_VoicesPaused reports the hardware, not the request: it stays
 * 0 through the drain and becomes 1 when the DMA has actually
 * stopped.
 *
 * Measured on a real STE, cycling pause and resume once a second
 * with a second of quiet either side, against the same device left
 * open and idle: the cycling clicked *less often*, not more. Had
 * the transitions been audible they would have added two clicks a
 * second and swamped the baseline; instead the baseline itself
 * thinned out, which is what happens if it only accrues while the
 * DMA is running. So stopping and starting is inaudible here, and
 * pausing removes the hardware click in proportion to how long you
 * stay paused. tests/hatari/voicesil.c is that test - green for
 * open and idle, cyan for cycling, blue for closed.
 */
void STDL_PauseVoices(void);
void STDL_ResumeVoices(void);
int  STDL_VoicesPaused(void);

/*
 * Start voice v (0..3) playing `data`: signed 8-bit mono, `len`
 * frames (at most 65535 - longer material belongs on a loop or on
 * STDL_PlaySampleLoop). After the first pass, playback loops the
 * region [loop_off, loop_off + loop_len); loop_len 0 = one-shot.
 * `freq` is the sample's playback rate in Hz (Paula ports:
 * 3546895 / period), resampled to the device rate. `vol` is 0..64;
 * at 64 a voice is the sample halved, so two voices at full volume
 * sum to the output range exactly and a third or fourth loud voice
 * clips rather than wraps - the mixer clamps. The reserve is half
 * what it was before v1.8.1, which made every port louder and
 * doubled what survives at low volumes: scaling an 8-bit sample
 * into an 8-bit table is lossy, and at the old quarter scale a
 * voice at vol 16 kept only the loudest sixteenth of the range.
 *
 * If your port balances these voices against anything else - YM
 * music especially - that balance is wrong after the change and
 * the fix is in your code, not here. Measured by a port doing
 * exactly that: its effects went from -26.0 to -20.3 dBFS peak on
 * identical content, so it moved its music volume from 60 to 70,
 * which took the music from -24.7 to -21.4. Six decibels on one
 * side of a mix nobody re-checked is the kind of regression that
 * gets described as "the sound is wrong now" long after the
 * upgrade that caused it.
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
