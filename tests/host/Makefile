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

#
# -fsanitize=alignment earns its place on a library whose fast paths
# read plane pairs as longs: the host would happily do a misaligned
# 32-bit load, a 68000 takes an address error. It turns "the guard
# that keeps a misaligned surface off the long path is missing" from
# an on-target crash into a host-test failure.

CC      = cc
CFLAGS  = -std=gnu99 -O1 -g -fsanitize=address,alignment \
          -fno-sanitize-recover=alignment -Wall -Wextra \
          -Wno-unused-parameter -Wno-unused-function \
          -I../../include -I../../src

SRC     = ../../src
# indexed.c is in the core set because the source-art converters
# (1bpp, byte-per-pixel, colour remap) build surfaces the other
# suites draw with, not because every program links it - on target
# it is a separate object precisely so that they don't.
CORE    = stubs.c $(SRC)/surface.c $(SRC)/indexed.c $(SRC)/draw.c \
          $(SRC)/blit.c $(SRC)/sprite.c $(SRC)/palette.c \
          $(SRC)/planes.c

TESTS   = test_pixel test_compose test_cursor test_assets test_planes \
          test_footprint test_indexed test_glyph test_vbl

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

test_footprint: test_footprint.c $(CORE)
	$(CC) $(CFLAGS) -o $@ $^

test_indexed: test_indexed.c $(CORE)
	$(CC) $(CFLAGS) -o $@ $^

test_glyph: test_glyph.c $(CORE) $(SRC)/drawchar.c
	$(CC) $(CFLAGS) -o $@ $^

test_vbl: test_vbl.c $(CORE) $(SRC)/vbl.c
	$(CC) $(CFLAGS) -o $@ $^

run: $(TESTS)
	@./test_pixel && ./test_compose && ./test_cursor \
	  && ./test_assets && ./test_planes && ./test_footprint \
	  && ./test_indexed && ./test_glyph && ./test_vbl \
	  && echo "ALL HOST TESTS PASSED"

clean:
	rm -f $(TESTS)
	rm -rf *.dSYM

.PHONY: all run clean
