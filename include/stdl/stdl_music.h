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

#ifdef __cplusplus
extern "C" {
#endif

#include <stdl/stdl_types.h>

typedef struct STDL_Music STDL_Music;

/* Load an STM stream (stdlconv midi / stdlconv stm output). */
STDL_Music *STDL_LoadMusic(const char *file);

/* The same from memory - an STM linked into the program (stdlconv's
 * C-array output, say), so a demo can be one file. The stream is
 * played where it lies, not copied: it must stay put until
 * STDL_FreeMusic, which does not free it. */
STDL_Music *STDL_MusicFromMemory(const void *stm, uint32_t bytes);

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

/*
 * Where the music is: the frames played since it started, which is
 * the index of the next frame to play, or -1 with no music. Frames
 * tick at the stream's own rate (50Hz from stdlconv midi), not the
 * display's, and the count goes back to the loop frame when the
 * stream loops. The tick advances it, so it is the music's clock to
 * sync to rather than a frame counter of the program's own.
 */
int  STDL_MusicFrame(void);

/* A stream's length in frames, its last frame plus one: the range
 * STDL_SeekMusic takes. -1 for NULL. */
int  STDL_MusicLength(const STDL_Music *music);

/*
 * Jump the current music, playing or paused, to `frame` (0 ..
 * nframes-1). The stream only records what changes,
 * so the player replays the changes up to `frame` into its state,
 * from a key the loader keeps every 256 frames, then writes that
 * state to the chip - every voice, not just the ones the next frame
 * changes. Straight after STDL_PlayMusic it starts a song part way
 * in. The envelope restarts if a voice uses it. Returns 0, or -1
 * with no music or a frame out of range.
 */
int  STDL_SeekMusic(int frame);

/*
 * The 14 YM registers as the stream last set them - tone periods
 * (0-5), noise (6), mixer (7, port bits clear), volumes (8-10, as
 * written, before STDL_VolumeMusic), envelope (11-13) - for VU
 * meters and the like, without reading the chip. They are the
 * stream's values even where an effect has taken the voice. Returns
 * 0, or -1 with the registers zeroed when there is no music.
 */
int  STDL_GetMusicRegisters(uint8_t regs[14]);

#ifdef __cplusplus
}
#endif

#endif /* STDL_MUSIC_H */
