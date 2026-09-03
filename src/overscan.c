/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_Overscan: border overscan. Top: 228 seamless visible lines.
 * Bottom: 245 seamless visible lines. Both: 273.
 *
 * The GLUE decides between border and picture per scanline by
 * looking at the sync rate. Top: during scanlines 25-33 a 60Hz
 * screen starts its picture at line 34 where a 50Hz one waits for
 * line 63 - show it 60Hz inside that nine-line window, restore
 * 50Hz once the first line has been fetched, and 229 rows fetch,
 * the first (the one fetched at 60Hz while the restore is pinned)
 * showing border colour.
 * Bottom: flick to 60Hz across the border test at the end of
 * picture line 200 and the border never turns on - lines continue
 * to 244.
 *
 * The bottom flick has to fit a window the top one does not. A
 * line whose display starts while the sync rate is 60Hz starts
 * four cycles early and fetches its planes one word out of step
 * (the "+2/-2 line" of the overscan literature), which is what an
 * earlier version of this code showed as a seam at picture line
 * 200: it left 60Hz on until the next line had ended. The GLUE
 * tests for the bottom border at cycle ~502 of frame line 262 and
 * for the start of a 60Hz line at cycle 52 of the next, so 60Hz
 * has to be on across 502 and off again by 564 (line 263's cycle
 * 52) - a 62-cycle window, ~7.7us, which an interrupt cannot hit
 * at every CPU speed and which cycle-counted code hits only on the
 * machine it was counted for.
 *
 * What hits it here is the Shifter's video counter. During the
 * fetch of a line ($ff8209 increments by two every four cycles) it
 * is a position sensor readable to four cycles, and it parks at the
 * next line's start address in between - the ISR reads it to know
 * both which line it is on and where the beam is. From the moment
 * line 262's fetch passes byte 130 (cycle ~326) a short dbra loop
 * runs out the rest of the distance; the counter value picked up by
 * that read selects the loop count from a small table, so the poll
 * loop's own period is not an error. The loop counts come from a
 * calibration at open time that times 7680 iterations of the same
 * dbra against displayed scanlines (Timer B counting Display Enable
 * ends), i.e. against the clock the GLUE itself runs on - a plain
 * ST measures ten cycles a turn, a 16MHz Mega STE five, an
 * accelerator whatever it is. Fixed costs between the read and the
 * writes are the 68000's own instruction times scaled by the same
 * ratio, with the bus cycles of the register writes held at their
 * 8MHz width. The 60Hz write lands at cycle ~456, the 50Hz one at
 * ~533: about thirty cycles clear of each edge of the window, which
 * is what the calibration's residual (under one percent of a
 * 200-cycle delay), the counter's four-cycle grain and a couple of
 * cycles of bus phase add up to with room to spare.
 *
 * A cached CPU (the Mega STE) would run that path cold once a
 * frame, and the difference between cold and warm instruction
 * fetches is bigger than the window; the ISR therefore runs the
 * path once against a scratch byte before it polls, which is the
 * classic demo answer and costs ~250 cycles a frame.
 *
 * Nothing else is cycle-counted: Timer A in delay mode hits the top
 * window from the VBL at any CPU speed because it runs off the
 * MFP's own clock, Timer B counting Display Enable events fires on
 * an exact line for the bottom, and every sub-line wait is pinned
 * to hardware events (bounded polls). Cost is one interrupt plus
 * two to three lines of polling per frame for each border, under
 * 1% of an 8MHz frame each - nearly all of it spent waiting on the
 * beam.
 *
 * Cooperative by design, where the classic demo versions of these
 * tricks own the machine: the VBL hook is a register-free prefix
 * on the $70 autovector that forces 50Hz (the safety net - a
 * missed window shows one normal border and recovers), arms the
 * timer and falls through to the original handler, so the TOS VBL
 * queue, screenpt handling and STDL's sound tick keep running; the
 * ISRs end with a software EOI and the only MFP resources claimed
 * are the one timer slot (top: Timer A, vector $134, IERA/IMRA bit
 * 5, plus Timer B's counter with its interrupt left masked;
 * bottom: Timer B, vector $120, IERA/IMRA bit 0).
 *
 * One more vector is borrowed while a border is open: TOS's 200Hz
 * Timer C handler ($114) gets a register-free prefix that lowers
 * the CPU mask to 5 before falling through to it. TOS runs that
 * handler at mask 6 for over two scanlines at a time (EmuTOS
 * measured at ~1140 cycles, once per frame when its fourth-tick
 * work runs), and its phase drifts across the frame by 165 cycles
 * a frame, so any interrupt that has to land on a given line loses
 * a run of frames every twenty seconds - unless it fires two or
 * three lines early and spins, which is what earlier versions did.
 * At mask 5 the MFP delivers the channels above Timer C (Timer A
 * is 13, Timer B 8, the ACIA 6) into the handler instead, so each
 * ISR fires on the line it wants and spends nothing waiting for it.
 * The handler itself does not mind: our ISRs touch only their own
 * state, and under STDL the keyboard path TOS's key-repeat code
 * shares with the ACIA is idle, ikbdsys being ours.
 *
 * Each ISR still guards against being late (a long interrupts-off
 * section, a keyboard byte's handler in progress, or a BLiTTER hog
 * blit - though blits started while a border is open run in shared
 * mode for exactly this reason, see stdl_no_hog in blitter.c).
 * The top ISR reads the Shifter's video counter, and if fetching
 * has started the window is gone, so it skips the sync flip
 * entirely rather than glitch mid-frame, counts the miss and shows
 * one normal-bordered frame. The bottom ISR's guard is the same
 * register: it must read line 262's start address when the ISR
 * gets there, and the flick is skipped and counted when it does
 * not, when the counter's phase was lost to a stall, or when line
 * 263 then fails to display.
 *
 * While an ISR runs (including the bounded polls) its in-service
 * bit blocks lower-priority MFP interrupts - the ACIA/IKBD among
 * them - for ~130us once per frame. Harmless, but if a keyboard
 * byte ever drops under stress, look here before suspecting the
 * border code.
 */

