/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/*
 * The raster service's bookkeeping, against the host's MFP and
 * vector stand-ins (the interrupt handlers are the machine's; RASTCHK
 * covers them):
 *  - refusals: no video mode, bad arguments, a border holding Timer B;
 *  - opening: the entry count, both registers' tables starting as the
 *    palette, the vectors and the MFP's Timer B bits, the owner, the
 *    terminate hook and a VBL slot;
 *  - the VBL arming a shown table: entry 0 written to the registers,
 *    the handlers' pointer at entry 1, Timer B restarted;
 *  - the program never being handed the table the VBL or the current
 *    frame reads, and each new one starting as a copy of the last
 *    shown;
 *  - closing and the terminate path putting back what was there.
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

extern uint16_t *volatile stdl_rast_ptr;

static void fire(void)
{
    int i;

    for (i = 0; i < STDL_NVBLS; i++) {
        if (STDL_VBLQUEUE[i] != NULL) {
            STDL_VBLQUEUE[i]();
        }
    }
}

static int slots(void)
{
    int i, n = 0;

    for (i = 0; i < STDL_NVBLS; i++) {
        n += STDL_VBLQUEUE[i] != NULL;
    }
    return n;
}

int main(void)
{
    uint16_t *shown[8];
    uint16_t *t;
    int n, i, k;

    STDL_Init(0);
    stdl.colours[0].r = 0x20;
    stdl.colours[15].g = 0xE0;
    stdl_host_vec_tb = 0x1111;
    stdl_host_vec_tc = 0x2222;
    stdl_host_mfp[0x07] = 0x40;         /* IERA: another channel on */
    stdl_host_mfp[0x13] = 0x40;

    /* refusals */
    stdl.video_set = 0;
    CHECK(STDL_OpenRasters(2, 0, 15) == -1, "opened with no video mode");
    stdl.video_set = 1;
    CHECK(STDL_OpenRasters(0, 0, 15) == -1, "step 0 accepted");
    CHECK(STDL_OpenRasters(2, 16, -1) == -1, "register 16 accepted");
    CHECK(STDL_OpenRasters(2, 3, 3) == -1, "the same register twice");
    stdl_timerb_owner = STDL_TB_BORDER;
    CHECK(STDL_OpenRasters(2, 0, 15) == -1, "opened over a border");
    CHECK(stdl_timerb_owner == STDL_TB_BORDER, "border lost Timer B");
    stdl_timerb_owner = 0;
    CHECK(STDL_RasterTable() == NULL, "a table while closed");
    CHECK(slots() == 0, "a refusal took a VBL slot");

    /* open: two registers every second line */
    n = STDL_OpenRasters(2, 0, 15);
    CHECK(n == 101, "%d entries every 2 lines", n);
    CHECK(stdl_timerb_owner == STDL_TB_RASTERS, "owner not set");
    CHECK(stdl_shutdown_rasters != NULL, "no terminate hook");
    CHECK(slots() == 1, "%d VBL slots", slots());
    CHECK(stdl_host_vec_tb != 0x1111 && stdl_host_vec_tc != 0x2222,
          "vectors not taken");
    CHECK((stdl_host_mfp[0x07] & 0x41) == 0x41
          && (stdl_host_mfp[0x13] & 0x41) == 0x41,
          "Timer B not enabled, or another channel lost");
    t = STDL_RasterTable();
    CHECK(t != NULL && t[0] == STDL_HWColour(0x20, 0, 0)
          && t[1] == STDL_HWColour(0, 0xE0, 0)
          && t[200] == t[0] && t[201] == t[1],
          "table does not start as the palette");

    /* the VBL arms what is shown */
    for (i = 0; i < n; i++) {
        t[i * 2] = (uint16_t)(0x100 + i);
        t[i * 2 + 1] = (uint16_t)(0x200 + i);
    }
    STDL_ShowRasters();
    fire();
    CHECK(STDL_HWPAL[0] == 0x100 && STDL_HWPAL[15] == 0x200,
          "entry 0 not written: %03x %03x", STDL_HWPAL[0], STDL_HWPAL[15]);
    CHECK(stdl_rast_ptr == t + 2, "handlers do not start at entry 1");
    CHECK(stdl_host_mfp[0x21] == 2 && stdl_host_mfp[0x1B] == 8,
          "Timer B not restarted: data %u control %u",
          stdl_host_mfp[0x21], stdl_host_mfp[0x1B]);

    /* the program is never handed the armed or the shown table, and
     * each starts as the one last shown */
    shown[0] = t;
    for (k = 1; k < 8; k++) {
        uint16_t *next = STDL_RasterTable();

        CHECK(next != shown[k - 1], "handed the shown table at %d", k);
        CHECK(memcmp(next, shown[k - 1], 202 * 2) == 0,
              "new table %d is not a copy of the last shown", k);
        next[k] = (uint16_t)(0x300 + k);
        STDL_ShowRasters();
        shown[k] = next;
        if (k & 1) {
            fire();             /* sometimes a VBL, sometimes not */
        }
        CHECK(STDL_RasterTable() != shown[k], "drawing into the shown");
        CHECK(stdl_rast_ptr < STDL_RasterTable()
              || stdl_rast_ptr >= STDL_RasterTable() + 202,
              "drawing into the table the handlers read");
    }

    /* close: everything back */
    STDL_CloseRasters();
    CHECK(stdl_host_vec_tb == 0x1111 && stdl_host_vec_tc == 0x2222,
          "vectors not restored");
    CHECK((stdl_host_mfp[0x07] & 0x41) == 0x40
          && (stdl_host_mfp[0x13] & 0x41) == 0x40,
          "MFP bits not restored");
    CHECK(stdl_timerb_owner == 0 && stdl_shutdown_rasters == NULL,
          "owner or hook left behind");
    CHECK(slots() == 0, "VBL slot left behind");
    CHECK(STDL_HWPAL[0] == STDL_HWColour(0x20, 0, 0),
          "palette not put back");

    /* one register, then the terminate path's teardown */
    n = STDL_OpenRasters(4, 7, -1);
    CHECK(n == 51, "%d entries every 4 lines", n);
    t = STDL_RasterTable();
    CHECK(t[50] == t[0] && t[0] == STDL_HWColour(stdl.colours[7].r,
                                                 stdl.colours[7].g,
                                                 stdl.colours[7].b),
          "one-register table does not start as the palette");
    stdl_shutdown_rasters();
    CHECK(stdl_host_vec_tb == 0x1111 && stdl_host_vec_tc == 0x2222
          && stdl_timerb_owner == 0, "terminate path left Timer B");
    CHECK(STDL_RasterTable() == NULL, "still open after the teardown");

    if (failures) {
        printf("test_raster: %d failures\n", failures);
        return 1;
    }
    printf("test_raster: ok\n");
    return 0;
}
