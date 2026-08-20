/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The one interrupt STDL hands out: a callback on the 50Hz vertical
 * blank.
 */

#ifndef STDL_VBL_H
#define STDL_VBL_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Install `fn` in a free TOS VBL queue slot, so it is called once per
 * vertical blank - 50Hz in the colour low resolution STDL runs in.
 * Returns 0, or -1 with STDL_GetError() set when every slot is taken
 * (TOS provides eight; STDL's own sound tick uses one of them when
 * the program plays music or effects). Installing the same function
 * twice is a no-op that still returns 0. STDL_Init is performed if
 * the program has not called it yet.
 *
 * This is the *only* place a program gets an interrupt, and it exists
 * because the cooperative services cannot do this job: audio refill,
 * compat timers and the cursor all run from STDL_PumpEvents and the
 * delays, so their timing follows the frame. Anything that has to be
 * rock steady regardless of how long drawing takes - stepping a music
 * or sound-effect sequencer, a frame counter, a raster split - needs
 * the VBL instead. Everything else should stay on the pump.
 *
 * The contract for `fn`, which is not negotiable:
 *
 *  - It runs in supervisor mode at interrupt level 4, on the system
 *    stack, from TOS's VBL dispatcher. Keep it short: it delays the
 *    display, and it is charged to every frame including the ones the
 *    program spends in a loader.
 *  - No GEMDOS, BIOS or XBIOS calls, and no C library that makes them
 *    (printf, malloc, free, fopen). They are not re-entrant and the
 *    main program may be inside one.
 *  - No STDL call that allocates, draws or pumps. Reading
 *    STDL_GetTicks is safe; so is touching the program's own state,
 *    provided the main loop treats that state as volatile and does
 *    not assume its updates are atomic (a long is two writes on a
 *    68000 - mask interrupts around anything wider than a word that
 *    both sides change).
 *  - It will not be re-entered: TOS's dispatcher holds a semaphore
 *    and skips the whole queue if a VBL is still running. A callback
 *    that overruns a frame therefore loses ticks rather than nesting.
 *  - It must not touch the YM2149 while STDL_Music, STDL_Sfx or
 *    STDL_Speaker are in use. Those own the chip through STDL's own
 *    VBL tick, which runs from a different slot in an order TOS does
 *    not define, and register writes from both sides would interleave
 *    unpredictably. Sound effects belong in STDL_PlaySfx.
 *
 * Callbacks are removed automatically when STDL shuts down, including
 * on abnormal termination, so a program that installs one and dies in
 * an assert does not leave a queue entry pointing into freed memory.
 */
int  STDL_AddVBL(void (*fn)(void));

/* Remove a callback installed by STDL_AddVBL. Safe to call with a
 * function that is not installed, and safe from inside a callback. */
void STDL_RemoveVBL(void (*fn)(void));

#ifdef __cplusplus
}
#endif

#endif /* STDL_VBL_H */