#include <stdlib.h>
#include <string.h>
#include "stdl_internal.h"
#include <stdl/stdl_overscan.h>
#include <stdl/stdl_vbl.h>

#ifdef __m68k__

#include <mint/osbind.h>

/* top: 229 rows fetched (frame lines 34-262), the first shows
 * border colour, visible picture is rows 1..228.
 * bottom: 245 rows fetched and visible (picture lines 0-244).
 * both: 274 rows fetched (frame lines 34-307); row 0 hidden,
 * rows 1-273 display - the surface exposes rows 1..273. */
#define TOP_HIDDEN_ROWS   1
#define BOTH_FETCH_ROWS   274
#define MAX_FETCH_BYTES   (BOTH_FETCH_ROWS * STDL_SCREEN_STRIDE)

#define MODE_TOP 1
#define MODE_BOT 2

/* Bottom flick placement, in cycles from the start of frame line
 * 262 as the GLUE counts them (512 per 50Hz line). The line's
 * fetch runs 56..376; the GLUE samples the sync rate for its
 * bottom-border test at ~502 (STF) / ~500 (STE) and for a 60Hz
 * display start at cycle 52 of line 263, i.e. 564. */
#define OVSC_HIT_READ   326     /* the read that first sees fetch
                                 * byte 130 lands here, +4 per table
                                 * slot (two bytes), mid-grain      */
#define OVSC_PARK_READ  384     /* byte 160 (parked) shows from here */
#define OVSC_TICK_READ  400     /* Timer B's count steps here       */
#define OVSC_T1         456     /* 60Hz write: inside 377..500      */
#define OVSC_T2         533     /* 50Hz write: inside 503..564      */
/* Fixed costs, x16: from the poll's last read to the 60Hz write in
 * counter mode (TB) and tick mode (TT), between the writes (TM),
 * and half a poll period for the reads that catch an edge rather
 * than a value (PARK: the counter's parking, TICK: Timer B's step).
 * Each has a CPU part in plain-ST cycles, scaled by the measured
 * turn / 192, and a bus part the register accesses spend at 8MHz
 * whatever the CPU does. Measured in Hatari with zeroed tables at
 * 8 and 16MHz, see the ISR comment. */
#define OVSC_TB_CPU     (68 * 16)
#define OVSC_TB_BUS     (8 * 16)
#define OVSC_TT_CPU     (29 * 16)
#define OVSC_TT_BUS     (3 * 16)
#define OVSC_TM_CPU     (26 * 16)
#define OVSC_TM_BUS     (2 * 16)
#define OVSC_PARK_CPU   (15 * 16)
#define OVSC_PARK_BUS   (1 * 16)
#define OVSC_TICK_CPU   (15 * 16)
#define OVSC_TICK_BUS   (1 * 16)
/* a loop's first turn follows the table read in bus phase and
 * costs four cycles less than the rest; the first loop only, the
 * second's count load is a register move */
#define OVSC_FIRST_TURN (4 * 16)
#define OVSC_SPIN       7680    /* calibration turns: 150 lines on
                                 * a plain ST, the slowest case, so
                                 * it always ends inside the picture */
#define OVSC_C16_8MHZ   192     /* dbra turn on a plain ST, x16: 10
                                 * cycles rounded up to the 4-cycle
                                 * bus slot                         */
/* the stopwatch: Timer B in delay mode at 26us a tick from 255,
 * started at ~34/500 by the top ISR (top only) or at ~262/560 by
 * the bottom ISR (combined), read by a VBL prefix that ran on
 * time. Measured in Hatari at 83 and 135 (the same on ST, STE and
 * Mega STE - it is MFP time, not CPU cycles), and set one higher
 * so a tick of start phase never reads as early and disables the
 * correction: an on-time frame then arms Timer A a tick early at
 * most, a third of a line of extra spin. */
#define OVSC_TBREF_TOP  84
#define OVSC_TBREF_BOTH 136

uint32_t stdl_ovsc_old70;    /* original $70 vector, read by asm  */
uint32_t stdl_ovsc_oldtc;    /* original $114 vector, read by asm */
uint8_t  stdl_ovsc_tbref;    /* Timer B stopwatch reading the VBL
                              * prefix expects when it ran on time */
uint8_t  stdl_ovsc_tbarm;    /* what the top ISR arms Timer B with:
                              * a DE count for the bottom ISR, or 0
                              * for the stopwatch                  */
uint8_t  stdl_ovsc_tbseen;   /* last stopwatch reading (diagnostic) */
uint8_t  stdl_ovsc_tacnt;    /* the count the prefix armed Timer A
                              * with, which its ISR's lateness guard
                              * is relative to                    */
uint8_t  stdl_ovsc_bhi;      /* screen base bytes the top ISR      */
uint8_t  stdl_ovsc_bmid;     /* compares the video counter against */
uint32_t stdl_ovsc_missed;   /* frames whose flip was skipped      */

/* bottom ISR data: dbra turns before the 60Hz write, indexed by
 * the fetch byte the poll caught line 262 at (slot 0 = byte 130,
 * one slot per two bytes, slot 15 = parked at 160); turns between
 * the writes; the address bytes of frame line 262's first word,
 * which the counter shows while parked after line 261; and the
 * byte the warm-up pass writes to */
