/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The host pixel tests, run on the machine.
 *
 * tests/host/test_pixel.c checks every fill, blit, sprite and span
 * against a per-pixel reference model - but natively, where the
 * pixel paths are the C twins. Hand-written 68000 in a pixel path
 * is only covered by running the same checks here, against the
 * code the target actually executes. BLITCHK cannot do it: it
 * compares the CPU path with the BLiTTER path, and on a plain ST
 * there is only one of them.
 *
 * The suite runs twice, with the BLiTTER refused and then allowed,
 * so on a BLiTTER machine the accelerated paths meet the same
 * reference model as well. Run it on `st` first:
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Iinclude -Ilib/xpad/src \
 *       -o dist/PIXCHK.TOS tests/hatari/pixchk.c libstdl.a
 *   TOS=$PWD/tmp/tos/etos256uk.img MACHINE=st EXTRA="--memsize 4" \
 *       tests/hatari/run.sh px dist/PIXCHK.TOS 8 \
 *       "waitfor PX:done;waitfor PX:done;waitfor PX:done"
 */
#define main pixchk_suite
#include "../host/test_pixel.c"
#undef main

int main(void)
{
    int use, bad = 0;

    if (STDL_Init(STDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "init failed: %s\n", STDL_GetError());
        return 1;
    }
    printf("PX: blitter %s\n",
           STDL_GetMachineInfo()->has_blitter ? "present" : "absent");
    /* a pass runs for minutes and prints only failures, which looks
     * like a hang to someone watching the machine */
    printf("PX: several minutes per pass;\n"
           "PX: nothing prints unless a test fails\n");
    fflush(stdout);
    for (use = 0; use <= 1; use++) {
        uint32_t t0;
        int r;

        if (use && !STDL_GetMachineInfo()->has_blitter) {
            break;
        }
        STDL_UseBlitter(use);
        failures = 0;
        t0 = STDL_GetTicks();
        r = pixchk_suite();
        printf("PX: BLiTTER %s: %s in %lus\n",
               use ? "allowed" : "refused", r == 0 ? "PASS" : "FAIL",
               (unsigned long)((STDL_GetTicks() - t0) / 1000));
        fflush(stdout);
        bad |= r;
    }
    STDL_UseBlitter(1);
    printf("PX:done %s\n", bad ? "FAIL" : "PASS");
    fflush(stdout);
    STDL_Quit();
    return bad;
}
