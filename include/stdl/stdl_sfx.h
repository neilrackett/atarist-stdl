/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_Sfx: YM2149 sound effects, coexisting with STDL_Music.
 *
 * Two shapes, extracted from real ports:
 *  - the speaker: an immediate tone on/off, the PC-speaker idiom
 *    (Sopwith's engine drone) - always voice A
 *  - step effects: arrays of YM periods stepped at a fixed rate
 *    (FreeNukum's PC-speaker-sequence effects, AYFX-style)
 *
 * Effects run on the shared 50Hz VBL sound tick. While an effect
 * plays, it owns its voice: the music stream skips that voice's
 * registers and the voice is handed back (with the stream's state
 * restored) when the effect ends. All state changes are applied on
 * the next tick, so these calls are safe from normal game code.
 */

#ifndef STDL_SFX_H
#define STDL_SFX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdl/stdl_types.h>

/* YM tone period for a frequency in Hz (2MHz master / 16) */
#define STDL_YM_PERIOD(hz) ((uint16_t)(125000L / (hz)))

typedef struct STDL_Sfx {
    const uint16_t *periods;   /* one per step; 0 = silent step     */
    const uint8_t  *volumes;   /* optional per-step 0-15, else NULL */
    uint16_t nsteps;
    uint8_t  volume;           /* fixed volume when volumes == NULL */
    uint8_t  step_ms;          /* duration of each step (>= 1)      */
    uint8_t  noise;            /* 0 = tone; else noise period 1-31  */
} STDL_Sfx;

/*
 * Play an effect. voice 0-2 forces a channel (stealing it from
 * music or a running effect); -1 picks a free one, preferring C.
 * The STDL_Sfx and its arrays must stay valid while playing.
 * Returns the voice used, or -1.
 */
int  STDL_PlaySfx(const STDL_Sfx *sfx, int voice);
void STDL_StopSfx(int voice);          /* -1 stops all effects      */
int  STDL_SfxActive(int voice);        /* -1 counts active effects  */

/* Immediate tone on voice A: freq_hz 0 (or Off) silences. Stays on
 * until changed - the PC-speaker contract. Volume 0-15. */
void STDL_SpeakerOn(int freq_hz, uint8_t volume);
void STDL_SpeakerOff(void);

#ifdef __cplusplus
}
#endif

#endif /* STDL_SFX_H */
