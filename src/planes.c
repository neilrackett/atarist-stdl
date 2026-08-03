/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Plane budget: how many of the four bitplanes the drawing paths
 * maintain.
 *
 * The screen is always four planes - that is the hardware - but a
 * program that only ever draws colours 0-3 leaves planes 2 and 3
 * zero forever, and every write STDL makes to them is wasted bus
 * time. STDL_SetPlaneBudget is the program's promise that no colour
 * index >= 2^planes is drawn again; the primitives then write only
 * the low planes and move proportionally less memory.
 *
 * Skipping a write is only sound if the skipped plane is already
 * zero, so lowering the budget clears the high planes of every
 * screen page STDL owns. Surfaces the program allocates start
 * zeroed (STDL_CreateSurface), so they satisfy the invariant as
 * long as the program keeps its promise.
 */

#include "stdl_internal.h"

int stdl_planes = 4;

/* Zero planes [np, 4) of one planar block. */
static void clear_high(uint8_t *base, int stride, int h, int np)
{
    int y, g, p;
    int groups = stride / 8;

    for (y = 0; y < h; y++) {
        uint16_t *grp = (uint16_t *)(base + (uint32_t)y * stride);
        for (g = 0; g < groups; g++) {
            for (p = np; p < 4; p++) {
                grp[p] = 0;
            }
            grp += 4;
        }
    }
}

/*
 * Re-establish the invariant on a block of planar data that was
 * written wholesale rather than drawn (a Degas splash, an asset
 * bank surface): everything above the budget is truncated away, the
 * same thing the drawing paths do to a colour index. A no-op at the
 * default budget.
 */
void stdl_planes_normalise(uint8_t *base, int stride, int h)
{
    if (stdl_planes < 4 && base != NULL) {
        clear_high(base, stride, h, stdl_planes);
    }
}

/*
 * Re-establish the invariant on the screen pages. Called when the
 * budget is lowered and again from STDL_SetVideoMode, so the two
 * can be issued in either order and a back page allocated later is
 * still covered.
 */
void stdl_planes_clear_screens(void)
{
    int i;

    if (stdl_planes >= 4 || !stdl.video_set) {
        return;
    }
    for (i = 0; i < 2; i++) {
        if (stdl.page[i] != NULL) {
            clear_high(stdl.page[i], STDL_SCREEN_STRIDE,
                       STDL_SCREEN_H, stdl_planes);
        }
    }
}

int STDL_SetPlaneBudget(int planes)
{
    int old = stdl_planes;

    if (planes < 0) {
        return old;                             /* query only */
    }
    if (planes < 1) {
        planes = 1;
    }
    if (planes > 4) {
        planes = 4;
    }
    stdl_planes = planes;
    stdl_planes_clear_screens();
    return old;
}
