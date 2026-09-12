/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: CC0-1.0
 *
 * STDL example program, dedicated to the public domain so it can
 * be used as a starting point without licence concerns.
 */
/*
 * opldemo - an OPL2 register stream played on the YM (STDL_Opl).
 *
 * The shape of an IMF player: a stream of (register, value, delay)
 * triples replayed at a fixed rate (560Hz here, Commander Keen's),
 * each write handed to STDL_OplWrite. The stream below is a small
 * hand-written tune - a bass on channel 0, a melody on channel 1 and
 * a two-note chord on channels 2 and 3 - so the demo needs no data
 * file. Real games hand over their own IMF bytes the same way. The
 * console names each bar; ESC quits.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdl/stdl.h>

#define RATE 560                    /* IMF ticks per second */

typedef struct {
    uint8_t  reg, val;
    uint16_t delay;                 /* ticks before the next write */
} imf_t;

/* F-numbers at block 4: f * 65536 / 49716 */
#define C3 0xAD
#define G3 0x102
#define C4 0x159
#define E4 0x1B3
#define G4 0x205
#define C5 0x2B1

#define ON(ch, fn)  { 0xA0 + (ch), (fn) & 0xFF, 0 }, \
                    { 0xB0 + (ch), 0x20 | (4 << 2) | ((fn) >> 8), 0 }
#define OFF(ch)     { 0xB0 + (ch), (4 << 2), 0 }
#define WAIT(t)     { 0x01, 0x00, (t) }     /* a harmless write */

static const imf_t tune[] = {
    /* carrier levels: bass full, melody a little softer, chord soft */
    { 0x43, 0x00, 0 }, { 0x44, 0x08, 0 }, { 0x45, 0x18, 0 },
    { 0x4B, 0x18, 0 },
    /* bar 1: bass C3, melody C E G C */
    ON(0, C3), ON(1, C4), WAIT(140), ON(1, E4), WAIT(140),
    ON(1, G4), WAIT(140), ON(1, C5), WAIT(140), OFF(1),
    /* bar 2: bass G3, chord E G held over it */
    ON(0, G3), ON(2, E4), ON(3, G4), WAIT(560),
    /* bar 3: bass C3 again and a melody bend down through the chord */
    ON(0, C3), ON(1, C5), WAIT(100),
    { 0xA1, G4 & 0xFF, 0 }, { 0xB1, 0x20 | (4 << 2) | (G4 >> 8), 100 },
    { 0xA1, E4 & 0xFF, 0 }, { 0xB1, 0x20 | (4 << 2) | (E4 >> 8), 100 },
    { 0xA1, C4 & 0xFF, 0 }, { 0xB1, 0x20 | (4 << 2) | (C4 >> 8), 260 },
    OFF(1), OFF(2), OFF(3), WAIT(280), OFF(0),
};

static const char *bars[] = {
    "bar 1: bass with a rising melody",
    "bar 2: a chord over the bass, four notes for three voices",
    "bar 3: the melody steps down through the chord",
};

static void poll(void)
{
    STDL_Event e;

    while (STDL_PollEvent(&e)) {
        if (e.type == STDL_KEYDOWN && e.key.keysym.sym == STDLK_ESCAPE) {
            printf("(escape)\n");
            STDL_OplReset();
            STDL_Quit();
            exit(0);
        }
    }
}

int main(int argc, char *argv[])
{
    static const size_t bar_start[3] = { 0, 19, 26 };
    uint32_t t0, next_tick = 0;
    size_t i = 0;
    int bar = 0;

    (void)argc; (void)argv;
    if (STDL_Init(STDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "init failed: %s\n", STDL_GetError());
        return 1;
    }
    if (STDL_SetVideoMode(320, 200, 4, 0) == NULL) {
        fprintf(stderr, "video failed: %s\n", STDL_GetError());
        return 1;
    }

    t0 = STDL_GetTicks();
    while (i < sizeof(tune) / sizeof(tune[0])) {
        uint32_t tick = (STDL_GetTicks() - t0) * RATE / 1000;

        poll();
        if (tick < next_tick) {
            STDL_Delay(1);
            continue;
        }
        /* every write due by now, then the delay of the last one */
        do {
            if (bar < 3 && i == bar_start[bar]) {
                printf("%s\n", bars[bar++]);
            }
            STDL_OplWrite(tune[i].reg, tune[i].val);
            next_tick += tune[i].delay;
            i++;
        } while (i < sizeof(tune) / sizeof(tune[0])
                 && tune[i - 1].delay == 0);
    }
    STDL_Delay(300);
    printf("done\n");
    STDL_OplReset();
    STDL_Quit();
    return 0;
}
