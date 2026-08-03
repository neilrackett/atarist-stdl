/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: CC0-1.0
 *
 * STDL example program, dedicated to the public domain so it can
 * be used as a starting point without licence concerns.
 */
/*
 * vblchk - on-target check for the two things tests/host cannot see.
 *
 * 1. STDL_AddVBL really is an interrupt: the callback has to tick at
 *    the display rate (50Hz in colour low resolution) independently
 *    of what the main program is doing, and stop the moment it is
 *    removed. The host suite can only check the slot bookkeeping.
 *
 * 2. STDL hands the machine back however the program dies. This one
 *    ends by calling Pterm directly - which is what a failed assert,
 *    an abort() or a bus error does: the C runtime never runs, so
 *    atexit handlers never fire. If the GEMDOS terminate vector is
 *    doing its job the desktop comes back in its own resolution and
 *    palette, with no VBL entry left pointing into this program's
 *    freed memory. If it is not, the machine panics a frame or two
 *    later, which is exactly how an out-of-memory used to end.
 *
 * So the last line of output is a claim, and the screen after it is
 * the evidence: a normal desktop means pass.
 */

#include <stdio.h>
#include <mint/osbind.h>
#include <stdl/stdl.h>

static volatile uint16_t vbl_ticks;

/* An interrupt: no GEMDOS, no allocation, no drawing. Counting is
 * about the most a VBL callback should ever do. */
static void count_vbl(void)
{
    vbl_ticks++;
}

static int failures;

static int measure(const char *what, int ms, int lo, int hi)
{
    uint16_t before, after;
    int got;

    before = vbl_ticks;
    STDL_Delay((uint32_t)ms);
    after = vbl_ticks;
    got = (int)(uint16_t)(after - before);

    if (got < lo || got > hi) {
        fprintf(stderr, "FAIL %s: %d ticks in %dms, expected %d-%d\n",
                what, got, ms, lo, hi);
        failures++;
        return 0;
    }
    fprintf(stderr, "PASS %s: %d ticks in %dms\n", what, got, ms);
    return 1;
}

int main(void)
{
    STDL_Surface *screen;

    if (STDL_Init(STDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "init failed: %s\n", STDL_GetError());
        return 1;
    }
    screen = STDL_SetVideoMode(320, 200, 4, 0);
    if (screen == NULL) {
        fprintf(stderr, "video mode failed: %s\n", STDL_GetError());
        return 1;
    }
    STDL_FillRect(screen, NULL, 1);

    if (STDL_AddVBL(count_vbl) < 0) {
        fprintf(stderr, "FAIL add: %s\n", STDL_GetError());
        return 1;
    }
    /* 50Hz for two seconds, with slack for a machine whose display
     * runs at 60Hz and for the 5ms clock the delay is measured on */
    measure("50Hz tick", 2000, 90, 125);

    /* installing again must not double the rate: it is the same
     * callback, and it should still hold exactly one slot */
    if (STDL_AddVBL(count_vbl) < 0) {
        fprintf(stderr, "FAIL re-add: %s\n", STDL_GetError());
        failures++;
    }
    measure("no double install", 1000, 45, 63);

    STDL_RemoveVBL(count_vbl);
    measure("stopped after remove", 500, 0, 0);

    fprintf(stderr, failures == 0 ? "PASS: VBL callbacks\n"
                                  : "FAIL: %d VBL problems\n",
            failures);

    /*
     * Now the abnormal exit. Reinstall so there is something live to
     * clean up, then leave the way a crashing program leaves.
     */
    STDL_AddVBL(count_vbl);
    fprintf(stderr, "terminating without atexit - the desktop should "
                    "come back clean\n");
    Pterm(1);
    return 1;                       /* not reached */
}