uint16_t stdl_ovsc_n1[16];
uint16_t stdl_ovsc_n2;
uint16_t stdl_ovsc_n2t;      /* tick mode: turns between the writes */
uint16_t stdl_ovsc_postn;    /* poll bound for line 263's DE end     */
uint16_t stdl_ovsc_waitn;    /* poll bound of ~8 lines, any wait     */
uint8_t  stdl_ovsc_tick;     /* 1 = time from Timer B, not the counter */
uint8_t  stdl_ovsc_l262lo;
uint8_t  stdl_ovsc_l262mid;
uint8_t  stdl_ovsc_scratch;

static void *buf_alloc;
static uint8_t *buf;
static uint32_t old_ta_vec;      /* original $134 and $120 vectors */
static uint32_t old_tb_vec;
static uint32_t old_tc_vec;      /* original $114 (Timer C) vector   */
static uint8_t  old_sync;        /* sync rate before the first open */
static uint8_t  old_ier;         /* previous IERA/IMRA state of both */
static uint8_t  old_imr;         /* timer bits (0x21 mask)           */
static int      mode;            /* MODE_TOP | MODE_BOT */
static int      c16;             /* calibrated dbra turn, x16, or 0 */

#define MFP_IERA (*(volatile uint8_t *)0xFFFFFA07UL)
#define MFP_IPRA (*(volatile uint8_t *)0xFFFFFA0BUL)
#define MFP_ISRA (*(volatile uint8_t *)0xFFFFFA0FUL)
#define MFP_IMRA (*(volatile uint8_t *)0xFFFFFA13UL)
#define MFP_TACR (*(volatile uint8_t *)0xFFFFFA19UL)
#define MFP_TBCR (*(volatile uint8_t *)0xFFFFFA1BUL)
#define MFP_TBDR (*(volatile uint8_t *)0xFFFFFA21UL)
#define SYNC_REG (*(volatile uint8_t *)0xFFFF820AUL)
#define VEC_VBL  (*(volatile uint32_t *)0x70UL)
#define VEC_TA   (*(volatile uint32_t *)0x134UL)
#define VEC_TB   (*(volatile uint32_t *)0x120UL)
#define VEC_TC   (*(volatile uint32_t *)0x114UL)

