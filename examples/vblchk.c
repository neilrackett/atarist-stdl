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
 * 2. STDL_SetRefresh really changes the display rate, which is only
 *    observable as the VBL rate: ten more ticks a second and a tune
 *    that still plays at the right speed. Checked by measuring,
 *    because the register accepts the write on any machine and the
 *    monitor is what decides whether anything happened.
 *
 * 3. STDL hands the machine back however the program dies. This one
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
    int boot_hz, other_hz, got_hz;

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
    /* Whatever the machine booted at - PAL 50, NTSC 60 - measured
     * over two seconds, with slack for the 5ms clock the delay is
     * timed on. Asking first is the point: the rate is a property
     * of the machine, not an assumption. */
    boot_hz = STDL_SetRefresh(-1);
    fprintf(stderr, "display is %dHz\n", boot_hz);
    measure("boot rate tick", 2000, boot_hz * 2 - 10, boot_hz * 2 + 5);

    /* The other rate. The call returns what actually took effect,
     * so a machine that will not change says so rather than
     * leaving the caller to assume it worked - and the tick count
     * is the independent check on the answer, since the register
     * takes the write either way and the monitor decides. */
    other_hz = (boot_hz == 50) ? 60 : 50;
    got_hz = STDL_SetRefresh(other_hz);
    if (got_hz != other_hz) {
        fprintf(stderr, "SKIP switch: asked %dHz, got %dHz\n",
                other_hz, got_hz);
    } else {
        measure("switched rate tick", 2000,
                other_hz * 2 - 10, other_hz * 2 + 5);
        if (STDL_SetRefresh(boot_hz) != boot_hz) {
            fprintf(stderr, "FAIL: could not switch back\n");
            failures++;
        } else {
            measure("back at the boot rate", 2000,
                    boot_hz * 2 - 10, boot_hz * 2 + 5);
        }
    }

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
    /* leave the machine on the other rate deliberately: the
     * terminate vector has to put the sync register back too, and
     * a desktop that comes back rolling is the failure */
    STDL_SetRefresh(other_hz);
    fprintf(stderr, "terminating without atexit at %dHz - the desktop "
                    "should come back clean and at %dHz\n",
            other_hz, boot_hz);
    Pterm(1);
    return 1;                       /* not reached */
}
