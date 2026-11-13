/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * How long does this machine take over ordinary compiled code, and how
 * far is the emulator from it? Every TT and Falcon timing in this
 * repository was taken in Hatari, and on 2026-11-11 a real TT (TOS
 * 3.06, both caches on) took 1.5 to 2.3 times as long as Hatari's TT
 * (--cpu-exact on --data-cache on --mmu off) over one binary built by
 * another compiler: 1.46 for a loop the instruction cache holds, 2.2
 * to 2.3 for calls, for code larger than the cache and for reading
 * memory in order, ST-RAM and TT-RAM alike. CPUCHK is that measurement
 * built as STDL's programs are, so that the factor for this library's
 * own figures can be taken on the machine, against run.sh's settings.
 *
 * It times, against the 200Hz counter, a second of each of:
 *
 *  - loop: two instructions, eight bytes (subq, bne), in any cache;
 *  - 32-way: one of 32 pieces of code chosen without a pattern, more
 *    code than a 68030's 256-byte instruction cache holds;
 *  - calls: a small function, called as compiled C calls all day;
 *  - ST read, ST scatter: 128K of ST-RAM read in order, and a long
 *    here and there, so that a data cache never helps;
 *  - ST copy: a screen's 32000 bytes copied within ST-RAM by memcpy;
 *  - TT read, TT scatter, TT>ST copy: the same from TT-RAM, where the
 *    machine has some.
 *
 * Each is in nanoseconds per unit: a turn, a step, a call, a long read
 * or copied. On a 68030 (a TT, a Falcon) each runs with the caches as
 * found, both on with burst fill (CACR 0x3111) and both off; on a Mega
 * STE as found, at 8MHz, 16MHz without the cache and 16MHz with it;
 * elsewhere as found. Whatever it changed is put back. The whole set
 * runs twice, passes A and B, so that each log carries its own noise
 * floor, and the lines are SPRCOST's, so tests/hatari/sccmp.py
 * compares two logs: the emulator's as OLD and the machine's as NEW
 * gives each case's real/emulated time and their geometric mean.
 *
 * It links no STDL: it measures the machine, and the library's VBL and
 * keyboard handlers would only add to it. The first lines say where
 * the program and its blocks landed: code in TT-RAM and in ST-RAM are
 * different machines on a TT. A case is a second whatever the machine,
 * so a run is about 15s on an ST or an STE, 50s on a Mega STE, 40s on
 * a Falcon and 40-55s on a TT (with TT-RAM, three cases more).
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 \
 *       -Wall -Wextra -o dist/CPUCHK.TOS tests/hatari/cpuchk.c
 *   MACHINE=tt TOS=<a 512K EmuTOS or TOS 3.06> \
 *       LATE="--ttram 4 --addr24 off" \
 *       tests/hatari/run.sh cpu_tt dist/CPUCHK.TOS 6 "waitfor SC:done"
 *   tests/hatari/sccmp.py tests/hatari/out/cpu_tt.log CPUCHK.LOG
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <mint/osbind.h>
#define HOLD_NO_STDL
#include "../../examples/hold.h"

#define HZ200       (*(volatile uint32_t *)0x4BAUL)
#define PHYSTOP     (*(volatile uint32_t *)0x42EUL)
#define SYSBASE     (*(volatile uint32_t *)0x4F2UL)
#define RAMTOP      (*(volatile uint32_t *)0x5A4UL)
#define RAMVALID    (*(volatile uint32_t *)0x5A8UL)
#define MSTE_SPEED  (*(volatile uint8_t *)0xFFFF8E21UL)

#define WINDOW      200         /* ticks of the 200Hz counter a case */
#define BLOCK_LONGS 32768L      /* 128K: far past any data cache */
#define COPY_BYTES  32000L      /* a screen */
#define LOOP_BATCH  4000
#define STEP_BATCH  256
#define CALL_BATCH  1000
#define SCAT_BATCH  4096

#define MCH_MEGASTE 0x00010010UL

static uint32_t *st_block, *tt_block;
static volatile uint32_t sink;
static char pass;

static uint32_t read_cacr(void)
{
    uint32_t v;

    __asm__ volatile(".word 0x4e7a,0x0002\n\tmove.l %%d0,%0"
                     : "=d"(v) : : "d0");          /* movec cacr,d0 */
    return v;
}

/* 0x0808 empties both caches as the setting goes in */
static void write_cacr(uint32_t v)
{
    v |= 0x0808;
    __asm__ volatile("move.l %0,%%d0\n\t.word 0x4e7b,0x0002"
                     : : "d"(v) : "d0");           /* movec d0,cacr */
}

static uint32_t cookie(uint32_t name, int *found)
{
    const uint32_t *jar = *(const uint32_t **)0x5A0UL;

    *found = 0;
    for (; jar != NULL && jar[0] != 0; jar += 2) {
        if (jar[0] == name) {
            *found = 1;
            return jar[1];
        }
    }
    return 0;
}

