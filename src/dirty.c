/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_Dirty: background-restore bookkeeping. Rectangles pushed
 * since the last reset are repainted from the background surface.
 *
 * One list per page. A double-buffered game draws frame N into one
 * page and frame N+1 into the other, so the rectangles to erase from
 * a page are the ones drawn into it the last time it was the back
 * page - two frames ago, not one. A single list replayed onto
 * whichever page is being drawn restores the wrong page's sprites
 * and leaves its own behind. So the lists are keyed by the page's
 * pixels (which STDL_Flip swaps under stdl_screen): STDL_DirtyRestore
 * replays the list of the page it is given and makes that list the
 * one STDL_DirtyPush fills until the next restore. A single-buffered
 * game only ever names one page and gets exactly the old behaviour.
 *
 * The second list is allocated when a second page first appears, so
 * a single-buffered game pays for one. A third page takes the slot of
 * the page restored least recently, and that page is remembered as
 * owing a whole restore: its list is gone, but when it comes back
 * the background is copied over it entirely, which is always correct.
 * Four such pages are remembered - enough for an off-screen surface
 * restored now and then, or a border opening and bringing its own
 * pair of pages.
 */

#include <stdlib.h>
#include <string.h>
#include "stdl_internal.h"

typedef struct {
    const uint8_t *key;         /* the page's pixels; NULL = unclaimed */
    STDL_Rect *rects;
    int n;
    int overflowed;             /* full restore instead of the list  */
} dirty_list_t;

static STDL_Surface *bg;
static dirty_list_t lists[2];
static int cur;                 /* the list STDL_DirtyPush fills      */
static int max_rects;
/* evicted pages, each owed a whole restore when it next comes back;
 * four remembered, oldest forgotten first */
static const uint8_t *owes[4];
static int owes_next;
static int shared;              /* no room for a second list: both
                                 * pages share one, restored whole    */

int STDL_DirtyInit(STDL_Surface *background, int max)
{
    STDL_DirtyQuit();
    if (background == NULL || max <= 0) {
        STDL_SetError("bad dirty init");
        return -1;
    }
    lists[0].rects = malloc(sizeof(STDL_Rect) * (unsigned)max);
    if (lists[0].rects == NULL) {
        STDL_SetError("out of memory");
        return -1;
    }
    bg = background;
    max_rects = max;
    return 0;
}

void STDL_DirtyQuit(void)
{
    free(lists[0].rects);
    free(lists[1].rects);
    lists[0].rects = lists[1].rects = NULL;
    lists[0].key = lists[1].key = NULL;
    lists[0].n = lists[1].n = 0;
    lists[0].overflowed = lists[1].overflowed = 0;
    cur = 0;
    memset(owes, 0, sizeof(owes));
    owes_next = 0;
    shared = 0;
    bg = NULL;
    max_rects = 0;
}

void STDL_DirtyPush(const STDL_Rect *r)
{
    dirty_list_t *l = &lists[cur];

    if (r == NULL || l->rects == NULL) {
        return;
    }
    if (l->n >= max_rects) {
        l->overflowed = 1;      /* fall back to full restore */
        return;
    }
    l->rects[l->n++] = *r;
}

/* the list for page `key`, claiming or evicting one if it has none */
static int list_for(const uint8_t *key)
{
    int i, other;

    if (shared) {
        lists[0].key = key;
        lists[0].overflowed = 1;
        return 0;
    }
    for (i = 0; i < 2; i++) {
        if (lists[i].key == key) {
            return i;
        }
    }
    /* before any restore, pushes collect in the current list without
     * a page: they belong to the first page restored */
    if (lists[cur].key == NULL) {
        lists[cur].key = key;
        return cur;
    }
    other = cur ^ 1;
    if (lists[other].rects == NULL) {
        lists[other].rects = malloc(sizeof(STDL_Rect) * (unsigned)max_rects);
        if (lists[other].rects == NULL) {
            /*
             * Two pages and room for one list. Share it and restore
             * both pages whole from here on - slower, never wrong.
             */
            STDL_SetError("out of memory for a second dirty list");
            shared = 1;
            cur = 0;
            lists[0].key = key;
            lists[0].overflowed = 1;
            return 0;
        }
    } else if (lists[other].key != NULL) {
        /* a third page: the other list's page was restored least
         * recently; its rectangles go, and it owes a whole restore */
        owes[owes_next] = lists[other].key;
        owes_next = (owes_next + 1) & 3;
    }
    lists[other].key = key;
    lists[other].n = 0;
    lists[other].overflowed = 0;
    return other;
}

void STDL_DirtyRestore(STDL_Surface *dst)
{
    dirty_list_t *l;
    int i, full;

    if (bg == NULL || dst == NULL || lists[0].rects == NULL) {
        return;
    }
    cur = list_for(dst->pixels);
    l = &lists[cur];
    full = l->overflowed;
    for (i = 0; i < 4; i++) {
        if (owes[i] == dst->pixels) {
            owes[i] = NULL;
            full = 1;
        }
    }
    if (full) {
        STDL_BlitSurface(bg, NULL, dst, NULL);
    } else {
        for (i = 0; i < l->n; i++) {
            STDL_Rect d = l->rects[i];
            STDL_BlitSurfaceEx(bg, &l->rects[i], dst, &d, 0);
        }
    }
    l->n = 0;
    l->overflowed = shared;     /* a shared list stays whole */
}

void STDL_DirtyReset(void)
{
    lists[cur].n = 0;
    lists[cur].overflowed = shared;
}
