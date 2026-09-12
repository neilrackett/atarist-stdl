/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_Opl: an OPL2 register stream as notes on the YM2149.
 *
 * The AdLib-era games (Commander Keen, Wolfenstein 3D, Bio Menace,
 * Duke Nukem II and the rest of the IMF family) keep their music as
 * the raw register writes their sound driver sent to the OPL2, and
 * their effects the same way. A port replays that data at its own
 * rate and hands each write to STDL_OplWrite, which keeps the nine
 * OPL channels' state and turns them into notes on the STDL_Tone
 * device: a key-on keys the channel's tone slot on at the OPL pitch,
 * a frequency change while keyed bends it, the carrier's total level
 * sets its volume, and a key-off keys it off. The result is a
 * three-voice square-wave cover, not the FM sound.
 *
 * Channels 0-8 use tone slots 0-8; slots 9-15 stay free for the
 * program (a PC-speaker tone, say). Timbre registers, rhythm mode
 * and the noise generator are ignored.
 *
 * STDL_OplWrite is safe from the program's own timer or VBL
 * service, as the tone calls are - provided STDL_OplReset has been
 * called once from the main line first. That is what installs the
 * shared sound service, which claims a VBL slot and touches TOS's
 * console byte, and neither belongs inside an interrupt.
 */

#ifndef STDL_OPL_H
#define STDL_OPL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdl/stdl_types.h>

#define STDL_OPL_CHANNELS 9

/* Apply one OPL2 register write (register 0x00-0xFF, value). */
void STDL_OplWrite(uint8_t reg, uint8_t val);

/* Key every channel off and forget the register state (the OPL reset
 * a sound driver does at start-up and shutdown). */
void STDL_OplReset(void);

#ifdef __cplusplus
}
#endif

#endif /* STDL_OPL_H */
