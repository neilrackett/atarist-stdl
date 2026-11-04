/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * For benches, not games: what a responsive screen's Flip did. Not
 * one of STDL's supported calls, so stdl.h leaves it out; it may
 * change or go in any release. Include it by name.
 *
 * The present is a TT's copy of what was drawn to the display's
 * doubled lines (stdl_responsive.h). A Falcon has none; on an ST's own
 * screen nothing here moves and `shown` stays NULL. Times are in
 * STDL_GetHz38400 counts. Flip still waits for the VBL, so a bench
 * timing a frame takes the wait out itself: the time in Flip, less
 * `present_time`.
 */

#ifndef STDL_BENCH_H
#define STDL_BENCH_H

#include <stdl/stdl_types.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint32_t flips;
    uint32_t present_time;  /* STDL_GetHz38400 counts presenting      */
    uint32_t groups;        /* 16-pixel groups copied to the display,
                             * a row's each                          */
    const uint8_t *shown;   /* the screen's rows on the page the last
                             * Flip showed (not the margins, where
                             * the console may have written)         */
    uint32_t shown_bytes;
} STDL_PresentStats;

const STDL_PresentStats *STDL_GetPresentStats(void);

#ifdef __cplusplus
}
#endif

#endif /* STDL_BENCH_H */
