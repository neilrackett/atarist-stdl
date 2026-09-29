/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Background restore for sprite-over-background games: push the
 * rectangles you dirtied, restore repaints them from a background
 * surface before the next frame's draws.
 *
 * A frame is: STDL_DirtyRestore(page), draw, STDL_DirtyPush each
 * rectangle drawn into that page. Each page keeps its own list, keyed
 * by its pixels, so this works unchanged with STDL_DOUBLEBUF: restoring
 * the back page erases what was drawn into that page the last time
 * it was the back page, not what went into the page on screen.
 * Pushes go to the list of the page restored last (before any
 * restore, to the first page restored). STDL_DirtyReset forgets that
 * list only. Two pages are tracked; a third evicts the one restored
 * least recently, which is then restored whole when it next comes
 * back. Restoring is always from the background, so a rectangle
 * pushed twice or too many only costs time; overflowing max_rects
 * restores that page whole.
 */

#ifndef STDL_DIRTY_H
#define STDL_DIRTY_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdl/stdl_types.h>

int  STDL_DirtyInit(STDL_Surface *background, int max_rects);
void STDL_DirtyQuit(void);
void STDL_DirtyPush(const STDL_Rect *r);   /* mark for restore        */
void STDL_DirtyRestore(STDL_Surface *dst); /* repaint from background */
void STDL_DirtyReset(void);

#ifdef __cplusplus
}
#endif

#endif /* STDL_DIRTY_H */
