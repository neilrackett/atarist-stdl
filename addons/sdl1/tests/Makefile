# STDL - Planar Display Library for Atari ST
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# The SDL1 add-on's host test: native cc with ASan, as the SDL2 add-on's
# are, on its stand-in for STDL (addons/sdl2/tests/fake_stdl.c) and the
# SDL2 add-on itself. Two groups of objects, as in the cross build: the
# test and video.c see the SDL1 add-on's SDL.h, bridge.c and the SDL2
# add-on's sources see the SDL2 add-on's, and no file sees both.

CC      = cc
CFLAGS  = -std=gnu99 -O1 -g -fsanitize=address,alignment \
          -fno-sanitize-recover=alignment -Wall -Wextra \
          -Wno-unused-parameter -Wno-unused-function -fno-common
# from this directory, whichever one the objects are made in
T       = $(CURDIR)
A       = $(CURDIR)/..
S2      = $(CURDIR)/../../sdl2/src
INC1    = -I$(A)/include/SDL1 -I$(A)/../../include
INC2    = -I$(A)/../sdl2/include/SDL2 -I$(S2) -I$(A)/../../include

SDL2    = $(S2)/init.c $(S2)/error.c $(S2)/hints.c $(S2)/c2p.c \
          $(S2)/c2pany.c $(S2)/c2pp.c $(S2)/c2ppair.c $(S2)/palette.c \
          $(S2)/timer.c $(S2)/video.c $(S2)/events.c $(S2)/keymap.c \
          $(S2)/joystick.c $(S2)/surface.c $(S2)/rwops.c $(S2)/log.c \
          $(S2)/audio.c $(S2)/audiomix.c $(S2)/wav.c $(S2)/bmp.c
SIDE1   = $(T)/test_sdl1.c $(A)/src/video.c
SIDE2   = $(A)/src/bridge.c $(T)/sdl2_side.c $(A)/../sdl2/tests/fake_stdl.c
DEPS    = test_sdl1.c sdl2_side.c ../src/video.c ../src/bridge.c \
          ../src/bridge.h $(wildcard ../include/SDL1/*.h) \
          $(wildcard ../../sdl2/src/*.[ch]) ../../sdl2/tests/fake_stdl.c \
          $(wildcard ../../../include/compat/*.h)

TESTS   = test_sdl1 test_sdl1_st

# the SDL2 add-on built for a TT and a Falcon, whose pages the test
# decodes
test_sdl1: $(DEPS)
	rm -rf obj && mkdir -p obj/1 obj/2
	cd obj/1 && $(CC) $(CFLAGS) $(INC1) -c $(SIDE1)
	cd obj/2 && $(CC) $(CFLAGS) $(INC2) -c $(SIDE2) $(SDL2)
	$(CC) $(CFLAGS) -o $@ obj/1/*.o obj/2/*.o

# and its experimental 68000 build, on an ST's screens and then on those
test_sdl1_st: $(DEPS)
	rm -rf objst && mkdir -p objst/1 objst/2
	cd objst/1 && $(CC) $(CFLAGS) $(INC1) -DSDL1_TEST_ST -c $(SIDE1)
	cd objst/2 && $(CC) $(CFLAGS) $(INC2) -DSTDL_SDL2_ST -c $(SIDE2) \
	    $(SDL2) $(S2)/st.c
	$(CC) $(CFLAGS) -o $@ objst/1/*.o objst/2/*.o

run: $(TESTS)
	@set -e; for t in $(TESTS); do ./$$t; done; \
	  echo "ALL SDL1 ADD-ON HOST TESTS PASSED"

clean:
	rm -f $(TESTS)
	rm -rf obj objst *.dSYM

.PHONY: run clean
