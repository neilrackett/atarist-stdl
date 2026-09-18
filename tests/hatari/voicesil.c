/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The smallest reproducer for "an open voice device clicks on real
 * hardware with nothing playing". No game, no music, no samples:
 * open the device at the default rate, set no voices, and hold.
 *
 * The ring is 512 frames of silence looped at 6258Hz, which is
 * 81.8ms a pass - 12.2 times a second. A port heard clicking at
 * about that rate on a real STE and silence when the device was
 * never opened, and the emulator records the same configuration as
 * digital silence, so this program exists to take the game out of
 * the question entirely.
 *
 * SPACE cycles three states and ESC quits, so one run answers
 * everything without a build per question:
 *
 *   green   device open, TOS's floppy VBL running
 *   yellow  device open, TOS's floppy VBL locked off
 *   blue    device closed
 *
 * If the clicking stops on the third it is the open device rather
 * than anything else in the machine. If it stops on the second it
 * is TOS's floppy poll.
 *
 * With the ring's guard bytes cleared the continuous 12Hz clicking
 * went and one click every five to ten seconds remained - far too
 * slow to be the loop boundary, which comes round twelve times a
 * second whatever is written near it, and not stable either: it
 * rose within a single session, which no fixed loop period does.
 *
 * TOS's VBL floppy poll was the first hypothesis and is dead -
 * locking it off with $43E made no difference on hardware. The
 * middle state now tests the next one, which splits the space in
 * half rather than probing a branch: play a buffer of silence
 * through STDL_PlaySampleLoop, which points the DMA straight at it
 * and loops it with no ring, no VBL handler and nothing writing to
 * it ever.
 *
 *   silent      the hardware looping zeroes is fine, and the click
 *               comes from the voice device's refill - the CPU
 *               writing the ring while the DMA reads it, or when
 *               it does. That is software, and ours.
 *   still clicks the ring content and the refill are both
 *               innocent; it is the DMA unit or the analogue path
 *               with sound enabled, and the ring's size is then
 *               the right next question.
 *
 * Note what it cannot separate: with no refill there is also no
 * mix_block, so a fault inside the mixer and a fault in writing to
 * the ring at all look the same here. If this comes back silent,
 * the next variant refills with memset only.
 */
#include <stddef.h>
#include <stdl/stdl.h>



int main(int argc, char *argv[])
{
    STDL_Surface *screen;
    int open = 0, state = 0, cycle = 0;

    (void)argc; (void)argv;
    if (STDL_Init(STDL_INIT_VIDEO | STDL_INIT_AUDIO) < 0) {
        return 1;
    }
    screen = STDL_SetVideoMode(320, 200, 4, 0);
    if (screen == NULL) {
        STDL_Quit();
        return 1;
    }
    open = (STDL_OpenVoices(6258) == 0);

    for (;;) {
        STDL_Event ev;
        STDL_Rect r;

        while (STDL_PollEvent(&ev)) {
            if (ev.type == STDL_QUIT) {
                goto done;
            }
            if (ev.type == STDL_KEYDOWN) {
                if (ev.key.keysym.sym == STDLK_ESCAPE) {
                    goto done;
                }
                if (ev.key.keysym.sym == STDLK_SPACE) {
                    /* one key, because only SPACE and ESC were
                     * reaching this program under the test
                     * harness and a state it cannot reach is
                     * worse than a state that needs two presses */
                    state = (state + 1) % 3;
                    cycle = 0;
                    if (state == 2) {
                        if (open) {
                            STDL_CloseVoices();
                            open = 0;
                        }
                    } else {
                        if (!open) {
                            open = (STDL_OpenVoices(6258) == 0);
                        }
                        /* unconditional, per the header: during a
                         * drain this is what cancels the request */
                        STDL_ResumeVoices();
                    }
                }
            }
        }
        /* green when the device is open and silent, blue when it is
         * closed: the only thing on screen, so nothing else is
         * drawing or touching the bus while you listen */
        /* the whole screen is the indicator - one fill, nothing
         * overlaid. A small marker was tried and never appeared,
         * and an indicator you cannot see is worse than none */
        if (state == 1 && open) {
            /* a second either side, so the ear has quiet to judge
             * the transition against rather than a stutter */
            if (++cycle == 50) {
                STDL_PauseVoices();
            } else if (cycle >= 100) {
                STDL_ResumeVoices();
                cycle = 0;
            }
        }
        r.x = 0; r.y = 0; r.w = 320; r.h = 200;
        STDL_FillRect(screen, &r,
                      (uint8_t)(state == 0 ? 10 : (state == 1 ? 14 : 12)));
        STDL_WaitVBL();
    }
done:
    if (open) {
        STDL_CloseVoices();
    }
    STDL_Quit();
    return 0;
}
