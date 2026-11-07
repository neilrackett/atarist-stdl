/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Not a supported call, as STDL's stdl_bench.h is not: what a bench
 * reads to split a frame's time. It may change or go in any release.
 */

#ifndef STDL_SDL2_STDL_BENCH_H
#define STDL_SDL2_STDL_BENCH_H

#include "SDL_render.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * STDL_GetHz38400's counts (38,400 a second) since the last call with
 * reset set, summed over the presents that flipped: `convert` from the
 * start of SDL_RenderPresent to just before STDL_Flip (the frame's last
 * batch, the catch-up, the check and the colours), `flip` STDL_Flip's
 * own, its wait for the display among it. A batch converted before
 * the present, when a copy came from another texture, is not counted.
 */
extern void STDL_SDL2_GetPresentTimes(SDL_Renderer *renderer, Uint32 *convert,
                                      Uint32 *flip, int reset);

#ifdef __cplusplus
}
#endif

#endif /* STDL_SDL2_STDL_BENCH_H */
