/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The SDL2 add-on's rectangles and points, SDL2's own layout.
 */

#ifndef STDL_SDL2_RECT_H
#define STDL_SDL2_RECT_H

#include "SDL_stdinc.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SDL_Point {
    int x;
    int y;
} SDL_Point;

typedef struct SDL_Rect {
    int x, y;
    int w, h;
} SDL_Rect;

SDL_FORCE_INLINE SDL_bool SDL_RectEmpty(const SDL_Rect *r)
{
    return (!r || r->w <= 0 || r->h <= 0) ? SDL_TRUE : SDL_FALSE;
}

SDL_FORCE_INLINE SDL_bool SDL_RectEquals(const SDL_Rect *a, const SDL_Rect *b)
{
    return (a && b && a->x == b->x && a->y == b->y && a->w == b->w &&
            a->h == b->h) ? SDL_TRUE : SDL_FALSE;
}

#ifdef __cplusplus
}
#endif

#endif /* STDL_SDL2_RECT_H */
