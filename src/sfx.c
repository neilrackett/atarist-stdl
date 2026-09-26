/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_Sfx: YM2149 effects on the shared VBL sound tick.
 *
 * The game thread only writes intent (pending fields); the tick -
 * which runs after the music stream each VBL - applies it to the
 * chip. Voice ownership goes through stdl_ym_owned so the music
 * stream leaves effect voices alone, and stdl_ym_release_voice
 * restores the stream's registers when an effect ends.
 */

#include <string.h>
#include "stdl_internal.h"
#include <stdl/stdl_sfx.h>

typedef struct {
    /* written by the game thread */
    const STDL_Sfx *volatile pending;   /* start this effect        */
    volatile uint8_t stop;              /* stop request             */

    /* owned by the tick */
    const STDL_Sfx *sfx;
    uint16_t step;
    uint16_t acc;                       /* ms x the VBL rate        */
    uint8_t  running;
    uint8_t  noisy;                     /* this step uses the noise */
} sfx_voice_t;

static sfx_voice_t voices[3];

/* speaker intent (voice A) */
static volatile uint16_t spk_period;    /* 0 = off                  */
static volatile uint8_t  spk_volume;
static volatile uint8_t  spk_dirty;
static uint8_t spk_running;

/* ---------------------------------------------------------------- */

static void voice_off(int v)
{
    sfx_voice_t *sv = &voices[v];

    sv->running = 0;
    sv->noisy = 0;
    sv->sfx = NULL;
    if (stdl_ym_owned & (1u << v)) {
        stdl_ym_release_voice(v);
    }
}

/*
 * Two ways to read a step. Without `noises`, as ever: a step is the
 * tone at its period, or with `noise` set the noise instead, and a
 * period of 0 is a rest. With `noises`, tone and noise are separate
 * per step - the period sounds the tone, the noise entry the noise
 * generator at that period, both at once when both are set, and a
 * rest only when neither is. That is what a gunshot wants (a crack
 * of noise over a thump of tone, on one voice) and an explosion (a
 * noise that sinks as it fades).
 */
static void apply_step(int v)
{
    sfx_voice_t *sv = &voices[v];
    const STDL_Sfx *fx = sv->sfx;
    uint16_t period = fx->periods[sv->step];
    uint8_t vol = fx->volumes != NULL ? fx->volumes[sv->step]
                                      : fx->volume;
    uint8_t noise, off;

    if (fx->noises != NULL) {
        noise = fx->noises[sv->step] & 0x1F;
    } else {
        noise = (period != 0) ? (uint8_t)(fx->noise & 0x1F) : 0;
        if (noise != 0) {
            period = 0;                 /* noise effects are noise only */
        }
    }
    sv->noisy = (noise != 0);
    if (period == 0 && noise == 0) {
        stdl_ym_write(8 + v, 0);
        return;
    }
    off = 0;                            /* r7 bits: 1 = off         */
    if (period != 0) {
        stdl_ym_write(2 * v, period & 0xFF);
        stdl_ym_write(2 * v + 1, (period >> 8) & 0x0F);
    } else {
        off |= (uint8_t)(1u << v);
    }
    if (noise != 0) {
        stdl_ym_owned |= 0x08;
        stdl_ym_write(6, noise);
    } else {
        off |= (uint8_t)(1u << (v + 3));
    }
    stdl_ym_mix_update(STDL_YM_VOICE_BITS(v), off);
    stdl_ym_write(8 + v, vol > 15 ? 15 : vol);
}

