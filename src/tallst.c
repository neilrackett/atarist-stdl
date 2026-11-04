/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * A tall screen on an ST, an STE or a Mega STE (STDL_RESPONSIVE_TALL):
 * its own screen with the bottom border open. An object of its own, as
 * it names the border code (overscan.c and what it links, 8.7K of a
 * program) that a program asking for the display's own height
 * (STDL_RESPONSIVE_HEIGHT) never runs.
 */

#include "stdl_internal.h"
#include <stdl/stdl_responsive.h>

/* 240 of the border's 245 lines; without it (a program already using
 * Timer B for rasters) the screen stays at 200 and the error says why.
 * The cap is set while the border is this screen's, not the program's
 * own, and the release clears it */
void stdl_resp_tall_st(int h)
{
    /* a 200-line set after a tall one: its border had stayed open */
    if (h <= 200) {
        if (stdl_ovsc_cap != 0) {
            STDL_CloseBottomBorder();
            stdl_ovsc_cap = 0;
        }
        return;
    }
    if (stdl_ovsc_cap != 0) {
        return;
    }
    stdl_ovsc_cap = 240;
    if (STDL_OpenBottomBorder() == 0) {
        stdl_ovsc_cap = 0;
    }
}
