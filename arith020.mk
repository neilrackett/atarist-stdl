# STDL - Planar Display Library for Atari ST
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# The 68020's own multiply and divide for a 68000 program
# (src/arith020.c, docs/building.md): a program's Makefile includes
# this file and adds $(STDL_ARITH020) to its link, which takes
# libstdl.a or libstdl-cmini.a as before:
#
#   include $(STDL)/arith020.mk
#   GAME.TOS: $(OBJS) $(STDL_LIB)
#   	$(CC) ... -o $@ $(OBJS) $(STDL_LIB) -lcmini -lgcc $(STDL_ARITH020)
#
# Every call of libgcc's __mulsi3, __udivsi3, __divsi3, __umodsi3,
# __modsi3, __muldi3 and __divdi3 in the program - its own, STDL's, the
# C library's and libgcc's - then goes through the archive's arith020.o,
# which answers with muls.l, divu.l and the rest on a 68020 or later
# (a TT, a Falcon), libgcc's answer to the bit, and on a 68000 goes on
# to libgcc's own. The -u pulls arith020.o in whatever the program's
# own objects call: the C library is linked after the archive, and its
# calls (printf's divides) would otherwise find nothing to wrap them.
#
# The same flags for libcmini and mintlib. A program linked without
# them links none of arith020.o.

STDL_ARITH020 = -Wl,--wrap=__mulsi3 -Wl,--wrap=__udivsi3 \
                -Wl,--wrap=__divsi3 -Wl,--wrap=__umodsi3 \
                -Wl,--wrap=__modsi3 -Wl,--wrap=__muldi3 \
                -Wl,--wrap=__divdi3 -Wl,-u,___wrap___mulsi3