__asm__(
"    .text\n"
"    .even\n"

/* -- top border (and combined) ------------------------------------ */
/* VBL prefix: 50Hz safety, then Timer A armed to fire at line
 * ~32.2 - 100 ticks of 20.3us from a VBL taken on time, fewer
 * from one taken late. The VBL is level 4 and TOS's Timer C
 * handler, even opened up (see the prefix below), still runs above
 * it for over two lines at a time, so a few frames in every
 * thousand see this prefix up to two lines late, which a fixed
 * count would pass straight on to Timer A. The ISR that closed the
 * previous frame left Timer B running as a stopwatch (delay mode,
 * 26us ticks), and its reading against the on-time value says how
 * late: each late tick is 1.28 of Timer A's, so the count drops by
 * the lateness plus a quarter. Readings out of range (the previous
 * frame skipped, the first frame after opening) count as on time.
 * Both timers are then stopped and Timer A restarted - with the
 * mask at 7 from the reading to the restart, or Timer C could
 * still slip in between them and delay the restart by its whole
 * handler, uncompensated; the VBL's own level 4 is put back for
 * TOS's handler. */
"_stdl_ovsc_vbl_top:\n"
"    move.w #0x2700,%sr\n"
"    move.b #2,0xffff820a.w\n"
"    movem.l %d0-%d1,-(%sp)\n"
"    move.b 0xfffffa21.w,%d1\n"
"    move.b %d1,_stdl_ovsc_tbseen\n"
"    move.b _stdl_ovsc_tbref,%d0\n"
"    sub.b  %d1,%d0\n"
"    bmi.s  1f\n"
"    cmp.b  #12,%d0\n"
"    bls.s  2f\n"
"1:  moveq  #0,%d0\n"
"2:  move.b %d0,%d1\n"
"    lsr.b  #2,%d1\n"
"    add.b  %d1,%d0\n"
"    moveq  #100,%d1\n"
"    sub.b  %d0,%d1\n"
"    move.b %d1,_stdl_ovsc_tacnt\n"
"    clr.b  0xfffffa19.w\n"
"    clr.b  0xfffffa1b.w\n"
"    move.b %d1,0xfffffa1f.w\n"
"    move.b #4,0xfffffa19.w\n"
"    movem.l (%sp)+,%d0-%d1\n"
"    move.w #0x2400,%sr\n"
"    move.l _stdl_ovsc_old70,-(%sp)\n"
"    rts\n"
"\n"
/* Timer A ISR, serving top-only and combined modes alike. Two
 * lateness guards decide whether the window is still open before
 * anything touches the sync rate:
 *
 * 1. The timer itself is the clock. In delay mode it reloads and
 *    keeps counting after firing, so its data register reads as
 *    the armed count minus the ticks since the scheduled fire at
 *    line ~32.2 - a delivery delayed past what the window can
 *    absorb (a blit that could not be interrupted, an
 *    interrupts-off stretch) shows as a low count. 4 ticks = a
 *    line and a quarter of grace keeps the 60Hz write ahead of the
 *    test at line 33. The count is whatever the prefix armed,
 *    fewer than 100 after a late VBL, so the guard is relative.
 * 2. The video counter must still sit at the base - if fetching
 *    has started the frame is displaying, and a sync flip now
 *    would glitch it. This also catches a delivery so late the
 *    timer count has wrapped back into range.
 *
 * On time, 60Hz starts the picture at line 34 and Timer B, silent
 * in event-count mode, pins the 50Hz restore into the blanking gap
 * after line 34 - anywhere between that line's display end and
 * line 35's start at 60Hz, a 184-cycle window the poll cannot
 * miss, so line 34 is the only row fetched at 60Hz timing and the
 * surface starts at row 1. Timer B is then re-armed: in the
 * combined mode to count DE ends up to the end of frame line 260,
 * which chains into the bottom ISR; top-only, as the stopwatch the
 * VBL prefix reads. A late delivery or a timed-out pin skips the
 * flip and counts the miss; the top border is then closed for the
 * frame, so in the combined mode Timer B is armed to count DE ends
 * from line 63 instead (198 of them reach line 260, and the
 * bottom still opens), and top-only it is left cleared so the next
 * prefix reads no stopwatch and arms Timer A plainly. */
"    .even\n"
"_stdl_ovsc_ta:\n"
"    movem.l %d0-%d2/%a0,-(%sp)\n"
"    move.b 0xfffffa1f.w,%d0\n"
"    clr.b  0xfffffa19.w\n"
"    move.b _stdl_ovsc_tacnt,%d1\n"
"    subq.b #4,%d1\n"
"    cmp.b  %d1,%d0\n"
"    blo.s  ovsc_ta_late\n"
"    move.b 0xffff8205.w,%d0\n"
"    cmp.b  _stdl_ovsc_bhi,%d0\n"
"    bne.s  ovsc_ta_late\n"
"    move.b 0xffff8207.w,%d0\n"
"    cmp.b  _stdl_ovsc_bmid,%d0\n"
"    bne.s  ovsc_ta_late\n"
"    tst.b  0xffff8209.w\n"
"    bne.s  ovsc_ta_late\n"
"    clr.b  0xffff820a.w\n"
"    lea    0xfffffa21.w,%a0\n"
"    clr.b  0xfffffa1b.w\n"
"    move.b #200,(%a0)\n"
"    move.b #8,0xfffffa1b.w\n"
"    bsr    stdl_ovsc_wait_de\n"
"    bmi.s  ovsc_ta_fail\n"
"    move.b #2,0xffff820a.w\n"
"    clr.b  0xfffffa1b.w\n"
"    move.b _stdl_ovsc_tbarm,%d1\n"
"    beq.s  1f\n"
"    move.b %d1,(%a0)\n"
"    move.b #8,0xfffffa1b.w\n"
"    bra.s  ovsc_ta_out\n"
"1:  move.b #255,(%a0)\n"
"    move.b #5,0xfffffa1b.w\n"
"ovsc_ta_out:\n"
"    move.b #0xDF,0xfffffa0f.w\n"
"    movem.l (%sp)+,%d0-%d2/%a0\n"
"    rte\n"
"ovsc_ta_fail:\n"
"    move.b #2,0xffff820a.w\n"
"ovsc_ta_late:\n"
"    addq.l #1,_stdl_ovsc_missed\n"
"    clr.b  0xfffffa1b.w\n"
"    clr.b  0xfffffa21.w\n"
"    tst.b  _stdl_ovsc_tbarm\n"
"    beq.s  ovsc_ta_out\n"
"    move.b #198,0xfffffa21.w\n"
"    move.b #8,0xfffffa1b.w\n"
"    bra.s  ovsc_ta_out\n"
"\n"

/* -- Timer C prefix ------------------------------------------------ */
/* Lower the CPU mask inside TOS's 200Hz handler so the border
 * timers (MFP channels 13 and 8, above Timer C's 5) are taken while
 * it runs. The handler's own rte restores the interrupted mask.
 * Touches no registers. */
"    .even\n"
"_stdl_ovsc_tc:\n"
"    move.w #0x2500,%sr\n"
"    move.l _stdl_ovsc_oldtc,-(%sp)\n"
"    rts\n"
"\n"

/* -- bottom border ------------------------------------------------- */
/* VBL prefix: 50Hz safety, re-arm Timer B to fire after 198
 * displayed lines - two lines before the end of the picture. */
"    .even\n"
"_stdl_ovsc_vbl_bot:\n"
"    move.b #2,0xffff820a.w\n"
"    clr.b  0xfffffa1b.w\n"
"    move.b #198,0xfffffa21.w\n"
"    move.b #8,0xfffffa1b.w\n"
"    move.l _stdl_ovsc_old70,-(%sp)\n"
"    rts\n"
"\n"
/* Timer B ISR, at the end of picture line 197 (frame line 260).
 * That is a line earlier than it needs to be: with Timer C's
 * handler opened up (see the prefix above) the interrupt is taken
 * within a long instruction or a keyboard byte's handler, a few
 * hundred cycles at most, and from line 260 the ISR can be more
 * than a line late and still find the counter parked at line 262's
 * start address, which is what it polls for first (both bytes: the
 * low one alone recurs a line earlier). Not finding it within the
 * bound means the ISR is later still, and the frame is skipped.
 * Then the flick path runs once against a scratch byte (the
 * warm-up: a cached CPU fetches it cold otherwise, and
 * cold-versus-warm is wider than the window), and the polls
 * begin.
 *
 * Counter mode: wait for the counter to leave its parked value,
 * then for line 262's fetch to pass byte 130 - the cheapest loop
 * there is, the counter minus base-plus-2 goes negative exactly
 * there - and hand the byte offset to the flick as the table
 * index. Byte 160 is the counter parked again at the end of the
 * line: a stall ate the moving phase, and that slot of the table
 * is timed from the parking edge instead.
 *
 * Tick mode (the open-time check found the counter does not move
 * mid-line, which no ST does but an emulator's 16MHz mode may):
 * wait for line 262's own Display Enable end on Timer B and time
 * both writes from that read, at the cost of the poll period as
 * jitter - still inside the window, with less to spare.
 *
 * Afterwards line 263 has to end with a DE event, or the border
 * did not open and the frame is counted; Timer B is then restarted
 * as the stopwatch the top's VBL prefix reads (harmless in
 * bottom-only mode, whose prefix re-arms it anyway).
 *
 * From the poll's last read to the writes the paths are
 * sub/bmi/and/move/dbra-exit/clr, bne/clr, and move/dbra-exit/move
 * between the writes: the fixed costs ovsc_table() scales. Change
 * an instruction and re-measure them (zeroed tables and the
 * sync-write trace). */
"    .even\n"
"_stdl_ovsc_tb:\n"
"    movem.l %d0-%d4/%a0-%a3,-(%sp)\n"
"    lea    0xfffffa21.w,%a0\n"
"    lea    0xffff8209.w,%a1\n"
"    move.b _stdl_ovsc_l262lo,%d0\n"
"    move.b _stdl_ovsc_l262mid,%d1\n"
"    move.w _stdl_ovsc_waitn,%d2\n"
"1:  cmp.b  (%a1),%d0\n"
"    bne.s  2f\n"
"    cmp.b  0xffff8207.w,%d1\n"
"    beq.s  3f\n"
"2:  dbra   %d2,1b\n"
"    bra    ovsc_tb_late\n"
"3:  lea    _stdl_ovsc_n1,%a3\n"
"    move.w _stdl_ovsc_n2,%d3\n"
"    move.w _stdl_ovsc_n2t,%d4\n"
"    lea    _stdl_ovsc_scratch,%a2\n"
"    tst.b  _stdl_ovsc_tick\n"
"    bne    ovsc_tb_tick_warm\n"
"    moveq  #30,%d1\n"
"    bra.s  ovsc_tb_flick\n"
"ovsc_tb_poll:\n"
"    move.w #127,%d2\n"
"1:  cmp.b  (%a1),%d0\n"
"    bne.s  2f\n"
"    dbra   %d2,1b\n"
"    bra    ovsc_tb_late\n"
"2:  addq.b #2,%d0\n"
"    move.w #63,%d2\n"
"3:  move.b (%a1),%d1\n"
"    sub.b  %d0,%d1\n"
"    bmi.s  4f\n"
"    dbra   %d2,3b\n"
"    bra    ovsc_tb_late\n"
"4:  and.w  #0x3e,%d1\n"
"ovsc_tb_flick:\n"
"    move.w 0(%a3,%d1.w),%d2\n"
"5:  dbra   %d2,5b\n"
"    clr.b  (%a2)\n"
"    move.w %d3,%d2\n"
"6:  dbra   %d2,6b\n"
"    move.b #2,(%a2)\n"
"    cmp.l  #0xffff820a,%a2\n"
"    beq.s  ovsc_tb_check\n"
"    lea    0xffff820a.w,%a2\n"
"    bra.s  ovsc_tb_poll\n"
"ovsc_tb_tick_warm:\n"
"    bra.s  9f\n"
"ovsc_tb_tick:\n"
"    move.w #63,%d2\n"
"    move.b (%a0),%d1\n"
"8:  cmp.b  (%a0),%d1\n"
"    bne.s  9f\n"
"    dbra   %d2,8b\n"
"    bra.s  ovsc_tb_late\n"
"9:  clr.b  (%a2)\n"
"    move.w %d4,%d2\n"
"10: dbra   %d2,10b\n"
"    move.b #2,(%a2)\n"
"    cmp.l  #0xffff820a,%a2\n"
"    beq.s  ovsc_tb_check\n"
"    lea    0xffff820a.w,%a2\n"
"    bra.s  ovsc_tb_tick\n"
"ovsc_tb_check:\n"
"    move.w _stdl_ovsc_postn,%d2\n"
"    bsr    stdl_ovsc_wait_de_n\n"
"    bmi.s  ovsc_tb_late\n"
"ovsc_tb_out:\n"
"    clr.b  0xfffffa1b.w\n"
"    move.b #255,0xfffffa21.w\n"
"    move.b #5,0xfffffa1b.w\n"
"    move.b #0xFE,0xfffffa0f.w\n"
"    movem.l (%sp)+,%d0-%d4/%a0-%a3\n"
"    rte\n"
"ovsc_tb_late:\n"
"    addq.l #1,_stdl_ovsc_missed\n"
"    bra.s  ovsc_tb_out\n"
"\n"

/* Wait for Timer B to tick: the end of the next displayed line.
 * Bounded (about eight lines, see stdl_ovsc_waitn) so a missed
 * window can never hang the machine or hold off the VBL; returns N
 * set on timeout, N clear on success. The _n entry takes the bound
 * in d2 for waits that know how soon the tick is due. */
"stdl_ovsc_wait_de:\n"
"    move.w _stdl_ovsc_waitn,%d2\n"
"stdl_ovsc_wait_de_n:\n"
"    move.b (%a0),%d1\n"
"1:  cmp.b  (%a0),%d1\n"
"    bne.s  2f\n"
"    dbra   %d2,1b\n"
"2:  tst.w  %d2\n"
"    rts\n"
"\n"

/* Calibration loop: the flick's dbra, run for the count on the
 * stack. Same instruction, same two-word self-loop, so it takes the
 * same time per turn in the ISR's cache state as here. */
"    .even\n"
"_stdl_ovsc_spin:\n"
"    move.l 4(%sp),%d0\n"
"1:  dbra   %d0,1b\n"
"    rts\n"
);

