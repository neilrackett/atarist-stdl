# STDL - Planar Display Library for Atari ST
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Cross-compile with m68k-atari-mint-gcc via the toolkit container:
#   STCMD_NO_TTY=1 stcmd make
# Host-side targets (run without stcmd): run-<example>, clean

CROSS   = m68k-atari-mint-
CC      = $(CROSS)gcc
AR      = $(CROSS)ar
STRIP   = $(CROSS)strip

CFLAGS  = -O2 -fomit-frame-pointer -std=gnu99 -Wall -Wextra \
          -Wno-unused-parameter -Iinclude -Iinclude/compat

LIB     = libstdl.a

LIBSRCS = src/video.c src/surface.c src/draw.c src/blit.c \
          src/palette.c src/event.c src/time.c src/dirty.c \
          src/sprite.c src/asset.c src/compat.c src/bmp.c \
          src/audio.c src/cursor.c src/music.c src/mixer.c \
          src/sfx.c src/degas.c src/ym.c src/blitter.c \
          src/planes.c
LIBOBJS = $(LIBSRCS:.c=.o)

# Examples: ported SDL 1.2 test programs (public domain).
# GEMDOS needs 8.3 filenames.
EXAMPLES = dist/TBITMAP.TOS dist/GRAYWIN.TOS dist/TESTWIN.TOS \
           dist/TSPRITE.TOS dist/TPALETTE.TOS dist/CHECKKEY.TOS \
           dist/TTIMER.TOS dist/TBLITSPD.TOS dist/TVIDINFO.TOS \
           dist/TKEYS.TOS dist/TJOY.TOS dist/LOOPWAVE.TOS \
           dist/TCURSOR.TOS dist/PLAYMUS.TOS dist/SFXDEMO.TOS \
           dist/BLITCHK.TOS

all: $(LIB) $(EXAMPLES) assets

# runtime assets the examples load (tracked in examples/assets/,
# regenerable via tools/mkdemo.py and tools/stdlconv/stdlconv.py)
assets: | dist
	cp -f examples/assets/* dist/

$(EXAMPLES): | dist

dist:
	mkdir -p dist

$(LIB): $(LIBOBJS)
	$(AR) rcs $@ $(LIBOBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# libcmini variant. Games that have to fit a stock 520ST link with
# -nostdlib -lcmini instead of mintlib; libstdl.a compiled against
# mintlib headers cannot be mixed with that (different FILE, different
# startup), so build a parallel archive against the libcmini headers:
#
#   STCMD_NO_TTY=1 stcmd make libstdl-cmini.a
#   m68k-atari-mint-gcc -nostdlib -L$(CMINI)/lib -o P.TOS \
#       $(CMINI)/lib/crt0.o objs... libstdl-cmini.a -lcmini -lgcc
#
# STDL only needs stdio/stdlib/string/ctype plus mint/osbind.h, all of
# which libcmini provides. Note libcmini's stack is fixed by _stksize
# in the program, and its malloc comes from Mxalloc.
CMINI       ?= /freemint/libcmini
CMINILIB     = libstdl-cmini.a
CMINIOBJS    = $(LIBSRCS:.c=.cmini.o)

cmini: $(CMINILIB)

$(CMINILIB): $(CMINIOBJS)
	$(AR) rcs $@ $(CMINIOBJS)

%.cmini.o: %.c
	$(CC) $(CFLAGS) -I$(CMINI)/include -c -o $@ $<

dist/TBITMAP.TOS: examples/testbitmap.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude/compat -o $@ $< $(LIB) && $(STRIP) $@
dist/GRAYWIN.TOS: examples/graywin.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude/compat -DTEST_VGA16 -o $@ $< $(LIB) && $(STRIP) $@
dist/TESTWIN.TOS: examples/testwin.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude/compat -o $@ $< $(LIB) && $(STRIP) $@
dist/TSPRITE.TOS: examples/testsprite.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude/compat -o $@ $< $(LIB) -lm && $(STRIP) $@
dist/TPALETTE.TOS: examples/testpalette.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude/compat -o $@ $< $(LIB) -lm && $(STRIP) $@
dist/CHECKKEY.TOS: examples/checkkeys.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude/compat -o $@ $< $(LIB) && $(STRIP) $@
dist/TTIMER.TOS: examples/testtimer.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude/compat -o $@ $< $(LIB) && $(STRIP) $@
dist/TBLITSPD.TOS: examples/testblitspeed.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude/compat -o $@ $< $(LIB) && $(STRIP) $@
dist/TVIDINFO.TOS: examples/testvidinfo.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude/compat -o $@ $< $(LIB) && $(STRIP) $@
dist/TKEYS.TOS: examples/testkeys.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude/compat -o $@ $< $(LIB) && $(STRIP) $@
dist/TJOY.TOS: examples/testjoystick.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude/compat -o $@ $< $(LIB) && $(STRIP) $@
dist/LOOPWAVE.TOS: examples/loopwave.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude/compat -o $@ $< $(LIB) && $(STRIP) $@
dist/TCURSOR.TOS: examples/testcursor.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude/compat -o $@ $< $(LIB) && $(STRIP) $@
dist/PLAYMUS.TOS: examples/playmus.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude/compat -o $@ $< $(LIB) && $(STRIP) $@
dist/SFXDEMO.TOS: examples/sfxdemo.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude/compat -o $@ $< $(LIB) && $(STRIP) $@
dist/BLITCHK.TOS: examples/blitchk.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude/compat -o $@ $< $(LIB) && $(STRIP) $@

# host-side unit tests: native clang + ASan, no cross toolchain
# needed (run on the host, not through stcmd)
test:
	$(MAKE) -C tests/host run

clean:
	rm -f $(CMINIOBJS) $(CMINILIB)
	rm -f $(LIBOBJS) $(LIB) $(EXAMPLES)
	$(MAKE) -C tests/host clean

# Run an example in Hatari (host-side): make run-TSPRITE
run-%:
	hatari --machine megaste --memsize 4 --fast-boot on dist/$*.TOS

.PHONY: all clean assets test cmini
