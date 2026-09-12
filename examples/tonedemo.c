/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: CC0-1.0
 *
 * STDL example program, dedicated to the public domain so it can
 * be used as a starting point without licence concerns.
 */
/*
 * tonedemo - live notes on the YM's three voices (STDL_Tone).
 *
 * The pattern a port uses when its music arrives as notes at run
 * time (an OPL register stream, a MIDI-note stream): key notes on
 * and off in slots, and let the device place the three newest on
 * the chip. The demo plays a held bass, a melody over it, then a
 * four-note chord to show the oldest note giving way and coming
 * back, then a speaker tone stealing a voice and the note returning
 * when it stops. The console names each step; ESC quits.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdl/stdl.h>

/* a few note frequencies in Hz */
#define C3 131
#define G3 196
#define C4 262
#define E4 330
#define G4 392
#define A4 440
#define C5 523

static void wait_ms(uint32_t ms)
{
    uint32_t until = STDL_GetTicks() + ms;
    STDL_Event e;

    while (STDL_GetTicks() < until) {
        while (STDL_PollEvent(&e)) {
            if (e.type == STDL_KEYDOWN
                && e.key.keysym.sym == STDLK_ESCAPE) {
                printf("(escape)\n");
                STDL_Quit();
                exit(0);
            }
        }
        STDL_Delay(10);
    }
}

static void note(int slot, int hz, uint8_t vol)
{
    if (STDL_ToneOn(slot, STDL_YM_PERIOD(hz), vol) < 0) {
        fprintf(stderr, "tone failed: %s\n", STDL_GetError());
        STDL_Quit();
        exit(1);
    }
}

int main(int argc, char *argv[])
{
    static const int melody[] = { C4, E4, G4, C5, G4, E4 };
    int i;

    (void)argc; (void)argv;
    if (STDL_Init(STDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "init failed: %s\n", STDL_GetError());
        return 1;
    }
    if (STDL_SetVideoMode(320, 200, 4, 0) == NULL) {
        fprintf(stderr, "video failed: %s\n", STDL_GetError());
        return 1;
    }

    printf("bass C3 held on slot 0\n");
    note(0, C3, 11);
    wait_ms(600);

    printf("melody on slot 1 over the bass\n");
    for (i = 0; i < 6; i++) {
        note(1, melody[i], 12);
        wait_ms(250);
    }
    STDL_ToneOff(1);
    wait_ms(300);

    printf("chord C E G on slots 1-3: four notes, the bass gives way\n");
    note(1, C4, 10);
    note(2, E4, 10);
    note(3, G4, 10);
    wait_ms(1200);
    printf("chord off: the bass comes back\n");
    STDL_ToneOff(1);
    STDL_ToneOff(2);
    STDL_ToneOff(3);
    wait_ms(800);

    printf("a pitch bend on the bass (STDL_ToneSet)\n");
    for (i = 0; i < 25; i++) {
        STDL_ToneSet(0, STDL_YM_PERIOD(C3 + i * 2), 11);
        wait_ms(40);
    }
    STDL_ToneSet(0, STDL_YM_PERIOD(C3), 11);
    wait_ms(400);

    printf("speaker A4 steals the bass's voice for a second\n");
    STDL_SpeakerOn(A4, 13);
    wait_ms(1000);
    printf("speaker off: the bass returns\n");
    STDL_SpeakerOff();
    wait_ms(800);

    printf("all off\n");
    STDL_ToneOff(-1);
    wait_ms(200);
    STDL_Quit();
    return 0;
}
