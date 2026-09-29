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
# The BLiTTER paths run here against a software model of the chip
# (blitmodel.c, driven through blitter.c's host registers), so an
# overread is an ASan failure rather than a silent fetch on target;
# dist/BLITCHK.TOS still checks the real chip. The DMA and IKBD
# paths are hardware and are covered by the Hatari harness. The YM
# service runs against a register-file stand-in (stubs.c), so the
# sound tick's bookkeeping is covered natively.

#
# -fsanitize=alignment earns its place on a library whose fast paths
# read plane pairs as longs. Plane memory is accessed through
# stdl_wlong (stdl_internal.h), aligned to two as a 68000 long is, so
# the sanitizer checks the target's rule: an odd address, the one a
# 68000 faults on, fails here instead of crashing a machine.

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
CORE    = stubs.c blitmodel.c $(SRC)/surface.c $(SRC)/indexed.c \
          $(SRC)/draw.c $(SRC)/blit.c $(SRC)/sprite.c $(SRC)/tileset.c \
          $(SRC)/text.c $(SRC)/shapes.c $(SRC)/palette.c \
          $(SRC)/planes.c $(SRC)/blitter.c

TESTS   = test_pixel test_compose test_cursor test_assets test_planes \
          test_footprint test_indexed test_glyph test_vbl test_blit8 \
          test_underleak test_blitter test_dirty \
          test_voice test_ym test_tone test_sfx test_opl test_refresh \
          test_wav test_mixer test_ring

all: $(TESTS)

# every suite compiles library sources against these; without them a
# header-only change (a macro, a threshold) leaves the binaries stale
# and the suite passes on the old code
HDRS    = $(wildcard $(SRC)/*.h ../../include/stdl/*.h \
                     ../../include/compat/*.h *.h)

# one recipe: a suite is its own .c, the core, and the extra sources
# listed for it below
$(TESTS): %: %.c $(CORE) $(HDRS)
	$(CC) $(CFLAGS) $(XFLAGS) -o $@ $(filter %.c,$^)

test_pixel:     $(SRC)/surfacefrom.c $(SRC)/drawchar.c
test_dirty:     $(SRC)/dirty.c
# built with the counters, for stdl_blit_force: every operation the
# BLiTTER could take goes to it, whatever its size
test_blitter:   XFLAGS = -DSTDL_BLIT_STATS
test_compose:   $(SRC)/bmp.c $(SRC)/degas.c $(SRC)/asset.c
test_cursor:    $(SRC)/cursor.c
test_assets:    $(SRC)/bmp.c $(SRC)/asset.c $(SRC)/degas.c
test_glyph:     $(SRC)/drawchar.c
test_underleak: $(SRC)/surfacefrom.c
test_vbl:       $(SRC)/vbl.c
test_blit8:     $(SRC)/blit8.c $(SRC)/surfacefrom.c
test_voice:     $(SRC)/voice.c $(SRC)/vbl.c
test_refresh:   $(SRC)/refresh.c $(SRC)/music.c $(SRC)/ym.c $(SRC)/asset.c
test_ym:        $(SRC)/ym.c
test_tone:      $(SRC)/ym.c $(SRC)/tone.c $(SRC)/sfx.c
test_wav:       $(SRC)/wav.c $(SRC)/asset.c
# the SDL_mixer layer on the voice mixer, through its own API
test_mixer:     XFLAGS = -I../../include/compat
test_mixer:     $(SRC)/mixer.c $(SRC)/voice.c $(SRC)/vbl.c $(SRC)/wav.c \
                $(SRC)/music.c $(SRC)/ym.c $(SRC)/asset.c
# the ring device's refill, both paths, pumped by hand
test_ring:      $(SRC)/ring.c $(SRC)/wav.c $(SRC)/asset.c
test_sfx:       $(SRC)/ym.c $(SRC)/sfx.c
test_opl:       $(SRC)/ym.c $(SRC)/tone.c $(SRC)/sfx.c $(SRC)/opl.c

# Runs every binary in TESTS rather than a second hand-kept list:
# a test added to TESTS alone still built, still linked and still
# reported nothing, which is the "test that cannot fail" this suite
# exists to catch, one level up. Caught while adding test_refresh.
run: $(TESTS)
	@set -e; for t in $(TESTS); do ./$$t; done; \
	  echo "ALL HOST TESTS PASSED"

clean:
	rm -f $(TESTS)
	rm -rf *.dSYM

.PHONY: all run clean