/* --- the work: each call is one batch ------------------------------ */

static uint32_t work_loop(void)
{
    uint32_t n = LOOP_BATCH;

    __asm__ volatile("1: subq.l #1,%0\n\tbne.s 1b" : "+d"(n));
    return n;
}

#define STEP(k)                                         \
    case k:                                             \
        x += (uint32_t)(k) * 2654435761u;               \
        x ^= x >> ((k) % 7 + 1);                        \
        x -= (uint32_t)(k) << 4;                        \
        x = (x << ((k) % 5 + 1)) | (x >> 27);           \
        x += s & (0xffu << ((k) % 3));                  \
        break;

static uint32_t step_x = 1, step_s = 0x2545f491u;

static uint32_t work_32way(void)
{
    uint32_t x = step_x, s = step_s;
    int n;

    for (n = 0; n < STEP_BATCH; n++) {
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        switch (s >> 27) {
            STEP(0) STEP(1) STEP(2) STEP(3) STEP(4) STEP(5) STEP(6)
            STEP(7) STEP(8) STEP(9) STEP(10) STEP(11) STEP(12)
            STEP(13) STEP(14) STEP(15) STEP(16) STEP(17) STEP(18)
            STEP(19) STEP(20) STEP(21) STEP(22) STEP(23) STEP(24)
            STEP(25) STEP(26) STEP(27) STEP(28) STEP(29) STEP(30)
            STEP(31)
        }
    }
    step_x = x;
    step_s = s;
    return x;
}

static uint32_t __attribute__((noinline, noclone))
leaf(uint32_t a, uint32_t b)
{
    return (a << 1) + a + (b ^ (a >> 3));
}

static uint32_t work_calls(void)
{
    uint32_t x = 7, n;

    for (n = 0; n < CALL_BATCH; n++) {
        x = leaf(x, n);
    }
    return x;
}

static uint32_t read_in_order(const uint32_t *p)
{
    const uint32_t *end = p + BLOCK_LONGS;
    uint32_t sum = 0;

    while (p < end) {
        sum += *p++;
    }
    return sum;
}

static uint32_t scat_at;

static uint32_t read_scattered(const uint32_t *p)
{
    uint32_t sum = 0, at = scat_at;
    int n;

    for (n = 0; n < SCAT_BATCH; n++) {
        at = (at + 40503u) & (uint32_t)(BLOCK_LONGS - 1);
        sum += p[at];
    }
    scat_at = at;
    return sum;
}

static uint32_t work_st_read(void) { return read_in_order(st_block); }
static uint32_t work_tt_read(void) { return read_in_order(tt_block); }
static uint32_t work_st_scat(void) { return read_scattered(st_block); }
static uint32_t work_tt_scat(void) { return read_scattered(tt_block); }

static uint32_t work_st_copy(void)
{
    memcpy(st_block + BLOCK_LONGS / 2, st_block, COPY_BYTES);
    return st_block[BLOCK_LONGS / 2];
}

static uint32_t work_tt_copy(void)
{
    memcpy(st_block + BLOCK_LONGS / 2, tt_block, COPY_BYTES);
    return st_block[BLOCK_LONGS / 2];
}

/* --- the measurement ---------------------------------------------- */

/* Batches until a window of the 200Hz counter has passed, from a tick
 * boundary; the time is read after the batch that crossed it, so the
 * overshoot is measured, not lost. */
static void measure(const char *setting, const char *what,
                    uint32_t (*work)(void), uint32_t units)
{
    uint32_t t0, t, batches = 0;
    unsigned long long ns10;
    char label[32];

    t0 = HZ200;
    while (HZ200 == t0) {
    }
    t0 = HZ200;
    do {
        sink += work();
        batches++;
        t = HZ200 - t0;
    } while (t < WINDOW);
    ns10 = (unsigned long long)t * 50000000ULL /
           ((unsigned long long)batches * units);
    snprintf(label, sizeof(label), "%s: %s", setting, what);
    printf("SC:%c %-26s cpu=%6lu.%lu\n", pass, label,
           (unsigned long)(ns10 / 10), (unsigned long)(ns10 % 10));
    fflush(stdout);
}

static void measure_all(const char *setting)
{
    measure(setting, "loop", work_loop, LOOP_BATCH);
    measure(setting, "32-way", work_32way, STEP_BATCH);
    measure(setting, "calls", work_calls, CALL_BATCH);
    measure(setting, "ST read", work_st_read, BLOCK_LONGS);
    measure(setting, "ST scatter", work_st_scat, SCAT_BATCH);
    measure(setting, "ST copy", work_st_copy, COPY_BYTES / 4);
    if (tt_block != NULL) {
        measure(setting, "TT read", work_tt_read, BLOCK_LONGS);
        measure(setting, "TT scatter", work_tt_scat, SCAT_BATCH);
        measure(setting, "TT>ST copy", work_tt_copy, COPY_BYTES / 4);
    }
}

