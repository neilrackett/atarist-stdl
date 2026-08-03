# STDL - Planar Display Library for Atari ST
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Host-side unit tests: the pure pixel-path modules compiled
# natively with AddressSanitizer against per-pixel reference
# models. Run these before any emulator debugging:
#
#   make -C tests/host run     (or `make test` from the root)
#
# The BLiTTER, YM, DMA and IKBD paths are hardware and cannot be
# tested here - dist/BLITCHK.TOS and the Hatari harness cover them.

CC      = cc
CFLAGS  = -std=gnu99 -O1 -g -fsanitize=address -Wall -Wextra \
          -Wno-unused-parameter -Wno-unused-function \
          -I../../include -I../../src

SRC     = ../../src
CORE    = stubs.c $(SRC)/surface.c $(SRC)/draw.c $(SRC)/blit.c \
          $(SRC)/sprite.c $(SRC)/palette.c $(SRC)/planes.c

TESTS   = test_pixel test_compose test_cursor test_assets test_planes

all: $(TESTS)

test_pixel: test_pixel.c $(CORE)
	$(CC) $(CFLAGS) -o $@ $^

test_compose: test_compose.c $(CORE) $(SRC)/bmp.c $(SRC)/degas.c \
              $(SRC)/asset.c
	$(CC) $(CFLAGS) -o $@ $^

test_cursor: test_cursor.c $(CORE) $(SRC)/cursor.c
	$(CC) $(CFLAGS) -o $@ $^

test_assets: test_assets.c $(CORE) $(SRC)/bmp.c $(SRC)/asset.c
	$(CC) $(CFLAGS) -o $@ $^

test_planes: test_planes.c $(CORE)
	$(CC) $(CFLAGS) -o $@ $^

run: $(TESTS)
	@./test_pixel && ./test_compose && ./test_cursor \
	  && ./test_assets && ./test_planes \
	  && echo "ALL HOST TESTS PASSED"

clean:
	rm -f $(TESTS)
	rm -rf *.dSYM

.PHONY: all run clean
