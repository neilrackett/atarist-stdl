/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_Tone: live notes on the YM's three tone voices (see
 * stdl_tone.h).
 *
 * The program writes slot intent (period, volume, a recency stamp)
 * with interrupts masked and sets a dirty flag; the tick, run by
 * the YM service between the music stream and the effects, returns
 * at once unless the flag is set and otherwise re-allocates: the
 * three newest keyed slots get the voices, keeping any voice whose
 * slot is still among them, and only registers whose shadow
 * differs are written.
 *
 * Voices are claimed through stdl_ym_owned exactly as effects claim
 * them, so the music stream leaves them alone and gets them back
 * restored. Effects sit above tones: when an effect takes a voice
 * the YM service tells the tone hook, which marks it stolen and
 * stops writing it; when the effect ends the hook re-writes the
 * note that is due on that voice and claims it again, or declines
 * so the music stream's own restore runs.
 *
 * A voice an effect holds is not an idle voice. A note keyed while
 * one is taken goes to a free voice and sounds at once rather than
 * waiting for the hand-back, and a note already parked on a stolen
 * voice moves to a free one if the wait would otherwise keep it
 * silent - with three voices and a game firing effects, the
 * alternative drops notes it need not drop. Only when there is no
 * free voice does a note wait on the one it is parked on. The
 * ownership mask is what says which voices are taken - the stolen
 * flag records only the steals this module was told about, and an
 * effect already sounding when the first STDL_ToneOn installs the
 * hook was announced to nobody.
 */

#include <string.h>
#include "stdl_internal.h"
#include <stdl/stdl_tone.h>

typedef struct {
    /* written by the program with interrupts masked */
    volatile uint16_t period;       /* 0 = not keyed             */
    volatile uint8_t  volume;
    volatile uint32_t stamp;        /* recency of the last key-on */
} slot_t;

typedef struct {
    /* owned by the tick */
    int8_t   slot;                  /* -1 = idle                 */
    uint8_t  held;                  /* claimed on the chip       */
    uint8_t  stolen;                /* an effect has it for now  */
    uint16_t period;                /* register shadow           */
    uint8_t  volume;
} voice_t;

static slot_t  slots[STDL_TONE_SLOTS];
static voice_t voices[3];
static volatile uint8_t dirty;
static uint32_t stamp_clock;

/* ---------------------------------------------------------------- */

/*
 * A voice this module must not drive. stdl_ym_owned is the truth:
 * the stolen flag only records a steal the hook was told about, and
 * an effect already sounding when the first STDL_ToneOn installs
 * the hook was never announced to anybody.
 */
static int voice_taken(int v)
{
    return voices[v].stolen
        || (((stdl_ym_owned >> v) & 1u) != 0 && !voices[v].held);
}

/* write a voice's note, claiming the voice if this tone module does
 * not hold it yet (registers only where the shadow differs) */
static void program_voice(int v)
{
    voice_t *vc = &voices[v];
    const slot_t *s = &slots[vc->slot];
    uint16_t period = s->period;
    uint8_t volume = s->volume > 15 ? 15 : s->volume;
    int fresh = !vc->held;

    if (voice_taken(v)) {
        return;                     /* re-written at hand-back   */
    }
    /* every time, not only when fresh: a voice handed round
     * between the services can come back with the bit clear while
     * this module is still driving it, and music.c reads the bit
     * to decide whether its stream may write the voice. */
    vc->held = 1;
    stdl_ym_owned |= (uint8_t)(1u << v);
    if (fresh || period != vc->period) {
        vc->period = period;
        stdl_ym_write(2 * v, period & 0xFF);
        stdl_ym_write(2 * v + 1, (period >> 8) & 0x0F);
    }
    if (fresh || volume != vc->volume) {
        vc->volume = volume;
        stdl_ym_write(8 + v, volume);
    }
    if (fresh) {
        stdl_ym_mix_update(STDL_YM_VOICE_BITS(v),
                           (uint8_t)(1u << (v + 3))); /* tone on  */
    }
}

/* the voice no longer has a slot: hand it back unless an effect is
 * using it, in which case the effect's own release will do so */
static void drop_voice(int v)
{
    voice_t *vc = &voices[v];

    vc->slot = -1;
    if (vc->held && !vc->stolen) {
        vc->held = 0;
        stdl_ym_release_voice(v);
    }
}

/* which voice is sounding a slot, or -1 */
static int voice_of(int slot)
{
    int v;

    for (v = 0; v < 3; v++) {
        if (voices[v].slot == slot) {
            return v;
        }
    }
    return -1;
}

/*
 * The allocation policy, all of it: the three newest keyed slots
 * sound. Changing the policy (say, keeping the lowest note) means
 * changing how `chosen` is filled and nothing else.
 */
