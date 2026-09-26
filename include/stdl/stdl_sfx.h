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
 * Effects run on the shared VBL sound tick. While an effect plays,
 * it owns its voice: the music stream skips that voice's registers
 * and the voice is handed back (with the stream's state restored)
 * when the effect ends. All state changes are applied on the next
 * tick, so these calls are safe from normal game code.
 *
 * Steps last step_ms whatever the display rate: at 60Hz a tick is
 * 16.7ms and the effect still takes as long as at 50Hz (from v1.12.0;
 * before, every tick counted as 20ms and effects ran 20% fast). A
 * step still lands on a tick, so steps shorter than a tick blur
 * together - 20ms or more keeps each one distinct.
 */

#ifndef STDL_SFX_H
#define STDL_SFX_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdl/stdl_ym.h>

typedef struct STDL_Sfx {
    const uint16_t *periods;   /* one per step; 0 = silent step     */
    const uint8_t  *volumes;   /* optional per-step 0-15, else NULL */
    uint16_t nsteps;
    uint8_t  volume;           /* fixed volume when volumes == NULL */
    uint8_t  step_ms;          /* duration of each step (>= 1)      */
    uint8_t  noise;            /* 0 = tone; else noise period 1-31  */
    const uint8_t  *noises;    /* optional per-step noise, see below */
} STDL_Sfx;

/*
 * Without `noises` (NULL), an effect is tone or noise throughout, as
 * before v1.12.0: the step's period sounds the tone, or with `noise`
 * set the noise generator at that fixed period, and period 0 rests.
 * An initialiser written for the older struct, stopping at `noise`,
 * still means exactly that - though gcc's -Wextra now asks for the
 * missing field, so add a NULL.
 *
 * With `noises`, one entry per step, tone and noise are separate
 * each step on the same voice: periods[i] sounds the tone (0 = no
 * tone), noises[i] the noise at that period (1-31, low is bright;
 * 0 = no noise), both together when both are set, a rest when
 * neither is. `noise` is ignored. A gunshot is a noise crack over a
 * tone thump; an explosion a noise whose period rises as it fades.
 * The noise generator is one for the whole chip: an effect using it
 * takes it from the music while its steps do, and hands it back.
 */

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
