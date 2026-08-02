/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Background restore for sprite-over-background games: push the
 * rectangles you dirtied, restore repaints them from a background
 * surface before the next frame's draws.
 */

#ifndef STDL_DIRTY_H
#define STDL_DIRTY_H

#include <stdl/stdl_types.h>

int  STDL_DirtyInit(STDL_Surface *background, int max_rects);
void STDL_DirtyQuit(void);
void STDL_DirtyPush(const STDL_Rect *r);   /* mark for restore        */
void STDL_DirtyRestore(STDL_Surface *dst); /* repaint from background */
void STDL_DirtyReset(void);

#endif /* STDL_DIRTY_H */