extern void stdl_ovsc_vbl_top(void);
extern void stdl_ovsc_vbl_bot(void);
extern void stdl_ovsc_ta(void);
extern void stdl_ovsc_tb(void);
extern void stdl_ovsc_tc(void);
extern void stdl_ovsc_spin(int turns);

/*
 * Palette staging. With a border open the display starts fetching
 * at line 34 instead of 63, so a palette write from the main loop
 * is far more likely to land mid-display and flash wrong colours
 * for part of a frame. While open, stdl_pal_apply_hook points here:
 * writes are staged and a VBL callback drains them inside the
 * blanking. The hook lives in always-linked video.c so palette.o
 * carries only a pointer test for programs that never open a
 * border.
 */
static uint16_t pal_pending[16];
static volatile uint8_t pal_dirty;

static void ovsc_pal_stage(void)
{
    int i;
    for (i = 0; i < 16; i++) {
        pal_pending[i] = STDL_HWColour(stdl.colours[i].r,
                                       stdl.colours[i].g,
                                       stdl.colours[i].b);
    }
    pal_dirty = 1;
}

static void ovsc_pal_flush(void)
{
    int i;
    if (pal_dirty) {
        pal_dirty = 0;
        for (i = 0; i < 16; i++) {
            STDL_HWPAL[i] = pal_pending[i];
        }
    }
}

