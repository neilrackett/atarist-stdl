# STDL - Planar Display Library for Atari ST
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# The 68020's own multiply and divide for a 68000 program
# (docs/building.md says what it does and costs): a program's Makefile
# includes this file and puts $(STDL_ARITH020) on its link line before
# the libraries, libcmini or mintlib alike:
#
#   include $(STDL)/arith020.mk
#   GAME.TOS: $(OBJS) $(STDL_LIB)
#   	$(CC) ... -o $@ $(OBJS) $(STDL_ARITH020) $(STDL_LIB) -lcmini -lgcc
#
# It is the object libstdl-arith020.o beside this file (`make` builds
# it; the release page has it) and ld's --wrap for each of libgcc's
# routines it answers.

STDL_ARITH020_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
STDL_ARITH020 = $(STDL_ARITH020_DIR)libstdl-arith020.o \
                -Wl,--wrap=__mulsi3 -Wl,--wrap=__udivsi3 \
                -Wl,--wrap=__divsi3 -Wl,--wrap=__umodsi3 \
                -Wl,--wrap=__modsi3 -Wl,--wrap=__muldi3 \
                -Wl,--wrap=__divdi3
