/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_Dirty: background-restore bookkeeping. Rectangles pushed
 * since the last reset are repainted from the background surface.
 */

#include <stdlib.h>
#include "stdl_internal.h"

static STDL_Surface *bg;
static STDL_Rect *rects;
static int max_rects;
static int nrects;
static int overflowed;

int STDL_DirtyInit(STDL_Surface *background, int max)
{
    STDL_DirtyQuit();
    if (background == NULL || max <= 0) {
        STDL_SetError("bad dirty init");
        return -1;
    }
    rects = malloc(sizeof(STDL_Rect) * (unsigned)max);
    if (rects == NULL) {
        STDL_SetError("out of memory");
        return -1;
    }
    bg = background;
    max_rects = max;
    nrects = 0;
    overflowed = 0;
    return 0;
}

void STDL_DirtyQuit(void)
{
    free(rects);
    rects = NULL;
    bg = NULL;
    max_rects = 0;
    nrects = 0;
    overflowed = 0;
}

void STDL_DirtyPush(const STDL_Rect *r)
{
    if (r == NULL || rects == NULL) {
        return;
    }
    if (nrects >= max_rects) {
        overflowed = 1;         /* fall back to full restore */
        return;
    }
    rects[nrects++] = *r;
}

void STDL_DirtyRestore(STDL_Surface *dst)
{
    int i;

    if (bg == NULL || dst == NULL) {
        return;
    }
    if (overflowed) {
        STDL_BlitSurface(bg, NULL, dst, NULL);
    } else {
        for (i = 0; i < nrects; i++) {
            STDL_Rect d = rects[i];
            STDL_BlitSurface(bg, &rects[i], dst, &d);
        }
    }
    STDL_DirtyReset();
}

void STDL_DirtyReset(void)
{
    nrects = 0;
    overflowed = 0;
}