/*
 * Time the flick's dbra against displayed scanlines. Timer B counts
 * Display Enable ends, one per picture line of exactly 512 cycles
 * while the sync rate is 50Hz and no border trick is running -
 * interrupts are off, so none is. From the VBL the first event is
 * line 63's; the spin then runs 150 lines on a plain ST (the
 * slowest machine there is) and fewer on anything faster, ending
 * inside the 200-line picture, and the events it spanned bound its
 * length to one line either way. The middle of that bound is the
 * estimate: a third of a percent at 8MHz, two thirds at 16MHz, on
 * a delay of ~200 cycles - well under the window's margins. Costs
 * about a frame with interrupts masked, once per open. Returns the
 * cycles per turn x16, or 0 when no displayed line was seen.
 */
static int ovsc_counter_live(int c);

static int ovsc_calibrate(int *live)
{
    uint16_t sr;
    int i, v0, v1, lines;
    uint32_t r;

    STDL_WaitVBL();
    sr = stdl_int_off();
    MFP_TBCR = 0;
    MFP_TBDR = 255;
    MFP_TBCR = 8;
    v0 = MFP_TBDR;
    for (i = 0; i < 8000 && MFP_TBDR == v0; i++) {
        /* wait for the first event, up to ~2 frames */
    }
    v0 = MFP_TBDR;
    stdl_ovsc_spin(OVSC_SPIN);
    v1 = MFP_TBDR;
    lines = (v0 - v1) & 0xFF;
    if (i >= 8000 || lines < 2) {
        r = 0;
        *live = 1;
    } else {
        /* the spin took between lines*512 and (lines+1)*512 cycles */
        r = ((uint32_t)(2 * lines + 1) * 256UL * 16UL) / OVSC_SPIN;
        *live = ovsc_counter_live((int)r);
    }
    MFP_TBCR = 0;
    MFP_IPRA = (uint8_t)~0x01;              /* drop the pending B */
    stdl_int_restore(sr);
    return (int)r;
}

/*
 * Fill the flick's tables for a dbra turn of c/16 cycles. Costs
 * between the poll's last read and the writes are the flick path's
 * instruction times on an 8MHz 68000 (see the asm) scaled by
 * c/160, plus the register writes' bus cycles at their fixed width.
 * The poll's read that first sees fetch byte 136+2i lands at cycle
 * 338+4i (mid-grain); the 60Hz write is aimed at OVSC_T1 and the
 * 50Hz one at OVSC_T2 from there, rounding to the nearest turn and
 * never below zero - a late catch (a stall lengthened the poll)
 * just gets both writes proportionally later, still inside the
 * windows up to byte 158.
 */
static void ovsc_table(int c)
{
    const int tb = (OVSC_TB_CPU * c) / 192 + OVSC_TB_BUS;
    const int tt = (OVSC_TT_CPU * c) / 192 + OVSC_TT_BUS;
    const int tm = (OVSC_TM_CPU * c) / 192 + OVSC_TM_BUS;
    const int first = (OVSC_FIRST_TURN * c) / 192;
    int i, n, hit;

    for (i = 0; i < 16; i++) {
        hit = (i < 15) ? (OVSC_HIT_READ + 4 * i) * 16
                       : OVSC_PARK_READ * 16
                         + (OVSC_PARK_CPU * c) / 192 + OVSC_PARK_BUS;
        n = (OVSC_T1 * 16 - hit - tb + c / 2) / c;
        if (n >= 1) {
            n = (OVSC_T1 * 16 - hit - tb + first + c / 2) / c;
        }
        stdl_ovsc_n1[i] = (uint16_t)((n < 0) ? 0 : n);
    }
    n = ((OVSC_T2 - OVSC_T1) * 16 - tm + c / 2) / c;
    stdl_ovsc_n2 = (uint16_t)((n < 0) ? 0 : n);
    /* tick mode: no turns before the 60Hz write, it lands where the
     * fixed path puts it; the turns between the writes make up the
     * rest of the way to OVSC_T2 */
    hit = OVSC_TICK_READ * 16 + (OVSC_TICK_CPU * c) / 192 + OVSC_TICK_BUS;
    n = (OVSC_T2 * 16 - hit - tt - tm + c / 2) / c;
    stdl_ovsc_n2t = (uint16_t)((n < 0) ? 0 : n);
    /* the check that line 263 displayed: ~1300 plain-ST cycles of
     * polling, a line and a half, in this CPU's turns */
    stdl_ovsc_postn = (uint16_t)((40 * 192) / c);
}

