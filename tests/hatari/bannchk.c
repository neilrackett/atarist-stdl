/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * BANNCHK: STDL_Init's line on the console (src/banner.c), which only
 * the machine writes. Built as it is, the line is there once, however
 * often the program inits; with -DBANNCHK_OFF, STDL_UseBanner(0) comes
 * first and it is not there at all. The console's log says which:
 *
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 -Iinclude \
 *       -o dist/BANNCHK.TOS tests/hatari/bannchk.c libstdl.a
 *   STCMD_NO_TTY=1 stcmd m68k-atari-mint-gcc -O2 -std=gnu99 -Iinclude \
 *       -DBANNCHK_OFF -o dist/BANNOFF.TOS tests/hatari/bannchk.c libstdl.a
 *   TOS=$PWD/tmp/tos/etos256uk.img EXTRA="--memsize 4" \
 *       tests/hatari/run.sh bann dist/BANNCHK.TOS 8 "waitfor BAN:done"
 *   grep -c "^STDL .*| neilrackett.com/atarist" tests/hatari/out/bann.log
 *
 * 1 for BANNCHK and 0 for BANNOFF.
 */
#include <stdio.h>
#include <stdl/stdl.h>
#include "../../examples/hold.h"

int main(void)
{
    int was;
    hold_on_exit("BANNCHK");
#ifdef BANNCHK_OFF
    was = STDL_UseBanner(0);
#else
    was = STDL_UseBanner(-1);
#endif
    STDL_Init(STDL_INIT_VIDEO);
    STDL_Quit();
    STDL_Init(STDL_INIT_VIDEO);         /* a second init says nothing */
    fprintf(stderr, "BAN: was %d, is %d\n", was, STDL_UseBanner(-1));
    fprintf(stderr, "BAN:done\n");
    STDL_Quit();
    return 0;
}
