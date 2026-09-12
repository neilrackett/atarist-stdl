/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/*
 * STDL_Tone against the host YM register file, with the real ym.c
 * and sfx.c linked: allocation (last-note priority, a displaced
 * note returning), in-place changes staying on their voice, no
 * writes when nothing changed, an effect stealing a tone's voice
 * and the tone coming back when it ends, a note keyed while its
 * voice is stolen landing at hand-back, and keying everything off
 * leaving the chip silent and unowned.
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

#define TONE_ON(v)  ((stdl_host_ym[7] & (1u << (v))) == 0)

/* VBL queue slots in use - the sound service should claim one */
static int slots_used(void)
{
    int i, n = 0;

    for (i = 0; i < STDL_NVBLS; i++) {
        if (STDL_VBLQUEUE[i] != NULL) {
            n++;
        }
    }
    return n;
}

/* which voice is sounding a period, or -1 */
static int voice_of_period(unsigned period)
{
    int v;

    for (v = 0; v < 3; v++) {
        if (voice_period(v) == period && (stdl_host_ym[8 + v] & 15) != 0) {
            return v;
        }
    }
    return -1;
}

int main(void)
{
    memset((void *)stdl_host_ym, 0, sizeof(stdl_host_ym));
    stdl_host_ym[7] = 0xFF;             /* ports out, all off       */

    /* one note: voice A, ports preserved, noise off */
    CHECK(STDL_ToneOn(0, 500, 12) == 0, "on: %s", STDL_GetError());
    find_vbl();
    CHECK(vbl_fn != NULL, "no VBL slot claimed");
    vbl_fn();
    CHECK(voice_period(0) == 500, "A period %u", voice_period(0));
    CHECK(stdl_host_ym[8] == 12, "A volume %u", stdl_host_ym[8]);
    CHECK(stdl_host_ym[7] == (0xC0 | 0x3E), "mixer %02x",
          stdl_host_ym[7]);
    CHECK(stdl_ym_owned == 0x01, "owned %02x", stdl_ym_owned);
    CHECK(STDL_ToneActive(-1) == 1 && STDL_ToneActive(0),
          "active counts");

    /* two more: B and C; a fourth displaces the oldest (slot 0) */
    STDL_ToneOn(1, 400, 10);
    STDL_ToneOn(2, 300, 8);
    vbl_fn();
    CHECK(voice_period(1) == 400 && voice_period(2) == 300,
          "B %u C %u", voice_period(1), voice_period(2));
    CHECK(stdl_host_ym[7] == (0xC0 | 0x38), "mixer %02x",
          stdl_host_ym[7]);
    STDL_ToneOn(3, 200, 15);
    vbl_fn();
    CHECK(voice_period(0) == 200 && stdl_host_ym[8] == 15,
          "slot 3 should take A: %u/%u", voice_period(0),
          stdl_host_ym[8]);
    CHECK(voice_period(1) == 400 && voice_period(2) == 300,
          "B and C must keep their notes");
    CHECK(stdl_ym_owned == 0x07, "owned %02x", stdl_ym_owned);

    /* the displaced note returns when a voice frees */
    STDL_ToneOff(3);
    vbl_fn();
    CHECK(voice_period(0) == 500 && stdl_host_ym[8] == 12,
          "slot 0 should return to A: %u/%u", voice_period(0),
          stdl_host_ym[8]);

    /* an in-place change stays on its voice, and does not restrike */
    STDL_ToneSet(1, 410, 9);
    vbl_fn();
    CHECK(voice_period(1) == 410 && stdl_host_ym[9] == 9,
          "B after set: %u/%u", voice_period(1), stdl_host_ym[9]);
    STDL_ToneOn(4, 100, 7);             /* newest: displaces slot 0 */
    vbl_fn();
    CHECK(voice_period(0) == 100, "slot 4 should displace slot 0, A=%u",
          voice_period(0));
    STDL_ToneOff(4);
    vbl_fn();
    CHECK(voice_period(0) == 500, "slot 0 back on A: %u",
          voice_period(0));

    /* nothing changed: the tick writes nothing */
    stdl_host_ym[0] = 0xAA;
    stdl_host_ym[8] = 0x03;
    vbl_fn();
    CHECK(stdl_host_ym[0] == 0xAA && stdl_host_ym[8] == 0x03,
          "idle tick wrote registers");
    stdl_host_ym[0] = 500 & 0xFF;
    stdl_host_ym[8] = 12;

    /* the speaker steals A; the tone comes back when it stops */
    STDL_SpeakerOn(1000, 13);
    vbl_fn();
    CHECK(voice_period(0) == 125 && stdl_host_ym[8] == 13,
          "speaker should hold A: %u/%u", voice_period(0),
          stdl_host_ym[8]);
    STDL_ToneSet(0, 520, 11);           /* changed while stolen     */
    vbl_fn();
    CHECK(voice_period(0) == 125, "tone wrote a stolen voice");
    STDL_SpeakerOff();
    vbl_fn();
    CHECK(voice_period(0) == 520 && stdl_host_ym[8] == 11,
          "tone not restored after speaker: %u/%u", voice_period(0),
          stdl_host_ym[8]);
    CHECK(TONE_ON(0), "A tone bit off after restore");
    CHECK(stdl_ym_owned == 0x07, "owned %02x after restore",
          stdl_ym_owned);

    /* a note keyed while a voice is stolen takes a free one and
     * sounds at once, rather than waiting for the hand-back */
    STDL_SpeakerOn(2000, 13);
    vbl_fn();
    STDL_ToneOff(0);
    STDL_ToneOn(5, 250, 6);
    vbl_fn();
    CHECK(voice_period(0) == 62, "speaker should still hold A: %u",
          voice_period(0));
    CHECK(voice_period(1) == 250 && stdl_host_ym[9] == 6,
          "slot 5 should sound on B at once: %u/%u",
          voice_period(1), stdl_host_ym[9]);
    STDL_SpeakerOff();
    vbl_fn();
    CHECK(voice_period(1) == 250,
          "slot 5 should stay on B after hand-back: %u",
          voice_period(1));

    /* a slot keyed off while stolen does not release the voice */
    STDL_SpeakerOn(1000, 13);
    vbl_fn();
    STDL_ToneOff(5);
    vbl_fn();
    CHECK(voice_period(0) == 125 && stdl_host_ym[8] == 13,
          "tone-off must not touch the speaker's voice");
    STDL_SpeakerOff();
    vbl_fn();
    CHECK(stdl_host_ym[8] == 0 && !TONE_ON(0),
          "A should fall silent when nothing is due on it");

    /* everything off: silent and unowned, B and C included */
    STDL_ToneOff(-1);
    vbl_fn();
    CHECK(STDL_ToneActive(-1) == 0, "slots still active");
    CHECK(stdl_host_ym[8] == 0 && stdl_host_ym[9] == 0
          && stdl_host_ym[10] == 0, "voices not silenced");
    CHECK(stdl_host_ym[7] == 0xFF, "mixer %02x", stdl_host_ym[7]);
    CHECK(stdl_ym_owned == 0, "owned %02x", stdl_ym_owned);

    /*
     * A note with no voice left to take must come back when an
     * effect hands one over, with no further slot call: the
     * hand-back has to wake the tick, or the note stays silent
     * until the game happens to key something.
     */
    STDL_ToneOn(6, 600, 12);
    STDL_ToneOn(7, 700, 11);
    vbl_fn();
    STDL_SpeakerOn(1000, 13);           /* takes A; 6 moves to C  */
    vbl_fn();
    STDL_ToneOn(8, 800, 10);            /* newest: displaces 6    */
    vbl_fn();
    CHECK(STDL_ToneActive(6) && voice_of_period(600) < 0,
          "slot 6 should be keyed but voiceless");
    STDL_SpeakerOff();
    vbl_fn();                           /* effects release last,  */
    vbl_fn();                           /* so it lands next frame */
    CHECK(voice_of_period(600) >= 0,
          "slot 6 should return on the freed voice with no slot call");
    STDL_ToneOff(-1);
    vbl_fn();

    /*
     * Opening claims the service with nothing keyed, so a program
     * feeding notes from its own interrupt can get the install out
     * of the way on the main line. A later key must not install
     * anything a second time.
     */
    stdl_shutdown_music();
    stdl_host_conterm = 0x03;
    CHECK(STDL_ToneOpen() == 0, "open failed");
    CHECK(slots_used() == 1, "open should claim one VBL slot");
    CHECK(STDL_ToneActive(-1) == 0, "open should sound nothing");
    CHECK((stdl_host_conterm & 1) == 0, "open should silence the click");
    STDL_ToneOn(0, 500, 12);
    CHECK(slots_used() == 1, "a key after open claimed a second slot");
    vbl_fn();
    CHECK(voice_period(0) == 500, "key after open: %u", voice_period(0));
    STDL_ToneOff(-1);
    vbl_fn();

    /* bad calls fail cleanly */
    CHECK(STDL_ToneOn(16, 100, 1) < 0, "slot 16 accepted");
    CHECK(STDL_ToneOn(0, 0, 1) == 0 && !STDL_ToneActive(0),
          "period 0 should key off");

    stdl_shutdown_music();
    if (failures == 0) {
        printf("test_tone: all checks passed\n");
        return 0;
    }
    printf("test_tone: %d failure(s)\n", failures);
    return 1;
}