/*
 * Is the video counter readable as a position while a line is
 * being fetched? On every ST it is: $ff8209 advances two bytes per
 * four cycles from cycle 56 to 376. An emulator's 16MHz mode may
 * only model the parked values, and the ISR then has to time from
 * Timer B instead. Wait for a line end, then for the next fetch to
 * begin, run ~180 plain-ST cycles into it and read: a live counter
 * is still moving, mid-line. Three lines, majority. Interrupts are
 * off and Timer B counts DE ends, as left by the calibration.
 */
static int ovsc_counter_live(int c)
{
    volatile uint8_t *vc = (volatile uint8_t *)0xFFFF8209UL;
    int k, i, live = 0;
    uint8_t v, p, off;

    for (k = 0; k < 3; k++) {
        v = MFP_TBDR;
        for (i = 0; i < 8000 && MFP_TBDR == v; i++) {
        }
        p = *vc;
        for (i = 0; i < 200 && *vc == p; i++) {
        }
        stdl_ovsc_spin((180 * 16) / c);
        off = (uint8_t)(*vc - p);
        if (off > 8 && off < 156) {
            live++;
        }
    }
    return live >= 2;
}

/* Hardware-only teardown, shared with the terminate path: vectors,
 * timers and sync rate back, both timer bits released. No GEMDOS,
 * no heap - release_hardware() repoints the screen afterwards. */
static void ovsc_release(void)
{
    uint16_t sr;

    if (!mode) {
        return;
    }
    sr = stdl_int_off();
    VEC_VBL = stdl_ovsc_old70;
    MFP_TACR = 0;
    MFP_TBCR = 0;
    MFP_IPRA &= (uint8_t)~0x21;
    MFP_ISRA &= (uint8_t)~0x21;
    MFP_IERA = (uint8_t)((MFP_IERA & ~0x21) | old_ier);
    MFP_IMRA = (uint8_t)((MFP_IMRA & ~0x21) | old_imr);
    VEC_TA = old_ta_vec;
    VEC_TB = old_tb_vec;
    VEC_TC = old_tc_vec;
    SYNC_REG = old_sync;
    stdl_int_restore(sr);
    mode = 0;
    stdl_no_hog = 0;
    stdl_pal_apply_hook = NULL;
    stdl_shutdown_overscan = NULL;
}

/* (Re)program prefixes, vectors and enable bits for a mode. The
 * screen must already point at the tall buffer. */
static void ovsc_program(int m)
{
    const uint8_t bits = (uint8_t)(((m & MODE_TOP) ? 0x20 : 0)
                                 | ((m & MODE_BOT) ? 0x01 : 0));
    /* frame line 262 is the last picture line: row 199 of a bottom
     * screen, row 228 of a combined one (rows count from line 34) */
    const uintptr_t l262 = (uintptr_t)buf
        + (uintptr_t)((m & MODE_TOP) ? 228 : 199) * STDL_SCREEN_STRIDE;
    uint16_t sr = stdl_int_off();

    if (!mode) {
        /* first open: save both timers' state and hook the VBL */
        old_ta_vec = VEC_TA;
        old_tb_vec = VEC_TB;
        old_tc_vec = VEC_TC;
        stdl_ovsc_oldtc = old_tc_vec;
        old_ier = (uint8_t)(MFP_IERA & 0x21);
        old_imr = (uint8_t)(MFP_IMRA & 0x21);
        stdl_ovsc_old70 = VEC_VBL;
    } else {
        VEC_VBL = stdl_ovsc_old70;      /* prefix swap below */
    }
    MFP_TACR = 0;
    MFP_TBCR = 0;
    MFP_IPRA &= (uint8_t)~0x21;
    MFP_ISRA &= (uint8_t)~0x21;
    VEC_TA = (uint32_t)(uintptr_t)stdl_ovsc_ta;
    VEC_TB = (uint32_t)(uintptr_t)stdl_ovsc_tb;
    VEC_TC = (uint32_t)(uintptr_t)stdl_ovsc_tc;
    MFP_IERA = (uint8_t)((MFP_IERA & ~0x21) | bits);
    MFP_IMRA = (uint8_t)((MFP_IMRA & ~0x21) | bits);
    stdl_ovsc_bhi = (uint8_t)((uintptr_t)buf >> 16);
    stdl_ovsc_bmid = (uint8_t)((uintptr_t)buf >> 8);
    stdl_ovsc_l262lo = (uint8_t)l262;
    stdl_ovsc_l262mid = (uint8_t)(l262 >> 8);
    /* every bounded wait gives up after ~8 lines (4096 plain-ST
     * cycles of ~32-cycle polls): a border that failed to open has
     * no more DE events this frame, and a longer wait at MFP
     * priority would hold off the VBL, arm Timer B late and make
     * the next frame miss too - a cascade that was measured at a
     * dozen frames before this bound existed */
    stdl_ovsc_waitn = (uint16_t)((128 * 192) / (c16 ? c16 : OVSC_C16_8MHZ));
    stdl_ovsc_tbref = (m & MODE_BOT) ? OVSC_TBREF_BOTH : OVSC_TBREF_TOP;
    stdl_ovsc_tbarm = (m & MODE_BOT) ? 226 : 0;
    stdl_ovsc_missed = 0;
    VEC_VBL = (uint32_t)(uintptr_t)
        ((m & MODE_TOP) ? stdl_ovsc_vbl_top : stdl_ovsc_vbl_bot);
    stdl_int_restore(sr);

    mode = m;
    stdl_no_hog = 1;
    stdl_shutdown_overscan = ovsc_release;
}