static void allocate(void)
{
    int8_t chosen[3];
    uint32_t stamp[3];
    int n = 0;
    int s, v, i, j;

    for (s = 0; s < STDL_TONE_SLOTS; s++) {
        uint32_t st;

        if (slots[s].period == 0) {
            continue;
        }
        st = slots[s].stamp;
        for (i = 0; i < 3; i++) {
            if (i >= n || st > stamp[i]) {
                for (j = (n < 3 ? n : 2); j > i; j--) {
                    chosen[j] = chosen[j - 1];
                    stamp[j] = stamp[j - 1];
                }
                chosen[i] = (int8_t)s;
                stamp[i] = st;
                if (n < 3) {
                    n++;
                }
                break;
            }
        }
    }

    /* voices whose slot is no longer chosen go back */
    for (v = 0; v < 3; v++) {
        int keep = 0;

        if (voices[v].slot < 0) {
            continue;
        }
        for (i = 0; i < n; i++) {
            if (chosen[i] == voices[v].slot) {
                keep = 1;
                break;
            }
        }
        if (!keep) {
            drop_voice(v);
        }
    }
    /* chosen slots without a voice take the idle ones, oldest first
     * so notes keyed in sequence land on A, B, C in that order. A
     * voice an effect holds is not idle - but one holding a note of
     * this module's own is parked, keeps its note, and resumes it
     * at hand-back. */
    for (i = n - 1; i >= 0; i--) {
        int cur = voice_of(chosen[i]);

        if (cur >= 0 && !voice_taken(cur)) {
            continue;           /* already sounding            */
        }
        for (v = 0; v < 3; v++) {
            if (voices[v].slot < 0 && !voice_taken(v)) {
                if (cur >= 0) {
                    voices[cur].slot = -1;
                }
                voices[v].slot = chosen[i];
                break;
            }
        }
    }
    /*
     * Anything still without a voice, newest first: an effect has
     * left fewer voices than there are notes, so the newest notes
     * take them from the oldest. Without this the note that misses
     * out is whichever failed to find a voice rather than the
     * oldest, which is not the policy this module documents.
     */
    for (i = 0; i < n; i++) {
        int victim = -1;
        int cur = voice_of(chosen[i]);

        if (cur >= 0) {
            continue;           /* sounding, or parked on a
                                 * voice an effect holds       */
        }
        for (v = 0; v < 3; v++) {
            if (voice_taken(v) || voices[v].slot < 0) {
                continue;
            }
            if (slots[voices[v].slot].stamp >= stamp[i]) {
                continue;       /* newer than us: leave it      */
            }
            if (victim < 0
                || slots[voices[v].slot].stamp
                   < slots[voices[victim].slot].stamp) {
                victim = v;
            }
        }
        if (victim >= 0) {
            voices[victim].slot = chosen[i];
        }
    }
    /* write every voice that has a slot */
    for (v = 0; v < 3; v++) {
        if (voices[v].slot >= 0) {
            program_voice(v);
        }
    }
}

static void tone_tick(void)
{
    if (!dirty) {
        return;
    }
    dirty = 0;
    allocate();
}

/* the YM service's notice that an effect took a voice (lost) or is
 * handing it back (not lost); returns non-zero when a tone claimed
 * it back, so the music stream's restore is skipped */
static int voice_event(int v, int lost)
{
    voice_t *vc = &voices[v];

    if (lost) {
        vc->stolen = 1;
        vc->held = 0;
        return 0;
    }
    vc->stolen = 0;
    if (vc->slot < 0) {
        /* Nothing is due on this voice, so the music stream's
         * restore should run - but a note displaced by a newer one
         * may be waiting, and the tick only allocates when
         * something changed. Mark it, so the next tick offers this
         * voice to that note. */
        dirty = 1;
        return 0;
    }
    program_voice(v);               /* fresh: full re-write       */
    return 1;
}

/* ---------------------------------------------------------------- */

static int tone_install(void)
{
    int v;

    if (stdl_tone_tick == tone_tick) {
        return 0;
    }
    if (stdl_ym_install() < 0) {
        return -1;
    }
    memset(slots, 0, sizeof(slots));
    for (v = 0; v < 3; v++) {
        voices[v].slot = -1;
        voices[v].held = 0;
        voices[v].stolen = 0;
    }
    dirty = 0;
    stdl_ym_tone_hook = voice_event;
    stdl_tone_tick = tone_tick;
    return 0;
}

int STDL_ToneOpen(void)
{
    return tone_install();
}

int STDL_ToneOn(int slot, uint16_t period, uint8_t volume)
{
    uint16_t sr;

    if (slot < 0 || slot >= STDL_TONE_SLOTS) {
        STDL_SetError("bad tone slot");
        return -1;
    }
    if (period == 0) {
        STDL_ToneOff(slot);
        return 0;
    }
    if (tone_install() < 0) {
        return -1;
    }
    if (period > 0x0FFF) {
        period = 0x0FFF;
    }
    sr = stdl_int_off();
    slots[slot].stamp = ++stamp_clock;
    slots[slot].period = period;
    slots[slot].volume = volume;
    dirty = 1;
    stdl_int_restore(sr);
    return 0;
}

void STDL_ToneSet(int slot, uint16_t period, uint8_t volume)
{
    uint16_t sr;

    if (slot < 0 || slot >= STDL_TONE_SLOTS
        || stdl_tone_tick != tone_tick) {
        return;
    }
    if (period == 0) {
        STDL_ToneOff(slot);
        return;
    }
    if (period > 0x0FFF) {
        period = 0x0FFF;
    }
    sr = stdl_int_off();
    if (slots[slot].period != 0) {
        slots[slot].period = period;
        slots[slot].volume = volume;
        dirty = 1;
    }
    stdl_int_restore(sr);
}

void STDL_ToneOff(int slot)
{
    uint16_t sr;
    int s;

    if (slot < -1 || slot >= STDL_TONE_SLOTS
        || stdl_tone_tick != tone_tick) {
        return;
    }
    sr = stdl_int_off();
    for (s = 0; s < STDL_TONE_SLOTS; s++) {
        if ((slot < 0 || slot == s) && slots[s].period != 0) {
            slots[s].period = 0;
            dirty = 1;
        }
    }
    stdl_int_restore(sr);
}

int STDL_ToneActive(int slot)
{
    int s, n = 0;

    if (stdl_tone_tick != tone_tick) {
        return 0;
    }
    if (slot >= STDL_TONE_SLOTS) {
        return 0;
    }
    if (slot >= 0) {
        return slots[slot].period != 0;
    }
    for (s = 0; s < STDL_TONE_SLOTS; s++) {
        n += slots[s].period != 0 ? 1 : 0;
    }
    return n;
}
