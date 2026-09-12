/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/*
 * STDL_Opl against the host YM register file, with the real tone.c,
 * ym.c and sfx.c linked: a key-on landing at the OPL pitch and the
 * carrier's volume, modulator levels ignored, a level or F-number
 * change while keyed bending in place, a rewritten key-on not
 * restriking, operator offsets mapping to the right channel, key-off
 * freeing the voice, and reset keying everything off while leaving
 * the program's own slots alone.
 */
#include <stdio.h>
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

static void (*vbl_fn)(void);

static void find_vbl(void)
{
    int i;
    vbl_fn = NULL;
    for (i = 0; i < 8; i++) {
        if (STDL_VBLQUEUE[i] != NULL) {
            vbl_fn = STDL_VBLQUEUE[i];
        }
    }
}

static uint16_t voice_period(int v)
{
    return (uint16_t)(stdl_host_ym[2 * v]
                      | ((stdl_host_ym[2 * v + 1] & 0x0F) << 8));
}

/* key channel ch at block 4 with F-number fnum */
static void key_on(int ch, int fnum)
{
    STDL_OplWrite((uint8_t)(0xA0 + ch), (uint8_t)(fnum & 0xFF));
    STDL_OplWrite((uint8_t)(0xB0 + ch),
                  (uint8_t)(0x20 | (4 << 2) | ((fnum >> 8) & 3)));
}

int main(void)
{
    memset((void *)stdl_host_ym, 0, sizeof(stdl_host_ym));
    stdl_host_ym[7] = 0xFF;

    /* A4: F-number 0x244 at block 4 is 440Hz, period 284; the
     * carrier level 8 is volume 13 */
    STDL_OplWrite(0x43, 0x08);
    key_on(0, 0x244);
    find_vbl();
    CHECK(vbl_fn != NULL, "no VBL slot claimed");
    vbl_fn();
    CHECK(voice_period(0) == 284, "A period %u", voice_period(0));
    CHECK(stdl_host_ym[8] == 13, "A volume %u", stdl_host_ym[8]);
    CHECK(STDL_ToneActive(0) == 1, "slot 0 not keyed");

    /* the modulator's level is not the note's volume */
    STDL_OplWrite(0x40, 0x3F);
    vbl_fn();
    CHECK(stdl_host_ym[8] == 13, "modulator level changed volume");

    /* a level change while keyed: in place, same voice */
    STDL_OplWrite(0x43, 0x20);
    vbl_fn();
    CHECK(stdl_host_ym[8] == 7 && voice_period(0) == 284,
          "level change: %u/%u", voice_period(0), stdl_host_ym[8]);

    /* an F-number change while keyed bends: 0x260 is 461Hz, 271 */
    STDL_OplWrite(0xA0, 0x60);
    vbl_fn();
    CHECK(voice_period(0) == 271, "bend: A period %u", voice_period(0));

    /* rewriting the key-on register does not restrike: channel 0
     * stays the oldest and is the one a fourth note displaces */
    key_on(1, 0x244);
    key_on(2, 0x244);
    vbl_fn();
    STDL_OplWrite(0xB0, 0x20 | (4 << 2) | 2);
    key_on(3, 0x159);                   /* 345 * 49716 >> 16 = 261Hz */
    vbl_fn();
    CHECK(voice_period(0) == 478, "channel 3 should take A: %u",
          voice_period(0));
    CHECK(voice_period(1) == 284 && voice_period(2) == 284,
          "channels 1 and 2 should keep their voices");
    CHECK(STDL_ToneActive(0), "channel 0 should still be keyed");

    /* key-off frees a voice and the displaced note returns to it */
    STDL_OplWrite(0xB1, (4 << 2) | 2);
    vbl_fn();
    CHECK(!STDL_ToneActive(1), "channel 1 still keyed");
    CHECK(voice_period(1) == 271, "channel 0 should return on B: %u",
          voice_period(1));

    /* a carrier level of 60 is silence on the YM, so the channel
     * gives its voice up rather than holding one at volume 0 and
     * evicting something audible (0x4B is channel 3's carrier) */
    STDL_OplWrite(0x4B, 0x3C);          /* channel 3: volume 0       */
    vbl_fn();
    CHECK(!STDL_ToneActive(3), "channel 3 should be off at level 60");
    CHECK(voice_period(1) == 271 && voice_period(2) == 284,
          "channels 0 and 2 must keep their voices");

    /* operator offsets: 0x53 is channel 6's carrier */
    STDL_OplWrite(0x53, 0x04);          /* channel 6: volume 14      */
    key_on(6, 0x244);
    vbl_fn();
    CHECK(voice_period(0) == 284 && stdl_host_ym[8] == 14,
          "channel 6 should take the freed voice: %u/%u",
          voice_period(0), stdl_host_ym[8]);

    /* and a level rising off silence brings the channel back, since
     * the game never keyed it off */
    STDL_OplWrite(0x4B, 0x08);          /* channel 3: volume 13      */
    vbl_fn();
    CHECK(STDL_ToneActive(3),
          "channel 3 should return when its level rises");

    /* rhythm and timbre registers are ignored */
    STDL_OplWrite(0xBD, 0x3F);
    STDL_OplWrite(0x20, 0x21);
    STDL_OplWrite(0xE0, 0x03);
    vbl_fn();
    CHECK(STDL_ToneActive(-1) == 4, "keyed %d, expected 4",
          STDL_ToneActive(-1));

    /* a slot keyed off behind the translator (the program's own
     * ToneOff at a level change) is seen: the next key-on restrikes */
    STDL_ToneOff(-1);
    vbl_fn();
    STDL_OplWrite(0xB0, 0x20 | (4 << 2) | 2);   /* same key-on again */
    vbl_fn();
    CHECK(STDL_ToneActive(0) && voice_period(0) == 271,
          "key-on after a foreign ToneOff should restrike: %u",
          voice_period(0));
    key_on(1, 0x244);
    key_on(6, 0x244);
    vbl_fn();

    /* reset keys the channels off but leaves the program's slot */
    STDL_ToneOn(9, 125, 12);
    STDL_OplReset();
    vbl_fn();
    CHECK(STDL_ToneActive(-1) == 1 && STDL_ToneActive(9),
          "reset should leave only slot 9 keyed");
    key_on(0, 0x244);                   /* state cleared: level 0    */
    vbl_fn();
    CHECK(STDL_ToneActive(0) && STDL_ToneActive(-1) == 2,
          "channel 0 after reset");
    STDL_ToneOff(-1);
    vbl_fn();
    CHECK(stdl_host_ym[7] == 0xFF, "mixer %02x", stdl_host_ym[7]);

    stdl_shutdown_music();
    if (failures == 0) {
        printf("test_opl: all checks passed\n");
        return 0;
    }
    printf("test_opl: %d failure(s)\n", failures);
    return 1;
}
