/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Software mouse cursor with save-under. The event pump calls the
 * cursor hook after processing motion; the cursor is undrawn from
 * its old position (restoring the saved pixels) and drawn at the
 * new one. Everything happens on the screen surface.
 */

#include <stdlib.h>
#include <string.h>
#include "stdl_internal.h"
#include <stdl/stdl_cursor.h>

#define CUR_MAX 32              /* max cursor width/height          */
#define CUR_GROUPS 3            /* 32px window spans <= 3 groups    */

struct STDL_Cursor {
    int      w, h;
    int      hot_x, hot_y;
    uint32_t data[CUR_MAX];     /* left-aligned, bit 31 = leftmost  */
    uint32_t mask[CUR_MAX];
};

static STDL_Cursor *current;
static int visible = 1;         /* SDL default: shown               */
static int drawn;
static int drawn_x, drawn_y;    /* hotspot position when drawn      */
static int drawn_np = 4;        /* plane budget the save-under used */

static uint16_t saved[CUR_MAX][CUR_GROUPS][4];

static void cursor_undraw(void)
{
    STDL_Cursor *c = current;
    int x0, y0, row, g, p, gx, screen_groups;
    int np = drawn_np;          /* restore exactly what was saved */

    if (!drawn || c == NULL || stdl_screen.pixels == NULL) {
        drawn = 0;
        return;
    }
    x0 = drawn_x - c->hot_x;
    y0 = drawn_y - c->hot_y;
    gx = x0 >> 4;               /* arithmetic shift floors */
    screen_groups = stdl_screen.stride / 8;

    for (row = 0; row < c->h; row++) {
        int py = y0 + row;
        uint8_t *line;
        if (py < 0 || py >= stdl_screen.h) {
            continue;
        }
        line = stdl_screen.pixels + (uint32_t)py * stdl_screen.stride;
        for (g = 0; g < CUR_GROUPS; g++) {
            uint16_t *grp;
            if (gx + g < 0 || gx + g >= screen_groups) {
                continue;
            }
            grp = (uint16_t *)(line + (gx + g) * 8);
            for (p = 0; p < np; p++) {
                grp[p] = saved[row][g][p];
            }
        }
    }
    drawn = 0;
}

static void cursor_draw(int mx, int my)
{
    STDL_Cursor *c = current;
    int x0, y0, r, gx, row, g, p, screen_groups;
    int np = stdl_planes;

    if (c == NULL || !visible || stdl_screen.pixels == NULL) {
        return;
    }
    x0 = mx - c->hot_x;
    y0 = my - c->hot_y;
    r = x0 & 15;
    gx = x0 >> 4;               /* arithmetic shift floors */
    screen_groups = stdl_screen.stride / 8;

    for (row = 0; row < c->h; row++) {
        int py = y0 + row;
        uint8_t *line;
        uint64_t paint, white;

        if (py < 0 || py >= stdl_screen.h) {
            continue;
        }
        line = stdl_screen.pixels + (uint32_t)py * stdl_screen.stride;

        /* place the 32 cursor bits in a 48-bit window whose top bit
         * is the first pixel of group gx */
        paint = (uint64_t)(c->mask[row] | c->data[row]) << (16 - r);
        white = (uint64_t)(c->mask[row] & ~c->data[row]) << (16 - r);

        for (g = 0; g < CUR_GROUPS; g++) {
            uint16_t pm = (uint16_t)(paint >> (32 - 16 * g));
            uint16_t wm = (uint16_t)(white >> (32 - 16 * g));
            uint16_t *grp;

            if (gx + g < 0 || gx + g >= screen_groups) {
                continue;
            }
            grp = (uint16_t *)(line + (gx + g) * 8);
            for (p = 0; p < np; p++) {
                saved[row][g][p] = grp[p];
                /* black = all plane bits clear, white = all set -
                 * at a reduced budget "white" is the top colour of
                 * the budget, and the planes above it stay zero */
                grp[p] = (uint16_t)((grp[p] & ~pm) | wm);
            }
        }
    }
    drawn = 1;
    drawn_x = mx;
    drawn_y = my;
    drawn_np = np;
}

/* event-pump hook: track the mouse */
static void cursor_motion(int x, int y)
{
    if (drawn && x == drawn_x && y == drawn_y && visible) {
        return;
    }
    cursor_undraw();
    cursor_draw(x, y);
}

/* ---------------------------------------------------------------- */

STDL_Cursor *STDL_CreateCursor(const uint8_t *data, const uint8_t *mask,
                               int w, int h, int hot_x, int hot_y)
{
    STDL_Cursor *c;
    int row, b, bpr;

    if (data == NULL || mask == NULL || w <= 0 || h <= 0
        || w > CUR_MAX || h > CUR_MAX || (w & 7) != 0) {
        STDL_SetError("bad cursor size (w multiple of 8, max 32x32)");
        return NULL;
    }
    c = calloc(1, sizeof(STDL_Cursor));
    if (c == NULL) {
        STDL_SetError("out of memory");
        return NULL;
    }
    c->w = w;
    c->h = h;
    c->hot_x = hot_x;
    c->hot_y = hot_y;
    bpr = w / 8;
    for (row = 0; row < h; row++) {
        uint32_t d = 0, m = 0;
        for (b = 0; b < bpr; b++) {
            d |= (uint32_t)data[row * bpr + b] << (24 - b * 8);
            m |= (uint32_t)mask[row * bpr + b] << (24 - b * 8);
        }
        c->data[row] = d;
        c->mask[row] = m;
    }
    return c;
}

void STDL_SetCursor(STDL_Cursor *cursor)
{
    int x, y;

    cursor_undraw();
    current = cursor;
    stdl_cursor_hook = (cursor != NULL) ? cursor_motion : NULL;
    if (cursor != NULL && visible) {
        STDL_GetMouseState(&x, &y);
        cursor_draw(x, y);
    }
}

STDL_Cursor *STDL_GetCursor(void)
{
    return current;
}

void STDL_FreeCursor(STDL_Cursor *cursor)
{
    if (cursor == NULL) {
        return;
    }
    if (cursor == current) {
        STDL_SetCursor(NULL);
    }
    free(cursor);
}

int STDL_ShowCursorCtl(int toggle)
{
    int old = visible;
    int x, y;

    if (toggle < 0) {
        return old;
    }
    if (toggle != visible) {
        visible = toggle ? 1 : 0;
        cursor_undraw();
        if (visible && current != NULL) {
            STDL_GetMouseState(&x, &y);
            cursor_draw(x, y);
        }
    }
    return old;
}
