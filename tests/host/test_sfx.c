/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/*
 * STDL_PlaySfx against the host YM register file: the two forms an
 * effect had before v1.12.0 (tone throughout, noise throughout) read
 * exactly as they did, per-step noise sounds tone and noise
 * separately and together on one voice and hands the noise
 * generator back when its steps stop using it, and a step lasts its
 * step_ms at 60Hz as at 50.
 */
#include <stdio.h>
#include <string.h>
#include <stdl/stdl.h>
#include "stdl_internal.h"
#include "ymtest.h"

static int failures;

#define CHECK(cond, ...) do { \
    if (!(cond)) { \
        failures++; \
        printf("FAIL %s:%d: ", __FILE__, __LINE__); \
        printf(__VA_ARGS__); \
        printf("\n"); \
    } \
} while (0)

#define TONE_ON(v)  ((stdl_host_ym[7] & (1u << (v))) == 0)
#define NOISE_ON(v) ((stdl_host_ym[7] & (1u << ((v) + 3))) == 0)

/* ticks from PlaySfx until the effect has ended */
static int ticks_to_end(const STDL_Sfx *fx)
{
    int n = 0;

    STDL_PlaySfx(fx, 2);
    vbl_fn();                           /* starts it on this tick   */
    while (STDL_SfxActive(2) && n < 100) {
        vbl_fn();
        n++;
    }
    return n;
}

int main(void)
{
    static const uint16_t tone_p[2] = { 400, 500 };
    static const STDL_Sfx tone = { tone_p, NULL, 2, 10, 40, 0, NULL };
    static const uint16_t boom_p[3] = { 28, 0, 28 };
    static const STDL_Sfx boom = { boom_p, NULL, 3, 13, 20, 28, NULL };
    static const uint16_t shot_p[4] = { 0, 300, 300, 0 };
    static const uint8_t shot_n[4] = { 5, 12, 0, 0 };
    static const uint8_t shot_v[4] = { 15, 12, 9, 6 };
    static const STDL_Sfx shot = { shot_p, shot_v, 4, 0, 20, 0, shot_n };
    int n;

    memset((void *)stdl_host_ym, 0, sizeof(stdl_host_ym));
    stdl_host_ym[7] = 0xFF;             /* ports out, all off       */

    /* a tone effect, as before: its period on C, tone on, noise off */
    CHECK(STDL_PlaySfx(&tone, -1) == 2, "auto voice should be C");
    find_vbl();
    CHECK(vbl_fn != NULL, "no VBL slot claimed");
    vbl_fn();
    CHECK(voice_period(2) == 400 && stdl_host_ym[10] == 10,
          "tone step 0: %u/%u", voice_period(2), stdl_host_ym[10]);
    CHECK(TONE_ON(2) && !NOISE_ON(2), "tone mixer %02x", stdl_host_ym[7]);
    STDL_StopSfx(-1);
    vbl_fn();

    /* a noise effect, as before: noise only at its fixed period, the
     * generator owned while it sounds, period 0 a rest */
    STDL_PlaySfx(&boom, 2);
    vbl_fn();
    CHECK(stdl_host_ym[6] == 28 && stdl_host_ym[10] == 13,
          "noise step 0: r6 %u vol %u", stdl_host_ym[6], stdl_host_ym[10]);
    CHECK(!TONE_ON(2) && NOISE_ON(2), "noise mixer %02x", stdl_host_ym[7]);
    CHECK(stdl_ym_owned & 0x08, "noise generator not owned");
    vbl_fn();
    CHECK(stdl_host_ym[10] == 0, "period 0 should rest: vol %u",
          stdl_host_ym[10]);
    STDL_StopSfx(-1);
    vbl_fn();

    /* per-step noise: noise alone, then both at a new noise period,
     * then tone alone - the generator handed back - then a rest */
    STDL_PlaySfx(&shot, 2);
    vbl_fn();
    CHECK(stdl_host_ym[6] == 5 && !TONE_ON(2) && NOISE_ON(2)
          && stdl_host_ym[10] == 15,
          "shot 0: r6 %u mixer %02x vol %u", stdl_host_ym[6],
          stdl_host_ym[7], stdl_host_ym[10]);
    vbl_fn();
    CHECK(stdl_host_ym[6] == 12 && TONE_ON(2) && NOISE_ON(2)
          && voice_period(2) == 300 && stdl_host_ym[10] == 12,
          "shot 1: r6 %u mixer %02x period %u vol %u", stdl_host_ym[6],
          stdl_host_ym[7], voice_period(2), stdl_host_ym[10]);
    CHECK(stdl_ym_owned & 0x08, "noise generator not owned at step 1");
    vbl_fn();
    CHECK(TONE_ON(2) && !NOISE_ON(2) && stdl_host_ym[10] == 9,
          "shot 2: mixer %02x vol %u", stdl_host_ym[7], stdl_host_ym[10]);
    CHECK(!(stdl_ym_owned & 0x08), "noise generator kept at step 2");
    vbl_fn();
    CHECK(stdl_host_ym[10] == 0, "shot 3 should rest: vol %u",
          stdl_host_ym[10]);
    vbl_fn();
    CHECK(!STDL_SfxActive(-1), "shot should have ended");
    CHECK(stdl_ym_owned == 0, "owned %02x after the effects",
          stdl_ym_owned);

    /* two 40ms steps are 80ms: four ticks at 50Hz, and at 60Hz five
     * (83ms), not the four that counting 20ms a tick gave */
    n = ticks_to_end(&tone);
    CHECK(n == 4, "50Hz: ended after %d ticks, want 4", n);
    stdl_vbl_hz = 60;
    n = ticks_to_end(&tone);
    CHECK(n == 5, "60Hz: ended after %d ticks, want 5", n);
    stdl_vbl_hz = 50;

    if (failures == 0) {
        printf("test_sfx: all checks passed\n");
    }
    return failures ? 1 : 0;
}
