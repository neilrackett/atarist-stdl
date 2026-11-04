# STDL - Planar Display Library for Atari ST
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# The SDL2 add-on's host tests: native cc with ASan, as the core's
# tests/host are, and run by `make ci` beside them. The add-on's own
# include path only - never include/compat, whose SDL 1.2 SDL.h it
# must not meet.

CC      = cc
CFLAGS  = -std=gnu99 -O1 -g -fsanitize=address,alignment \
          -fno-sanitize-recover=alignment -Wall -Wextra \
          -Wno-unused-parameter -Wno-unused-function \
          -I../include/SDL2 -I../src -I../../../include

SRC     = ../src
HDRS    = $(wildcard ../include/SDL2/*.h) $(SRC)/internal.h

TESTS   = test_basics

test_basics: test_basics.c $(SRC)/init.c $(SRC)/error.c $(SRC)/hints.c \
             $(HDRS)
	$(CC) $(CFLAGS) -o $@ test_basics.c $(SRC)/init.c $(SRC)/error.c \
	    $(SRC)/hints.c

run: $(TESTS)
	@set -e; for t in $(TESTS); do ./$$t; done; \
	  echo "ALL SDL2 ADD-ON HOST TESTS PASSED"

clean:
	rm -f $(TESTS)
	rm -rf *.dSYM

.PHONY: run clean