static void ovsc_surface(void)
{
    switch (mode) {
    case MODE_TOP:
        stdl_screen.pixels = buf + TOP_HIDDEN_ROWS * STDL_SCREEN_STRIDE;
        stdl_screen.h = STDL_OVERSCAN_TOP_H;
        break;
    case MODE_BOT:
        stdl_screen.pixels = buf;
        stdl_screen.h = STDL_OVERSCAN_BOTTOM_H;
        break;
    case MODE_TOP | MODE_BOT:
        stdl_screen.pixels = buf + TOP_HIDDEN_ROWS * STDL_SCREEN_STRIDE;
        stdl_screen.h = STDL_OVERSCAN_BOTH_H;
        break;
    default:
        stdl_screen.pixels = stdl.page[stdl.backpage];
        stdl_screen.h = STDL_SCREEN_H;
        break;
    }
    stdl_screen.clip.x = 0;
    stdl_screen.clip.y = 0;
    stdl_screen.clip.w = STDL_SCREEN_W;
    stdl_screen.clip.h = (uint16_t)stdl_screen.h;
}

/* Open one border, combining with the other if it is already open.
 * Returns the resulting surface height or 0 with the error set. */
static int ovsc_open(int which)
{
    int m = mode | which;

    if (m == mode) {
        return stdl_screen.h;
    }
    if (!stdl.video_set) {
        STDL_SetError("no video mode set");
        return 0;
    }
    if (stdl.doublebuf) {
        STDL_SetError("border overscan does not combine with "
                      "STDL_DOUBLEBUF");
        return 0;
    }
    if (stdl.mach.mch_cookie >= 0x00020000UL) {
        STDL_SetError("TT/Falcon video has no GLUE borders");
        return 0;
    }
    /* The tricks are built on a 50Hz frame: the top border only
     * exists because a 50Hz picture waits for line 63, and the
     * bottom trick's line counts are PAL's. Opening a border is an
     * active choice, so a 60Hz base screen is not refused - it is
     * switched to 50Hz for as long as a border stays open (the VBL
     * prefix asserts it every frame anyway) and put back on the
     * final close. ST colour monitors sync both rates; a TV fed
     * through a modulator follows its region as it always did. */
    if (!mode) {
        old_sync = (uint8_t)(SYNC_REG & 3);
        SYNC_REG = 2;
    }
    if (buf_alloc == NULL) {
        /* 256-byte alignment satisfies the plain ST's screen base
         * granularity; unused rows stay zero = border colour. One
         * buffer sized for the largest mode serves them all, so a
         * mode change never reallocates */
        buf_alloc = malloc(MAX_FETCH_BYTES + 256);
        if (buf_alloc == NULL) {
            STDL_SetError("out of memory for overscan screen");
            return 0;
        }
        buf = (uint8_t *)(((uintptr_t)buf_alloc + 255)
                          & ~(uintptr_t)255);
    }
    memset(buf, 0, MAX_FETCH_BYTES);

    if (!mode) {
        /* point the display at the tall buffer and let the switch
         * land before the border trick starts, so no frame ever
         * fetches past the end of the old 200-row screen */
        (void)Setscreen(buf, buf, -1);
        STDL_WaitVBL();
        /* palette writes land in the blanking from here on; if no
         * VBL slot is free they stay immediate, as before */
        if (STDL_AddVBL(ovsc_pal_flush) == 0) {
            stdl_pal_apply_hook = ovsc_pal_stage;
        }
    }
    if ((which & MODE_BOT) && c16 == 0) {
        /* once per process: the CPU speed does not change under a
         * running program. A failed measurement (no displayed line
         * seen) falls back to the plain ST's numbers rather than
         * refuse the border. */
        int live;
        c16 = ovsc_calibrate(&live);
        if (c16 == 0) {
            c16 = OVSC_C16_8MHZ;
        }
        stdl_ovsc_tick = (uint8_t)!live;
        ovsc_table(c16);
    }
    ovsc_program(m);
    ovsc_surface();
    return stdl_screen.h;
}

static int ovsc_close(int which)
{
    int m = mode & ~which;

    if (!(mode & which)) {
        return stdl_screen.h;
    }
    if (m) {
        memset(buf, 0, MAX_FETCH_BYTES);
        ovsc_program(m);
        ovsc_surface();
        return stdl_screen.h;
    }
    if (stdl_pal_apply_hook != NULL) {
        STDL_RemoveVBL(ovsc_pal_flush);
    }
    ovsc_release();                     /* clears the hook */
    stdl_palette_apply_hw();            /* immediate, no staging */
    (void)Setscreen(stdl.page[0], stdl.page[0], -1);
    STDL_WaitVBL();
    ovsc_surface();
    free(buf_alloc);
    buf_alloc = NULL;
    buf = NULL;
    return 0;
}

int STDL_OpenTopBorder(void)
{
    return ovsc_open(MODE_TOP);
}

int STDL_OpenBottomBorder(void)
{
    return ovsc_open(MODE_BOT);
}

void STDL_CloseTopBorder(void)
{
    (void)ovsc_close(MODE_TOP);
}

void STDL_CloseBottomBorder(void)
{
    (void)ovsc_close(MODE_BOT);
}

uint32_t STDL_OverscanMisses(void)
{
    return stdl_ovsc_missed;
}

#else /* !__m68k__ */

int STDL_OpenTopBorder(void)
{
    STDL_SetError("border overscan needs ST hardware");
    return 0;
}

int STDL_OpenBottomBorder(void)
{
    STDL_SetError("border overscan needs ST hardware");
    return 0;
}

void STDL_CloseTopBorder(void)
{
}

void STDL_CloseBottomBorder(void)
{
}

uint32_t STDL_OverscanMisses(void)
{
    return 0;
}

#endif
