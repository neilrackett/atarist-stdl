/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * A tall screen in 16 colours (STDL_RESPONSIVE_TALL or _HEIGHT without
 * STDL_RESPONSIVE_COLOUR): 320x240, an ordinary 4-plane surface on
 * every machine, so every call and a program's own writes work on it
 * as they do on an ST Low screen.
 *   - An ST, an STE or a Mega STE: its own screen, with the bottom
 *     border open for a tall one (tallst.c), 240 of the 245 lines it
 *     gives; the display's own height there is its 200.
 *   - A Falcon: a 4-bit VIDEL mode, the ST's own plane layout, so the
 *     surface is the display page and Flip has nothing to do.
 *   - A TT: TT Low, eight planes and its rows on two lines each, so the
 *     surface is a 4-plane page of its own, and Flip compares it with
 *     what the display page shows and copies what changed, the upper
 *     four planes left at zero. A compare catches every write, whoever
 *     made it, where tracking would see only the library's calls.
 * Colours 0-15 go to the TT's or the Falcon's own registers
 * (responsive.c), which the 4-bit modes read in place of the ST's.
 */

#include "responsive.h"

static STDL_Surface *t4_page[2];        /* a TT's pages drawn into */

static int t4_screen_init(void)
{
    int p;

    stdl_rs.track = NULL;       /* the present compares whole pages */
    if (stdl_rs.falcon) {
        return 0;
    }
    for (p = 0; p < 2; p++) {
        t4_page[p] = stdl_surface_create(stdl_rs.w, stdl_rs.h);
        if (t4_page[p] == NULL) {
            return -1;
        }
    }
    return 0;
}

static void t4_screen_page(int p)
{
    STDL_Surface *s = &stdl_rs.screen.s;

    if (stdl_rs.falcon) {
        s->pixels = stdl_rs.page[p] + stdl_rs.left
                  + stdl_row_off(stdl_rs.top, (uint16_t)stdl_rs.line);
        s->stride = (uint16_t)stdl_rs.line;
    } else {
        s->pixels = t4_page[p]->pixels;
        s->stride = t4_page[p]->stride;
    }
}

static void t4_screen_free(void)
{
    int p;

    for (p = 0; p < 2; p++) {
        STDL_FreeSurface(t4_page[p]);
        t4_page[p] = NULL;
    }
}

/* a TT: the groups of page p that differ from what display page p
 * shows, onto both of their lines; the display's upper planes are
 * never written, so they stay at the zero the page was cleared to */
static void t4_present(int p, const resp_box *b)
{
    const int line = stdl_rs.line, ng = stdl_rs.w >> 4;
    const uint8_t *src;
    uint8_t *dst;
    uint32_t cells = 0;
    int y, g;

    /* before the page is read: a Falcon has none */
    if (stdl_rs.falcon) {
        return;
    }
    src = t4_page[p]->pixels;
    dst = stdl_rs.page[p] + stdl_row_off(stdl_rs.top, (uint16_t)line);
    if (b->x0 <= b->x1) {
        /* the console wrote it (responsive.c's resp_whole: nothing else
         * gives this screen a box, as it tracks none): every group, all
         * eight planes of both lines, so its upper planes and its odd
         * lines go too */
        for (y = stdl_rs.h; --y >= 0; src += t4_page[p]->stride,
             dst += 2 * line) {
            const uint32_t *s = (const uint32_t *)src;
            uint32_t *d = (uint32_t *)dst;

            for (g = ng; --g >= 0; s += 2, d += 4, cells++) {
                uint32_t *e = (uint32_t *)((uint8_t *)d + line);

                d[0] = e[0] = s[0];
                d[1] = e[1] = s[1];
                d[2] = e[2] = 0;
                d[3] = e[3] = 0;
            }
        }
        stdl_rs.stats.groups += cells;
        return;
    }
    for (y = stdl_rs.h; --y >= 0; src += t4_page[p]->stride,
         dst += 2 * line) {
        const uint32_t *s = (const uint32_t *)src;
        uint32_t *d = (uint32_t *)dst;

        for (g = ng; --g >= 0; s += 2, d += 4) {
            if (d[0] != s[0] || d[1] != s[1]) {
                uint32_t *e = (uint32_t *)((uint8_t *)d + line);

                d[0] = e[0] = s[0];
                d[1] = e[1] = s[1];
                cells++;
            }
        }
    }
    stdl_rs.stats.groups += cells;
}

/* no ops: its surfaces have 4 planes, and are made and drawn as an
 * ST's are */
const resp_model stdl_resp_tall = {
    .planes = 4,
    .screen_init = t4_screen_init, .screen_page = t4_screen_page,
    .screen_free = t4_screen_free, .present = t4_present
};
