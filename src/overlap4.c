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
    return stdl_blit_overlap_by(STDL_BlitSurfaceEx, src, dst, sx, sy, dx,
                                dy, w, h, flags);
}