/* Mxalloc is GEMDOS 0.19's: before TOS 1.04 there is only Malloc,
 * and only ST-RAM to get */
static uint32_t *alloc(long n, int mode)
{
    long p = (long)Mxalloc(n, mode);

    if (p == -32 && mode == 0) {
        p = (long)Malloc(n);
    }
    return p > 0 ? (uint32_t *)p : NULL;
}

static void fill(uint32_t *p)
{
    uint32_t x = 0x9E3779B9u;
    long i;

    for (i = 0; i < BLOCK_LONGS; i++) {
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        p[i] = x;
    }
}

static const char *where(const void *p)
{
    return (uint32_t)p < PHYSTOP ? "ST-RAM" : "alt-RAM";
}

static int run(void)
{
    uint32_t mch, cpu, cacr = 0, tt_top;
    uint8_t speed = 0;
    int found, is030, is_mste, i;

    mch = cookie(0x5F4D4348UL, &found);              /* _MCH */
    cpu = cookie(0x5F435055UL, &found);              /* _CPU */
    is030 = found && cpu == 30;
    is_mste = mch == MCH_MEGASTE;
    tt_top = RAMVALID == 0x1357BD13UL ? RAMTOP : 0;

    printf("SC: machine mch=%08lx cpu=%lu tos=%04x phystop=%08lx "
           "ramtop=%08lx\n", (unsigned long)mch, (unsigned long)cpu,
           ((const uint16_t *)SYSBASE)[1], (unsigned long)PHYSTOP,
           (unsigned long)tt_top);
    if (cpu >= 20) {
        cacr = read_cacr();
        printf("SC: cacr found %08lx (instruction cache %s, data cache "
               "%s)\n", (unsigned long)cacr, (cacr & 1) ? "on" : "off",
               (cacr & 0x100) ? "on" : "off");
    }
    if (is_mste) {
        speed = MSTE_SPEED;
        printf("SC: speed register found %02x (%s, cache %s)\n", speed,
               (speed & 2) ? "16MHz" : "8MHz",
               (speed & 1) ? "on" : "off");
    }

    st_block = alloc(BLOCK_LONGS * 4, 0);
    tt_block = alloc(BLOCK_LONGS * 4, 1);
    if (st_block == NULL || (uint32_t)st_block >= PHYSTOP) {
        printf("SC: no 128K block of ST-RAM\n");
        return 1;
    }
    if (tt_block != NULL && (uint32_t)tt_block < PHYSTOP) {
        Mfree(tt_block);                /* TT-RAM asked, ST-RAM given */
        tt_block = NULL;
    }
    fill(st_block);
    if (tt_block != NULL) {
        fill(tt_block);
    }
    printf("SC: code at %p (%s), blocks at %p (ST-RAM) and ", (void *)run,
           where((const void *)run), (void *)st_block);
    if (tt_block != NULL) {
        printf("%p (TT-RAM)\n", (void *)tt_block);
    } else {
        printf("none (no TT-RAM)\n");
    }
    printf("SC: units: nanoseconds per unit (a turn, a step, a call, "
           "a long read or copied)\n");
    fflush(stdout);

    for (i = 0; i < 2; i++) {
        pass = (char)('A' + i);
        measure_all("found");
        if (is030) {
            write_cacr(0x3111);
            measure_all("cache on");
            write_cacr(0);
            measure_all("cache off");
            write_cacr(cacr);
        } else if (is_mste) {
            /* bit 0 the cache, bit 1 the 16MHz clock; the high bits
             * are whoever set them's (src/cpuspeed.c) */
            MSTE_SPEED = (uint8_t)(speed & ~3);
            measure_all("8MHz");
            MSTE_SPEED = (uint8_t)((speed & ~3) | 2);
            measure_all("16MHz");
            MSTE_SPEED = (uint8_t)(speed | 3);
            measure_all("16MHz+cache");
            MSTE_SPEED = speed;
        }
    }
    if (is030) {
        printf("SC: cacr put back %08lx (check %08lx)\n",
               (unsigned long)read_cacr(), (unsigned long)sink);
    } else {
        printf("SC: check %08lx\n", (unsigned long)sink);
    }
    Mfree(st_block);
    if (tt_block != NULL) {
        Mfree(tt_block);
    }
    return 0;
}

int main(void)
{
    long ssp = 0;
    int rc;

    hold_on_exit("CPUCHK");

    /* the 200Hz counter, the cookie jar and the cache register are
     * the supervisor's; Super() is a toggle, so ask first */
    if (Super(1L) == 0) {
        ssp = Super(0L);
    }
    rc = run();
    if (ssp != 0) {
        Super((void *)ssp);
    }
    printf("SC:done\n");
    fflush(stdout);
    return rc;
}
