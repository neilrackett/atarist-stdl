/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/*
 * The shared YM2149 service (ym.c) against the host register file.
 *
 * The chip is a 16-entry array on the host, so the test can read
 * back what the service wrote: that installing claims a VBL slot and
 * silences the key click, that every mixer write preserves the
 * port-direction bits TOS relies on, that releasing a voice with no
 * music installed silences it, and that shutdown puts everything
 * back. This is the ground the tone and effects tests stand on.
 */
#include <stdio.h>
#include <string.h>
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

static int slots_used(void)
{
    int i, n = 0;
    for (i = 0; i < 8; i++) {
        if (STDL_VBLQUEUE[i] != NULL) {
            n++;
        }
    }
    return n;
}

int main(void)
{
    /* the chip as TOS leaves it: ports set as outputs, key click on */
    memset((void *)stdl_host_ym, 0, sizeof(stdl_host_ym));
    stdl_host_ym[7] = 0xFF;
    stdl_host_conterm = 0x07;

    CHECK(stdl_ym_install() == 0, "install: %s", STDL_GetError());
    CHECK(slots_used() == 1, "one VBL slot claimed, got %d", slots_used());
    CHECK((stdl_host_conterm & 1) == 0, "key click not silenced");
    CHECK(stdl_ym_install() == 0, "second install must be a no-op");
    CHECK(slots_used() == 1, "second install claimed another slot");

    /* a plain register write lands in the register file */
    stdl_ym_write(0, 0x34);
    stdl_ym_write(1, 0x02);
    CHECK(stdl_host_ym[0] == 0x34 && stdl_host_ym[1] == 0x02,
          "write did not land: %02x %02x", stdl_host_ym[0],
          stdl_host_ym[1]);

    /* the mixer: tone A on, everything else off, ports untouched */
    stdl_ym_mix_update(STDL_YM_VOICE_BITS(0), 0x08);
    CHECK(stdl_host_ym[7] == (0xC0 | 0x3E),
          "mixer %02x: expected ports kept and tone A on",
          stdl_host_ym[7]);
    /* a second update keeps the shadow: tone B on as well */
    stdl_ym_mix_update(STDL_YM_VOICE_BITS(1), 0x10);
    CHECK(stdl_host_ym[7] == (0xC0 | 0x3C),
          "mixer %02x: expected tones A and B on", stdl_host_ym[7]);

    /* releasing a voice with no music installed silences it */
    stdl_ym_owned |= 0x01;
    stdl_ym_write(8, 12);
    stdl_ym_release_voice(0);
    CHECK((stdl_ym_owned & 1) == 0, "voice A still owned after release");
    CHECK(stdl_host_ym[8] == 0, "voice A volume %d after release",
          stdl_host_ym[8]);
    CHECK(stdl_host_ym[7] == (0xC0 | 0x3D),
          "mixer %02x: expected only tone B on", stdl_host_ym[7]);
    stdl_ym_release_voice(3);   /* out of range: ignored */

    /* shutdown: slot freed, voices silent, key click back */
    stdl_ym_write(9, 15);
    stdl_shutdown_music();
    CHECK(slots_used() == 0, "VBL slot not released");
    CHECK(stdl_host_ym[8] == 0 && stdl_host_ym[9] == 0
          && stdl_host_ym[10] == 0, "voices not silenced");
    CHECK(stdl_host_ym[7] == 0xFF, "mixer %02x after shutdown",
          stdl_host_ym[7]);
    CHECK(stdl_host_conterm == 0x07, "conterm %02x not restored",
          stdl_host_conterm);
    CHECK(stdl_ym_owned == 0, "ownership not cleared");

    /* it can be installed again afterwards */
    CHECK(stdl_ym_install() == 0, "reinstall: %s", STDL_GetError());
    CHECK(slots_used() == 1, "reinstall did not claim a slot");
    stdl_shutdown_music();

    if (failures == 0) {
        printf("test_ym: all checks passed\n");
        return 0;
    }
    printf("test_ym: %d failure(s)\n", failures);
    return 1;
}
