/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/*
 * STDL_SetRefresh and the rate-awareness it demands of the music
 * tick.
 *
 * The part worth testing on the host is not the register write -
 * that is one store - but the consequence: every tune in the
 * library is replayed against the VBL, and the VBL rate is what
 * this call changes. A stream that assumed 50 would play 20% fast
 * at 60Hz, and nothing about the sound would say so. So the test
 * that matters counts how many VBL ticks a stream of known length
 * takes to finish at each rate, which is a property the host can
 * check exactly and an emulator can only be listened to.
 *
 * stubs.c provides the sync register as plain memory.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdl/stdl.h>
#include "stdl_internal.h"

static int failures;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        failures++; \
        printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } \
} while (0)

/* an STM of `frames` empty frames at `hz`, played once */
static const char *write_stm(unsigned hz, unsigned frames)
{
    static char path[] = "/tmp/stdl_test_XXXXXX.stm";
    static char named[64];
    FILE *f;
    unsigned i;

    snprintf(named, sizeof named, "/tmp/stdl_refresh_%u_%u.stm",
             hz, frames);
    f = fopen(named, "wb");
    if (f == NULL) {
        return NULL;
    }
    (void)path;
    fputs("STM1", f);
    fputc((int)(hz >> 8), f);      fputc((int)(hz & 255), f);
    fputc((int)(frames >> 8), f);  fputc((int)(frames & 255), f);
    fputc(0, f); fputc(0, f);      /* loop_frame */
    fputc(0, f); fputc(0, f);      /* reserved   */
    for (i = 0; i < frames; i++) {
        fputc(0, f); fputc(0, f);  /* change mask: nothing */
    }
    fclose(f);
    return named;
}

/* ticks the stream takes to finish, at the given VBL rate */
static int ticks_to_finish(const char *path, int vbl_hz, int bound)
{
    STDL_Music *m = STDL_LoadMusic(path);
    int n = 0;

    if (m == NULL) {
        printf("FAIL: could not load %s: %s\n", path, STDL_GetError());
        failures++;
        return -1;
    }
    stdl_vbl_hz = (uint8_t)vbl_hz;
    if (STDL_PlayMusic(m, 1) < 0) {
        printf("FAIL: play: %s\n", STDL_GetError());
        failures++;
        STDL_FreeMusic(m);
        return -1;
    }
    while (STDL_PlayingMusic() && n < bound) {
        stdl_music_tick();
        n++;
    }
    STDL_HaltMusic();
    STDL_FreeMusic(m);
    return n;
}

int main(void)
{
    const char *stm = write_stm(50, 100);
    int at50, at60;

    if (stm == NULL) {
        printf("FAIL: could not write the test stream\n");
        return 1;
    }

    /* A 50Hz stream of 100 frames is two seconds of music. On a
     * 50Hz VBL that is 100 ticks; on a 60Hz one it must be 120, or
     * the tune is playing fast. Before this was rate-aware the
     * answer was 100 either way. */
    at50 = ticks_to_finish(stm, 50, 1000);
    at60 = ticks_to_finish(stm, 60, 1000);
    CHECK(at50 == 100, "50Hz stream on a 50Hz VBL took %d ticks, want 100",
          at50);
    CHECK(at60 == 120, "50Hz stream on a 60Hz VBL took %d ticks, want 120",
          at60);

    /* and the same wall-clock length either way, which is the
     * property a listener would notice */
    CHECK(at50 * 1000 / 50 == at60 * 1000 / 60,
          "stream length differs between rates: %dms vs %dms",
          at50 * 1000 / 50, at60 * 1000 / 60);

    /* a 60Hz stream on a 60Hz VBL is one tick per frame again */
    stm = write_stm(60, 90);
    CHECK(ticks_to_finish(stm, 60, 1000) == 90,
          "60Hz stream on a 60Hz VBL did not take 90 ticks");

    /* The API contract. Not initialised, so nothing is written and
     * the effective rate is reported unchanged - a port that calls
     * this before STDL_Init gets an honest answer rather than a
     * silent no-op it believes worked. */
    stdl_vbl_hz = 50;
    stdl.initialised = 0;
    STDL_SYNC_REG = 2;
    CHECK(STDL_SetRefresh(60) == 50, "uninitialised: rate changed");
    CHECK(STDL_SYNC_REG == 2, "uninitialised: register written");

    stdl.initialised = 1;
    stdl.old_sync = -1;
    stdl_sync_want = -1;
    stdl_shutdown_overscan = NULL;
    CHECK(STDL_SetRefresh(60) == 60, "60 not taken");
    CHECK(STDL_SYNC_REG == 0, "60 did not clear the rate bit");
    CHECK(stdl.old_sync == 2, "the rate to restore was not remembered");
    CHECK(STDL_SetRefresh(-1) == 60, "query changed the rate");
    CHECK(STDL_SYNC_REG == 0, "query wrote the register");
    CHECK(STDL_SetRefresh(72) == 60, "a bad rate was accepted");
    CHECK(STDL_SYNC_REG == 0, "a bad rate wrote the register");
    CHECK(STDL_SetRefresh(50) == 50, "50 not taken");
    CHECK(STDL_SYNC_REG == 2, "50 did not set the rate bit");
    CHECK(stdl.old_sync == 2, "the restore value moved on a later call");

    /* While a border is open the overscan module owns the register
     * and forces 50Hz. The request must be recorded rather than
     * written, and the reported rate must be the truth (50), not
     * the wish. */
    stdl_shutdown_overscan = (void (*)(void))main;
    STDL_SYNC_REG = 2;
    stdl_vbl_hz = 50;
    CHECK(STDL_SetRefresh(60) == 50, "60 reported while a border is open");
    CHECK(STDL_SYNC_REG == 2, "the register was written under overscan");
    CHECK(stdl_sync_want == 0, "the request was not recorded for the close");
    stdl_shutdown_overscan = NULL;

    printf("test_refresh: %s\n", failures ? "FAILURES" : "all checks passed");
    return failures ? 1 : 0;
}
