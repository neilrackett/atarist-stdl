/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_Tone: live notes on the YM2149's three tone voices.
 *
 * For a port whose music arrives as notes at run time - an OPL
 * register stream (the IMF games), a MIDI-note stream (MUS, XMI),
 * a tracker of its own - rather than as an STM file it can hand to
 * STDL_Music. The program keys notes on and off in up to sixteen
 * slots, one per logical channel, and the device plays the three
 * most recently keyed on the chip: last-note priority, the policy
 * stdlconv's MIDI converter uses, so a live cover sounds like a
 * converted one. A note that lost its voice to a newer one comes
 * back when a voice frees, whether that is a note ending or an
 * effect handing its voice back, so held chords survive passing
 * notes. A note keyed while an effect holds a voice takes a free
 * voice and sounds at once; it does not wait for that voice.
 *
 * The slot calls only record intent; the shared 50Hz sound tick
 * applies it, between the music stream and the effects: tones
 * override STDL_Music on the voices they use and hand them back
 * restored, and STDL_Sfx / STDL_Speaker override tones the same
 * way. Nothing runs when nothing changed, and a held chord costs
 * no register writes per frame. The chip is silenced on exit,
 * including an abnormal one.
 *
 * Periods are YM tone periods (STDL_YM_PERIOD converts from Hz);
 * the tick does no division. Volume is 0-15.
 */

#ifndef STDL_TONE_H
#define STDL_TONE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdl/stdl_ym.h>

#define STDL_TONE_SLOTS 16

/*
 * Key slot 0-15 on with a tone period (1-4095) and volume (0-15).
 * Keying an already-sounding slot restrikes it: it becomes the
 * newest note. Returns 0, or -1 with STDL_GetError set (bad slot,
 * or no free VBL queue slot for sound).
 */
/*
 * Install the shared sound service without sounding anything, from
 * the main line. Keying the first note installs it anyway, so this
 * is for two cases: a program that wants to know now whether the
 * service is available (returns 0, or -1 with STDL_GetError set),
 * and one that feeds notes from its own timer or VBL service,
 * where the install must not happen inside the interrupt - it
 * claims a VBL slot and touches TOS's console byte, which a
 * main-line STDL_PlaySfx can be doing at the same moment.
 */
int  STDL_ToneOpen(void);

int  STDL_ToneOn(int slot, uint16_t period, uint8_t volume);

/*
 * Change a keyed slot's period and volume in place - a pitch bend
 * or a volume envelope - without making it the newest note. A
 * slot that is not keyed is left alone; period 0 keys it off.
 */
void STDL_ToneSet(int slot, uint16_t period, uint8_t volume);

/* Key a slot off; -1 keys every slot off. */
void STDL_ToneOff(int slot);

/* Non-zero while a slot is keyed; -1 counts the keyed slots. */
int  STDL_ToneActive(int slot);

#ifdef __cplusplus
}
#endif

#endif /* STDL_TONE_H */
