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
    memset(lists, 0, sizeof(lists));
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
            return list_for(key);
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

/*
 * The restore without STDL_BlitSurfaceEx. A background and the page it
 * restores are normally the same shape: same size and stride, no
 * origins, no masks. A rectangle is then an offset into both, two edge
 * masks and a run of whole groups, and the general blit's set-up -
 * clipping both surfaces, the BLiTTER's rules, the phase and mask
 * dispatch - is most of the cost of a sprite-sized restore. Measured
 * on a plain ST: a 16x16 at x & 15 = 5 cost about 9200 cycles through
 * it, of which the pixels were under 4000.
 *
 * Exactly what the general path draws: the rectangle clipped to the
 * page's clip, edge groups merged under their masks, nothing rounded
 * out. With a BLiTTER present, or a plane budget, or anything that
 * makes the two surfaces differ, the general path does it as before.
 */
static int restore_direct(const STDL_Surface *dst)
{
    return stdl_planes == 4 && !stdl_blitter_active()
        && bg->w == dst->w && bg->h == dst->h
        && bg->stride == dst->stride && dst->mask == NULL
        && !((bg->flags & STDL_SRCKEY) && bg->mask != NULL)
        && (bg->org_x | bg->org_y | dst->org_x | dst->org_y) == 0;
}

/* one column of groups, h rows, under mask m: d ^= (d ^ s) & m */
static void merge_col(uint8_t *d, const uint8_t *s, uint16_t m, int h,
                      int stride)
{
    const uint32_t ml = ((uint32_t)m << 16) | m;

    while (h-- > 0) {
        stdl_wlong *dl = (stdl_wlong *)d;
        const stdl_wlong *sl = (const stdl_wlong *)s;

        dl[0] ^= (dl[0] ^ sl[0]) & ml;
        dl[1] ^= (dl[1] ^ sl[1]) & ml;
        d += stride;
        s += stride;
    }
}

static void restore_rect(STDL_Surface *dst, const STDL_Rect *r)
{
    const STDL_Rect *c = &dst->clip;
    const int stride = dst->stride;
    int x0 = r->x, y0 = r->y, x1 = x0 + r->w, y1 = y0 + r->h;
    int g0, g1, h, n;
    uint16_t lm, rm;
    uint32_t off;
    uint8_t *d;
    const uint8_t *s;

    if (x0 < c->x) x0 = c->x;
    if (y0 < c->y) y0 = c->y;
    if (x1 > c->x + c->w) x1 = c->x + c->w;
    if (y1 > c->y + c->h) y1 = c->y + c->h;
    if (x0 >= x1 || y0 >= y1) {
        return;
    }
    h = y1 - y0;
    g0 = x0 >> 4;
    g1 = (x1 - 1) >> 4;                 /* inclusive */
    lm = (uint16_t)(0xFFFFu >> (x0 & 15));
    rm = (uint16_t)(0xFFFFu << (15 - ((x1 - 1) & 15)));
    off = stdl_row_off(y0, (uint16_t)stride) + (uint32_t)(g0 << 3);
    d = dst->pixels + off;
    s = bg->pixels + off;
    if (g0 == g1) {
        lm &= rm;               /* one group, both edges in it */
        rm = 0xFFFFu;
    }
    if (lm != 0xFFFFu) {
        merge_col(d, s, lm, h, stride);
        d += 8;
        s += 8;
        g0++;
    }
    if (rm != 0xFFFFu) {
        merge_col(d + ((g1 - g0) << 3), s + ((g1 - g0) << 3), rm, h,
                  stride);
        g1--;
    }
    n = g1 - g0 + 1;
    if (n > 0) {
        stdl_copy_rows_groups(d, s, n, h, stride - (n << 3),
                              stride - (n << 3));
    }
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
    } else if (restore_direct(dst)) {
        for (i = 0; i < l->n; i++) {
            restore_rect(dst, &l->rects[i]);
        }
    } else {
        for (i = 0; i < l->n; i++) {
            STDL_Rect d = l->rects[i];
            STDL_BlitSurfaceEx(bg, &l->rects[i], dst, &d, 0);
        }
    }
    STDL_DirtyReset();          /* cur is this page's list */
}

void STDL_DirtyReset(void)
{
    lists[cur].n = 0;
    lists[cur].overflowed = shared;     /* a shared list stays whole */
}
