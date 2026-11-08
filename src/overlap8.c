/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The 8-plane blits' way into a blit onto its own memory (overlap.c),
 * with their own blit for the pieces; overlap4.c is STDL_BlitSurfaceEx's.
 * Apart, so that each kind of program links only its own.
 */

#include "stdl_internal.h"

int stdl_blit_overlap8(const STDL_Surface *src, STDL_Surface *dst, int sx,
                       int sy, int dx, int dy, int w, int h, unsigned flags)
{
    return stdl_blit_overlap_by(stdl_resp->blit, src, dst, sx, sy, dx, dy,
                                w, h, flags);
}
