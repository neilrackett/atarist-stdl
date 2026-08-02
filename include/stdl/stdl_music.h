/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_Music: YM2149 register-stream replay.
 *
 * Music is a pre-rendered stream of YM register frames (the "STM"
 * format written by stdlconv from MIDI or other sources), replayed
 * at 50Hz from a VBL queue slot - the one interrupt-driven part of
 * STDL, because register updates cannot tolerate pump jitter. The
 * player never touches YM registers 14/15 (the I/O ports TOS uses
 * for floppy control) and preserves the port-direction bits when it
 * writes the mixer.
 *
 * Works on every ST - this is the plain-ST music path the design
 * reserved; DMA samples (STDL_Audio) are STE-only and mix freely
 * with it.
 */

#ifndef STDL_MUSIC_H
#define STDL_MUSIC_H

#include <stdl/stdl_types.h>

typedef struct STDL_Music STDL_Music;

/* Load an STM stream (stdlconv midi / stdlconv stm output). */
STDL_Music *STDL_LoadMusic(const char *file);
void        STDL_FreeMusic(STDL_Music *music);

/* loops: < 0 forever, otherwise play the stream `loops` times
 * (0 is treated as 1, like SDL_mixer in practice). */
int  STDL_PlayMusic(STDL_Music *music, int loops);
void STDL_HaltMusic(void);
void STDL_PauseMusic(void);
void STDL_ResumeMusic(void);
int  STDL_PausedMusic(void);
int  STDL_PlayingMusic(void);

/* 0..128; -1 queries. Scales the YM volume registers on the fly
 * (envelope-driven channels are left at full). */
int  STDL_VolumeMusic(int volume);

#endif /* STDL_MUSIC_H */
