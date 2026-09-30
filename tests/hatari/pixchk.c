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
 * reference model as well.
 *
 * Two builds. The default samples the suite - PIXCHK_QUICK cases of
 * each test, spread over its range - for real hardware: about 20s a
 * pass on a plain ST, 18s on an STE, 12s on a Mega STE, and every
 * test still runs. With -DPIXCHK_FULL it runs every case and prints
 * each test's time as it goes: 16 minutes a pass on a plain ST, a
 * job for the emulator. (It was three hours, most of it the suite's
 * own checking - see AGENTS.md.) Run the full build on `st` first:
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Iinclude -Ilib/xpad/src -DPIXCHK_FULL \
 *       -o dist/PIXFULL.TOS tests/hatari/pixchk.c libstdl.a
 *   TOS=$PWD/tmp/tos/etos256uk.img MACHINE=st EXTRA="--memsize 4" \
 *       tests/hatari/run.sh px dist/PIXFULL.TOS 8 \
 *       "waitfor PX:done;waitfor PX:done;waitfor PX:done"
 *
 * and the sampled one, dist/PIXCHK.TOS, on the machine.
 */
#include <stdio.h>
#include <stdl/stdl.h>

#ifdef PIXCHK_FULL
#define PIXCHK_QUICK 0
#define PIXCHK_VERBOSE
#elif !defined(PIXCHK_QUICK)
/* cases per test: see PX_SKIP */
#define PIXCHK_QUICK 3
#endif

/* each test starts its case count at zero, so a sampled run always
 * takes its first case; the full run (or -DPIXCHK_VERBOSE) also
 * reports each test's time - progress for whoever is watching, and
 * the number that says which test to trim when the suite is slow */
#ifdef PIXCHK_VERBOSE
#define PIXEL_RUN(t) do { \
        uint32_t t0_ = STDL_GetTicks(); \
        px_case = 0; \
        t(); \
        printf("PX:  %-24s %lums\n", #t, \
               (unsigned long)(STDL_GetTicks() - t0_)); \
        fflush(stdout); \
    } while (0)
#else
#define PIXEL_RUN(t) do { px_case = 0; t(); } while (0)
#endif

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
    px_quick = PIXCHK_QUICK;
    if (px_quick > 0) {
        printf("PX: %u cases a test,\n"
               "PX: about 20s a pass\n", px_quick);
    } else {
        printf("PX: every case,\n"
               "PX: about 16 min a pass on an ST\n");
    }
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
        printf("PX: BLiTTER %s: %s in %lums\n",
               use ? "allowed" : "refused", r == 0 ? "PASS" : "FAIL",
               (unsigned long)(STDL_GetTicks() - t0));
        fflush(stdout);
        bad |= r;
    }
    STDL_UseBlitter(1);
    printf("PX:done %s\n", bad ? "FAIL" : "PASS");
    fflush(stdout);
    STDL_Quit();
    return bad;
}
