/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/*
 * The public VBL callback slots.
 *
 * There is no interrupt on the host, but the part that goes wrong is
 * not the interrupt - it is the bookkeeping: which TOS queue slots
 * are STDL's, that a double install does not consume two of them,
 * that removal clears the right one, and above all that shutdown
 * clears STDL's slots and *only* STDL's. Getting the last one wrong
 * on target means either a dead entry pointing into freed memory
 * (the crash this API exists to stop) or wiping somebody else's
 * handler out of the queue.
 *
 * stubs.c provides the queue as plain memory, so the tests read the
 * slots directly and call the entries themselves in place of the
 * VBL.
 */
#include <stdio.h>
#include <stdlib.h>
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

static int hits_a, hits_b, hits_c;

static void cb_a(void) { hits_a++; }
static void cb_b(void) { hits_b++; }
static void cb_c(void) { hits_c++; }
static void foreign(void) { }

static int slot_of(void (*fn)(void))
{
    int i;

    for (i = 0; i < STDL_NVBLS; i++) {
        if (STDL_VBLQUEUE[i] == fn) {
            return i;
        }
    }
    return -1;
}

static int count_of(void (*fn)(void))
{
    int i, n = 0;

    for (i = 0; i < STDL_NVBLS; i++) {
        if (STDL_VBLQUEUE[i] == fn) {
            n++;
        }
    }
    return n;
}

/* run the queue as the VBL interrupt would */
static void fire(void)
{
    int i;

    for (i = 0; i < STDL_NVBLS; i++) {
        if (STDL_VBLQUEUE[i] != NULL) {
            STDL_VBLQUEUE[i]();
        }
    }
}

static void clear_queue(void)
{
    int i;

    for (i = 0; i < STDL_NVBLS; i++) {
        STDL_VBLQUEUE[i] = NULL;
    }
    stdl_shutdown_vbl = NULL;
}

int main(void)
{
    int i, n;

    clear_queue();

    /* a null callback is refused, and refusing must not claim a slot */
    CHECK(STDL_AddVBL(NULL) == -1, "null callback accepted");
    CHECK(slot_of(NULL) == 0, "null install consumed a slot");

    /* install, and it is in exactly one slot and gets called */
    CHECK(STDL_AddVBL(cb_a) == 0, "install a failed");
    CHECK(count_of(cb_a) == 1, "a in %d slots", count_of(cb_a));
    fire();
    CHECK(hits_a == 1, "a called %d times", hits_a);

    /* installing twice is a no-op, not a second slot */
    CHECK(STDL_AddVBL(cb_a) == 0, "reinstall a failed");
    CHECK(count_of(cb_a) == 1, "reinstall duplicated a");
    hits_a = 0;
    fire();
    CHECK(hits_a == 1, "a called %d times after reinstall", hits_a);

    /* a second callback coexists */
    CHECK(STDL_AddVBL(cb_b) == 0, "install b failed");
    CHECK(slot_of(cb_b) != slot_of(cb_a), "b took a's slot");
    hits_a = hits_b = 0;
    fire();
    CHECK(hits_a == 1 && hits_b == 1, "a %d b %d", hits_a, hits_b);

    /* removal takes out one and leaves the other */
    STDL_RemoveVBL(cb_a);
    CHECK(count_of(cb_a) == 0, "a still installed");
    CHECK(count_of(cb_b) == 1, "removal disturbed b");
    hits_a = hits_b = 0;
    fire();
    CHECK(hits_a == 0 && hits_b == 1, "after remove: a %d b %d",
          hits_a, hits_b);

    /* removing something that is not installed is harmless */
    STDL_RemoveVBL(cb_a);
    STDL_RemoveVBL(NULL);
    CHECK(count_of(cb_b) == 1, "spurious removal disturbed b");

    /*
     * Shutdown clears STDL's slots and nothing else. A foreign entry
     * - TOS's own, or a handler the program installed before STDL
     * existed - has to survive: the queue is shared.
     */
    STDL_VBLQUEUE[STDL_NVBLS - 1] = foreign;
    CHECK(STDL_AddVBL(cb_c) == 0, "install c failed");
    CHECK(stdl_shutdown_vbl != NULL, "no shutdown hook registered");
    stdl_shutdown_vbl();
    CHECK(count_of(cb_b) == 0 && count_of(cb_c) == 0,
          "shutdown left STDL callbacks installed");
    CHECK(count_of(foreign) == 1, "shutdown wiped a foreign entry");
    STDL_VBLQUEUE[STDL_NVBLS - 1] = NULL;

    /* a full queue is a clean failure, not a corrupted one */
    clear_queue();
    for (i = 0; i < STDL_NVBLS; i++) {
        STDL_VBLQUEUE[i] = foreign;
    }
    CHECK(STDL_AddVBL(cb_a) == -1, "install into a full queue");
    CHECK(count_of(foreign) == STDL_NVBLS, "full queue was overwritten");

    /* one free slot is enough */
    STDL_VBLQUEUE[3] = NULL;
    CHECK(STDL_AddVBL(cb_a) == 0, "install into the last free slot");
    CHECK(slot_of(cb_a) == 3, "took slot %d, expected 3",
          slot_of(cb_a));

    /* and shutdown gives that one back without touching the rest */
    n = 0;
    for (i = 0; i < STDL_NVBLS; i++) {
        if (STDL_VBLQUEUE[i] == foreign) {
            n++;
        }
    }
    stdl_shutdown_vbl();
    CHECK(count_of(cb_a) == 0, "a survived shutdown");
    CHECK(count_of(foreign) == n, "foreign entries changed");

    clear_queue();
    if (failures == 0) {
        printf("VBL callback tests passed\n");
    }
    return failures != 0;
}
