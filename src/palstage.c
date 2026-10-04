/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Palette staging: changes held until the vertical blank.
 *
 * A palette written from the main loop lands wherever the beam is,
 * and for the rest of that frame the picture above the beam shows
 * the old colours and the picture below it the new - a split a
 * palette cycle or a fade shows every frame. Staged, a change is
 * converted to hardware words where it is made and written by a VBL
 * callback inside the blanking, so every frame shows one palette.
 *
 * Two users share the one mechanism: STDL_StagePalette for a program
 * that asks, and overscan.c while a border is open (the picture
 * starts at line 34 then, so an unstaged write is even likelier to
 * land in it). Each holds a bit; the VBL slot and the hook stay while
 * either does, so closing a border leaves a program's own staging in
 * place. Its own translation unit, so a program that never stages
 * does not pay for it; the hook itself lives in always-linked
 * video.c, leaving palette.o a pointer test.
 */

#include "stdl_internal.h"

static uint16_t pending[16];
static volatile uint16_t dirty;         /* a bit a register to write */
static uint8_t users;

/* stdl_pal_apply_hook while staging: convert now, write in the VBL.
 * The mask drops first so a VBL landing halfway through cannot
 * write a half-converted palette; it writes the whole one at the
 * next blank instead. */
static void stage(void)
{
    int i;

    dirty = 0;
    for (i = 0; i < 16; i++) {
        pending[i] = STDL_HWColour(stdl.colours[i].r, stdl.colours[i].g,
                                   stdl.colours[i].b);
    }
    dirty = 0xFFFFu;
}

/* one register's word, from the SDL shim's physical palette: that
 * register alone is marked, so the blank writes it and nothing else
 * - not the other fifteen, which STDL_SetColour may have changed
 * since, and which an unstaged write would not have touched */
static int stage_raw(int index, uint16_t word)
{
    pending[index & 15] = word;
    dirty |= (uint16_t)(1u << (index & 15));
    return 1;
}

static void flush(void)
{
    const uint16_t m = dirty;
    int i;

    if (m != 0) {
        dirty = 0;
        for (i = 0; i < 16; i++) {
            if (m & (1u << i)) {
                STDL_HWPAL[i] = pending[i];
            }
        }
    }
}

int stdl_pal_stage_on(int who)
{
    /* release_hardware clears the hook and the VBL slots together, so
     * a hook that is not ours means nothing is installed, whatever
     * the bits say */
    if (stdl_pal_apply_hook != stage) {
        /* nothing left over from before a release - a fade-out just
         * before STDL_Quit - may reach the registers */
        dirty = 0;
        if (STDL_AddVBL(flush) != 0) {
            return -1;
        }
        users = 0;
        /* the set starts as what is on screen, so the first raw write
         * to one register has the rest to sit beside */
        stage();
        dirty = 0;
        stdl_pal_apply_hook = stage;
        stdl_pal_raw_hook = stage_raw;
    }
    users |= (uint8_t)who;
    return 0;
}

void stdl_pal_stage_off(int who)
{
    users &= (uint8_t)~who;
    if (users == 0 && stdl_pal_apply_hook == stage) {
        STDL_RemoveVBL(flush);
        stdl_pal_apply_hook = NULL;
        stdl_pal_raw_hook = NULL;
        flush();                /* a change still waiting goes now */
    }
}

int STDL_StagePalette(int on)
{
    if (on) {
        return stdl_pal_stage_on(STDL_PAL_STAGE_USER);
    }
    stdl_pal_stage_off(STDL_PAL_STAGE_USER);
    return 0;
}
