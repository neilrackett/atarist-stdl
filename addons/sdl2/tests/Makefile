# STDL - Planar Display Library for Atari ST
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# The SDL2 add-on's host tests: native cc with ASan, as the core's
# tests/host are, and run by `make ci` beside them. The add-on's own
# include path only - never include/compat, whose SDL 1.2 SDL.h it
# must not meet.

CC      = cc
# -fno-common, as tests/host: two definitions of a variable fail here
# as they do on the Linux runner
CFLAGS  = -std=gnu99 -O1 -g -fsanitize=address,alignment \
          -fno-sanitize-recover=alignment -Wall -Wextra \
          -Wno-unused-parameter -Wno-unused-function -fno-common \
          -I../include/SDL2 -I../src -I../../../include

SRC     = ../src
HDRS    = $(wildcard ../include/SDL2/*.h) $(SRC)/internal.h

TESTS   = test_basics test_c2p test_render test_events test_surface \
          test_audio test_bmp

# the renderer on fake_stdl.c's screens: the add-on as a program links
# it, STDL's calls stood in for
RENDER  = $(SRC)/init.c $(SRC)/error.c $(SRC)/hints.c $(SRC)/c2p.c \
          $(SRC)/c2pany.c $(SRC)/palette.c $(SRC)/timer.c $(SRC)/video.c

# and the rest of the add-on, on fake_stdl.c's input
REST    = $(SRC)/events.c $(SRC)/keymap.c $(SRC)/joystick.c \
          $(SRC)/surface.c $(SRC)/rwops.c $(SRC)/log.c $(SRC)/audio.c \
          $(SRC)/audiomix.c $(SRC)/wav.c $(SRC)/bmp.c

test_basics: test_basics.c $(SRC)/init.c $(SRC)/error.c $(SRC)/hints.c \
             $(HDRS)
	$(CC) $(CFLAGS) -o $@ test_basics.c $(SRC)/init.c $(SRC)/error.c \
	    $(SRC)/hints.c

test_c2p: test_c2p.c $(SRC)/c2p.c $(SRC)/c2p.h
	$(CC) $(CFLAGS) -o $@ test_c2p.c $(SRC)/c2p.c

test_render: test_render.c fake_stdl.c fake_stdl.h $(RENDER) $(HDRS) \
             $(SRC)/c2p.h
	$(CC) $(CFLAGS) -o $@ test_render.c fake_stdl.c $(RENDER)

test_events: test_events.c fake_stdl.c fake_stdl.h $(RENDER) $(REST) $(HDRS)
	$(CC) $(CFLAGS) -o $@ test_events.c fake_stdl.c $(RENDER) $(REST)

test_audio: test_audio.c fake_stdl.c fake_stdl.h $(RENDER) $(REST) $(HDRS)
	$(CC) $(CFLAGS) -o $@ test_audio.c fake_stdl.c $(RENDER) $(REST)

test_surface: test_surface.c fake_stdl.c fake_stdl.h $(RENDER) $(REST) \
              $(HDRS)
	$(CC) $(CFLAGS) -o $@ test_surface.c fake_stdl.c $(RENDER) $(REST)

# with bmp8conv.py's files, made by stdlconv bmp8 (Pillow, on the host)
test_bmp: test_bmp.c fake_stdl.c fake_stdl.h $(RENDER) $(REST) $(HDRS)
	$(CC) $(CFLAGS) -o $@ test_bmp.c fake_stdl.c $(RENDER) $(REST)

run: $(TESTS)
	@set -e; for t in $(TESTS); do ./$$t; done; \
	  echo "ALL SDL2 ADD-ON HOST TESTS PASSED"

clean:
	rm -f $(TESTS) test_rw.tmp test_log.tmp test_wav.tmp test_bmp.tmp
	rm -rf test_bmp.dir
	rm -rf *.dSYM

.PHONY: run clean
