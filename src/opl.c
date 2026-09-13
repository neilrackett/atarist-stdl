/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_Opl: OPL2 register writes to STDL_Tone notes (see
 * stdl_opl.h). Extracted from the Omnispeak (Commander Keen) port,
 * where it drives the game's own 560Hz IMF replay.
 *
 * Three register groups matter for a square-wave cover: A0-A8, the
 * low byte of a channel's F-number; B0-B8, its high bits with the
 * octave block and the key-on bit; and 40-55, the operators' total
 * level, of which the carrier's is the note's loudness. Everything
 * else sets timbre the YM cannot reproduce and is dropped.
 *
 * The OPL pitch is F-number * 49716 / 2^(20 - block) Hz and the YM
 * period 125000 / Hz; the two divides happen per note event, not
 * per frame. Total level is 0.75dB a step against the YM volume
 * register's roughly 3dB, so four OPL steps lose one YM step. (The
 * YM's 1.5dB figure belongs to its 32-step envelope table, which
 * this does not use - reading it as the step here would halve the
 * dynamic range.)
 */

#include <string.h>
#include "stdl_internal.h"
#include <stdl/stdl_tone.h>
#include <stdl/stdl_opl.h>

#if STDL_OPL_CHANNELS > STDL_TONE_SLOTS
#error "OPL channels map onto tone slots 0-8; there are not enough"
#endif

#define REG_LEVEL   0x40
#define REG_FNUM_LO 0xA0
#define REG_FNUM_HI 0xB0

/* The register state, per channel: what the chip would hold. Whether
 * a channel is sounding is not kept here but asked of the tone device
 * (STDL_ToneActive), so a slot keyed off behind the translator's back
 * - STDL_ToneOff(-1) at a level change, say - is seen as off and the
 * next key-on strikes it again rather than bending a silent slot. */
static uint8_t fnum_lo[STDL_OPL_CHANNELS];
static uint8_t fnum_hi[STDL_OPL_CHANNELS];   /* block, key-on, F-number 9:8 */
static uint8_t level[STDL_OPL_CHANNELS];     /* carrier total level 0-63 */

/*
 * Both halves stay on the 68000's own instructions. fnum is 10 bits
 * and the OPL constant fits a word, so the product is a mulu.w
 * through stdl_row_off rather than a __mulsi3 call; the quotient of
 * 125000/hz is under 65536 for any hz the chip can ask for, so it
 * is a divu.w rather than __udivsi3. Written as plain 32-bit
 * arithmetic the pair costs about 600 cycles on an 8MHz machine,
 * and this runs from a replay interrupt.
 *
 * No clamp at the bottom: fnum <= 1023 and block <= 7 cap the pitch
 * near 6.2kHz, so the period cannot come out zero.
 */
static uint16_t period_of(int fnum, int block)
{
    uint32_t hz = stdl_row_off(fnum, 49716u) >> (20 - block);
    uint32_t p;

    if (hz < 2) {
        return 0;                   /* below the YM's range      */
    }
    p = stdl_divu((uint32_t)STDL_YM_CLOCK, (uint16_t)hz);
    return p > 0x0FFF ? (uint16_t)0x0FFF : (uint16_t)p;
}

/* level is stored masked to 0-63, so this is always 0-15 */
static uint8_t volume_of(uint8_t total_level)
{
    return (uint8_t)(15 - (total_level >> 2));
}

/* The channel whose carrier an operator offset (0x00-0x15) belongs
 * to, or -1 for a modulator or a gap: operators come in rows of
 * eight, three modulators then three carriers then two unused. */
static int carrier_channel(int op)
{
    int row = op >> 3, col = op & 7;

    if (col < 3 || col > 5) {
        return -1;
    }
    return row * 3 + (col - 3);
}

static void update(int ch)
{
    uint8_t hi = fnum_hi[ch];
    uint8_t vol;
    uint16_t period;

    /*
     * Key-off first, and the cheap tests before the pitch: half of
     * all note events are key-offs, and computing a period for one
     * is a multiply and a divide thrown away. Volume 0 is silence
     * on the YM, and a silent channel that took a slot would evict
     * an audible one from the three voices, so a level of 60 or
     * more is a key-off here and a later level rise keys it on
     * again.
     */
    vol = volume_of(level[ch]);
    if ((hi & 0x20) == 0 || vol == 0) {
        STDL_ToneOff(ch);
        return;
    }
    period = period_of(((hi & 3) << 8) | fnum_lo[ch], (hi >> 2) & 7);
    if (period == 0) {
        STDL_ToneOff(ch);
    } else if (!STDL_ToneActive(ch)) {
        STDL_ToneOn(ch, period, vol);
    } else {
        STDL_ToneSet(ch, period, vol);   /* bend or level change  */
    }
}

/*
 * Whether the game has this channel keyed on, from the B0 byte this
 * module keeps - its own state, not a shadow of the tone device's.
 * The device is still asked the on-versus-set question, so a slot
 * keyed off behind our back is seen as off and restruck; but a
 * channel the game holds down stays ours to update, including one
 * this module keyed off because its level said silence.
 */
static int keyed(int ch)
{
    return (fnum_hi[ch] & 0x20) != 0;
}

/* whether the tone device and this module agree about the channel */
static int in_sync(int ch)
{
    return !STDL_ToneActive(ch) == !keyed(ch);
}

void STDL_OplWrite(uint8_t reg, uint8_t val)
{
    /*
     * A write that changes nothing is dropped, but only while the
     * tone device still agrees with this module about whether the
     * channel is sounding. IMF streams re-send state constantly -
     * the same key-off byte, an unchanged F-number under a
     * sustained note - and acting on those costs a multiply, a
     * divide and a slot call inside the replay interrupt to reach
     * the state already held. When the two disagree, because
     * something keyed the slot off behind this module's back, an
     * identical byte has to be acted on: that is what makes the
     * next key-on restrike instead of bending a silent slot.
     */
    if (reg >= REG_FNUM_LO && reg < REG_FNUM_LO + STDL_OPL_CHANNELS) {
        int ch = reg - REG_FNUM_LO;

        if (fnum_lo[ch] == val && in_sync(ch)) {
            return;
        }
        fnum_lo[ch] = val;
        if (keyed(ch)) {
            update(ch);
        }
    } else if (reg >= REG_FNUM_HI
               && reg < REG_FNUM_HI + STDL_OPL_CHANNELS) {
        int ch = reg - REG_FNUM_HI;

        if (fnum_hi[ch] == val && in_sync(ch)) {
            return;
        }
        fnum_hi[ch] = val;
        update(ch);
    } else if (reg >= REG_LEVEL && reg < REG_LEVEL + 0x16) {
        int ch = carrier_channel(reg - REG_LEVEL);

        if (ch >= 0) {
            uint8_t lv = (uint8_t)(val & 0x3F);

            if (level[ch] == lv && in_sync(ch)) {
                return;
            }
            level[ch] = lv;
            if (keyed(ch)) {
                update(ch);
            }
        }
    }
}

void STDL_OplReset(void)
{
    int ch;

    /* From the main line, so an IMF replay fed from the program's
     * own timer never installs the sound service inside the
     * interrupt. */
    (void)STDL_ToneOpen();
    for (ch = 0; ch < STDL_OPL_CHANNELS; ch++) {
        STDL_ToneOff(ch);
    }
    memset(fnum_lo, 0, sizeof(fnum_lo));
    memset(fnum_hi, 0, sizeof(fnum_hi));
    memset(level, 0, sizeof(level));
}
