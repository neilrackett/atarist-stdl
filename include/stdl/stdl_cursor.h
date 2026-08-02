/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Software mouse cursor, drawn with save-under on the screen
 * surface by the event pump. Black pixels render as colour 0,
 * white as colour 15 ("inverted" cursor pixels render black).
 *
 * The save-under is a snapshot: if the program draws beneath a
 * visible cursor, hide it first (STDL_ShowCursorCtl(0)) and show it
 * again afterwards, or artefacts appear when the cursor moves.
 * Games that redraw every frame should leave the cursor hidden and
 * draw their own pointer as a sprite.
 */

#ifndef STDL_CURSOR_H
#define STDL_CURSOR_H

#include <stdl/stdl_types.h>

typedef struct STDL_Cursor STDL_Cursor;

/*
 * data/mask are MSB-first rows of w/8 bytes (SDL 1.2 layout):
 *   data 1, mask 1 -> black; data 0, mask 1 -> white;
 *   data 1, mask 0 -> black (SDL: inverted); else transparent.
 * w must be a multiple of 8 and at most 32; h at most 32.
 */
STDL_Cursor *STDL_CreateCursor(const uint8_t *data,
                               const uint8_t *mask,
                               int w, int h, int hot_x, int hot_y);
void         STDL_SetCursor(STDL_Cursor *cursor);
STDL_Cursor *STDL_GetCursor(void);
void         STDL_FreeCursor(STDL_Cursor *cursor);

/* 1 = show, 0 = hide, -1 = query; returns the previous state */
int STDL_ShowCursorCtl(int toggle);

#endif /* STDL_CURSOR_H */
