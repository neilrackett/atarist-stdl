/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_DirtyBackground: the background itself changed (a coin taken,
 * a score redrawn into it, a new level), so every page must put that
 * rectangle back as it next comes round, not only the page that drew
 * over it. Each page's list gets the rectangle (NULL: the whole page,
 * as an overflow does); pages not seen yet get every change made since
 * the lists began when dirty.c first claims a list for them, through
 * the hook. Without this a game kept a list of its own and a count of
 * pages for each change; the starter did, and every game copied from
 * it would have.
 *
 * An object of its own, 312 bytes, so a program that never calls it
 * links none of it: dirty.c pays a test and a call when it claims a
 * list, and with its lists reachable from here came out 28 bytes
 * smaller (1,992 to 1,964), their 20 bytes more of BSS (the unseen
 * pages' slot and the hook) counted at the link instead.
 */

#include <stdlib.h>
#include "stdl_internal.h"

#define unseen stdl_dirty_lists[STDL_DIRTY_LISTS]

static void add(stdl_dirty_list_t *l, const STDL_Rect *r)
{
    if (r == NULL || l->rects == NULL || l->n >= stdl_dirty_max) {
        l->overflowed = 1;      /* restored whole instead */
        return;
    }
    l->rects[l->n++] = *r;
}

/* a page seen for the first time: every change since the lists began */
static void seed(int i)
{
    stdl_dirty_list_t *l = &stdl_dirty_lists[i];
    int k;

    for (k = 0; k < unseen.n; k++) {
        l->rects[k] = unseen.rects[k];
    }
    l->n = unseen.n;
    l->overflowed = unseen.overflowed;
}

void STDL_DirtyBackground(const STDL_Rect *r)
{
    int i;

    if (stdl_dirty_lists[0].rects == NULL) {
        return;                 /* no STDL_DirtyInit */
    }
    if (unseen.rects == NULL && !unseen.overflowed) {
        unseen.rects = malloc(sizeof(STDL_Rect) * (unsigned)stdl_dirty_max);
        if (unseen.rects == NULL) {
            unseen.overflowed = 1;  /* new pages restored whole */
        }
        stdl_dirty_claimed = seed;
    }
    for (i = 0; i <= STDL_DIRTY_LISTS; i++) {
        /* every list there is - claimed, collecting pushes before the
         * first restore, or the unseen pages' - gets the change */
        if (stdl_dirty_lists[i].rects != NULL || i == STDL_DIRTY_LISTS) {
            add(&stdl_dirty_lists[i], r);
        }
    }
}