static void sfx_tick(void)
{
    int v;
    int noise_used = 0;

    /* speaker: voice A immediate tone */
    if (spk_dirty) {
        spk_dirty = 0;
        if (spk_period != 0) {
            if (voices[0].running) {
                voice_off(0);       /* speaker evicts an effect     */
            }
            stdl_ym_claim_voice(0);
            stdl_ym_write(0, spk_period & 0xFF);
            stdl_ym_write(1, (spk_period >> 8) & 0x0F);
            stdl_ym_write(8, spk_volume > 15 ? 15 : spk_volume);
            stdl_ym_mix_update(STDL_YM_VOICE_BITS(0), 0x08); /* tone on, noise off */
            spk_running = 1;
        } else if (spk_running) {
            spk_running = 0;
            stdl_ym_release_voice(0);
        }
    }

    for (v = 0; v < 3; v++) {
        sfx_voice_t *sv = &voices[v];

        if (sv->stop) {
            sv->stop = 0;
            if (sv->running) {
                voice_off(v);
            }
        }
        if (sv->pending != NULL) {
            if (v == 0 && spk_running) {
                sv->pending = NULL;   /* speaker keeps voice A      */
                continue;
            }
            sv->sfx = sv->pending;
            sv->pending = NULL;
            sv->step = 0;
            sv->acc = 0;
            sv->running = 1;
            stdl_ym_claim_voice(v);
            apply_step(v);
        } else if (sv->running) {
            /* A tick is 1000/rate ms: count in thousandths of a tick
             * so 60Hz is exact (16.7ms) instead of taken as 20 -
             * which ran every effect 20% fast after STDL_SetRefresh(60) */
            const uint16_t step_len =
                (uint16_t)(sv->sfx->step_ms * stdl_vbl_hz);
            sv->acc = (uint16_t)(sv->acc + 1000);
            while (sv->running && sv->acc >= step_len) {
                sv->acc = (uint16_t)(sv->acc - step_len);
                sv->step++;
                if (sv->step >= sv->sfx->nsteps) {
                    voice_off(v);
                } else {
                    apply_step(v);
                }
            }
        }
        if (sv->running && sv->noisy) {
            noise_used = 1;
        }
    }
    if (!noise_used && (stdl_ym_owned & 0x08)) {
        stdl_ym_owned &= (uint8_t)~0x08;
    }
}

/* ---------------------------------------------------------------- */

static int sfx_install(void)
{
    if (stdl_sfx_tick == sfx_tick) {
        return 0;
    }
    if (stdl_ym_install() < 0) {
        return -1;
    }
    stdl_sfx_tick = sfx_tick;
    return 0;
}

int STDL_PlaySfx(const STDL_Sfx *sfx, int voice)
{
    int v;

    if (sfx == NULL || sfx->periods == NULL || sfx->nsteps == 0
        || sfx->step_ms == 0) {
        STDL_SetError("bad effect");
        return -1;
    }
    if (sfx_install() < 0) {
        return -1;
    }
    if (voice < 0) {
        /* prefer C: the converter parks drums there, and it is the
         * voice a melody misses least */
        for (v = 2; v >= 0; v--) {
            if (!voices[v].running && voices[v].pending == NULL
                && !(v == 0 && spk_period != 0)) {
                voice = v;
                break;
            }
        }
        if (voice < 0) {
            voice = 2;          /* all busy: steal C */
        }
    }
    if (voice > 2 || (voice == 0 && spk_period != 0)) {
        STDL_SetError("no free effect voice");
        return -1;
    }
    voices[voice].pending = sfx;
    return voice;
}

void STDL_StopSfx(int voice)
{
    int v;

    for (v = 0; v < 3; v++) {
        if (voice < 0 || voice == v) {
            voices[v].pending = NULL;
            voices[v].stop = 1;
        }
    }
}

int STDL_SfxActive(int voice)
{
    int v, n = 0;

    if (voice >= 0 && voice <= 2) {
        return voices[voice].running || voices[voice].pending != NULL;
    }
    for (v = 0; v < 3; v++) {
        n += (voices[v].running || voices[v].pending != NULL) ? 1 : 0;
    }
    return n;
}

void STDL_SpeakerOn(int freq_hz, uint8_t volume)
{
    if (freq_hz <= 0) {
        STDL_SpeakerOff();
        return;
    }
    if (sfx_install() < 0) {
        return;
    }
    {
        long p = STDL_YM_CLOCK / freq_hz;
        if (p < 1) p = 1;
        if (p > 0x0FFF) p = 0x0FFF;
        spk_period = (uint16_t)p;
    }
    spk_volume = volume;
    spk_dirty = 1;
}

void STDL_SpeakerOff(void)
{
    spk_period = 0;
    spk_dirty = 1;
}
