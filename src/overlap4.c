/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_BlitSurfaceEx's way into a blit onto its own memory
 * (overlap.c), in an object of its own: the call to STDL_BlitSurfaceEx
 * for the pieces is here, so that only a program that links blit.c
 * links it, and the 8-plane blits' way in (stdl_blit_overlap8) does
 * not bring the 4-plane paths with it.
 */

#include "stdl_internal.h"

int stdl_blit_overlap(const STDL_Surface *src, STDL_Surface *dst, int sx,
                      int sy, int dx, int dy, int w, int h, unsigned flags)
{
    /* two borrowed surfaces whose bytes do not meet, before anything
     * else: a game drawing from a borrowed sprite cache onto a borrowed
     * layer asks with every sprite, and played 1.6% slower on a plain
     * ST for the rest of this; 0.7% with this test. Made inline in
     * STDL_BlitSurfaceEx instead, no call at all, it took the game to
     * 0.2%, but reshaped that function and cost every program's small
     * blits 0.14-0.27% (SPRCOST), where nothing is borrowed. */
    if (src != dst
        && (dst->pixels >= src->pixels + stdl_row_off(src->h, src->stride)
            || src->pixels >= dst->pixels
                              + stdl_row_off(dst->h, dst->stride))) {
        return -1;
    }
    return stdl_blit_overlap_by(STDL_BlitSurfaceEx, src, dst, sx, sy, dx,
                                dy, w, h, flags);
}
