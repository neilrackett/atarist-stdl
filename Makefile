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

# Xpad is a submodule, not a vendored copy: the ABI is still moving and
# a stale xpad.h still compiles, so drift would be silent. Update it
# deliberately with `git submodule update --remote lib/xpad` and read
# the diff.
XPAD    = lib/xpad/src

# -MMD -MP: write a .d beside each .o listing the headers it used,
# so editing one rebuilds what included it. Without this a change to
# stdl_internal.h left every object stale and `sizecheck` measured
# the previous build - it read 32 bytes under a clean build of the
# same commit, which is a gate reporting on code that is not there.
CFLAGS  = -O2 -fomit-frame-pointer -std=gnu99 -Wall -Wextra \
          -Wno-unused-parameter -MMD -MP \
          -Iinclude -Iinclude/compat -I$(XPAD)

LIB     = libstdl.a

LIBSRCS = src/video.c src/surface.c src/draw.c src/blit.c \
          src/palette.c src/event.c src/time.c src/dirty.c \
          src/sprite.c src/asset.c src/compat.c src/bmp.c \
          src/audio.c src/cursor.c src/music.c src/mixer.c \
          src/sfx.c src/degas.c src/ym.c src/blitter.c \
          src/planes.c src/vbl.c src/indexed.c src/drawchar.c \
          src/surfacefrom.c src/blit8.c src/voice.c \
          $(XPAD)/xpad.c src/stdl_xpad.c src/overscan.c \
          src/hwscroll.c src/tone.c src/cpuspeed.c src/opl.c \
          src/refresh.c src/mouse.c src/stram.c
# Objects live under obj/, mirroring each source's own path. Sources
# now come from two places, this repo and the xpad submodule, and
# building beside the source would drop .o files inside lib/xpad. The
# path is mirrored rather than flattened so two sources with the same
# basename cannot collide.
OBJDIR  = obj
LIBOBJS = $(patsubst %.c,$(OBJDIR)/%.o,$(LIBSRCS))

# Examples: ported SDL 1.2 test programs (public domain).
# GEMDOS needs 8.3 filenames.
EXAMPLES = dist/TBITMAP.TOS dist/GRAYWIN.TOS dist/TESTWIN.TOS \
           dist/TSPRITE.TOS dist/TPALETTE.TOS dist/CHECKKEY.TOS \
           dist/CHUNKY.TOS \
           dist/TTIMER.TOS dist/TBLITSPD.TOS dist/TVIDINFO.TOS \
           dist/TKEYS.TOS dist/TJOY.TOS dist/LOOPWAVE.TOS \
           dist/TCURSOR.TOS dist/PLAYMUS.TOS dist/SFXDEMO.TOS \
           dist/BLITCHK.TOS dist/VBLCHK.TOS dist/OVERSCAN.TOS \
           dist/HWSCROLL.TOS dist/TONEDEMO.TOS dist/OPLDEMO.TOS

# Pinned, not left to ordering: a rule added above this one would
# silently become the default goal, and `make` would then build
# that one thing, produce nothing else and exit zero. A build that
# appears to work while producing nothing is the hardest kind to
# notice - a port lost a cycle to exactly this.
.DEFAULT_GOAL := all

all: $(LIB) sizecheck $(EXAMPLES) assets

# Size budget for the pixel-path objects.
#
# The archive links whole objects into programs that run on 512K-1M
# machines, so library text is a resource with an owner, not a
# by-product. The plane budget once instantiated every hot loop four
# times through STDL_PLANE_DISPATCH; three of the four copies are
# unreachable at the default budget and they put 12K of dead code
# into every port, which is what stopped FreeNukum fitting in 1M.
# Nothing in tests/host can see that - it is native code there - so
# the build measures it instead. Raise the ceiling deliberately,
# with a number, or not at all.
#
# Raised 26000 -> 33000 on 2026-09-15, deliberately and with the
# numbers. STDL_FLAG_DISPATCH specialises the blit row loops on the
# two composition flags as well as the plane count, which costs 6548
# bytes and buys 19% on ordinary masked blits, 18% on UNDER, 13% on
# MARK and 7% on both - measured on a plain ST, 400 masked 64x16
# blits, mask reset between runs. UNDER is not a niche: one port
# sets it on essentially every gameplay sprite. The 1M fit that set
# the old ceiling was a target of its own rather than a requirement,
# and the port that achieved it stays pinned to the version that
# did.
PIXEL_OBJS = $(addprefix $(OBJDIR)/src/,draw.o blit.o sprite.o surface.o)
PIXEL_MAX  = 33000

sizecheck: $(PIXEL_OBJS)
	@n=`$(CROSS)size $(PIXEL_OBJS) | awk 'NR>1 {t+=$$1} END {print t}'`; \
	 echo "pixel-path text $$n bytes (budget $(PIXEL_MAX))"; \
	 if [ $$n -gt $(PIXEL_MAX) ]; then \
	   echo "*** over the pixel-path size budget"; exit 1; \
	 fi

# runtime assets the examples load (tracked in examples/assets/,
# regenerable via tools/mkdemo.py and tools/stdlconv/stdlconv.py)
assets: | dist
	cp -f examples/assets/* dist/

$(EXAMPLES): | dist

dist:
	mkdir -p dist

$(LIB): $(LIBOBJS)
	$(AR) rcs $@ $(LIBOBJS)

$(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
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
CMINIOBJS    = $(patsubst %.c,$(OBJDIR)/%.cmini.o,$(LIBSRCS))

cmini: $(CMINILIB)

$(CMINILIB): $(CMINIOBJS)
	$(AR) rcs $@ $(CMINIOBJS)

$(OBJDIR)/%.cmini.o: %.c
	@mkdir -p $(dir $@)
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
dist/OVERSCAN.TOS: examples/overscan.c $(LIB)
	$(CC) $(CFLAGS) -o $@ $< $(LIB) && $(STRIP) $@
dist/VBLCHK.TOS: examples/vblchk.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude/compat -o $@ $< $(LIB) && $(STRIP) $@
dist/HWSCROLL.TOS: examples/hwscroll.c $(LIB)
	$(CC) $(CFLAGS) -o $@ $< $(LIB) && $(STRIP) $@

dist/TONEDEMO.TOS: examples/tonedemo.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude/compat -o $@ $< $(LIB) && $(STRIP) $@
dist/OPLDEMO.TOS: examples/opldemo.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude/compat -o $@ $< $(LIB) && $(STRIP) $@

dist/CHUNKY.TOS: examples/chunky.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude/compat -o $@ $< $(LIB) && $(STRIP) $@

# The generated header dependencies, if any objects exist yet.
-include $(LIBOBJS:.o=.d) $(CMINIOBJS:.o=.d)

# host-side unit tests: native clang + ASan, no cross toolchain
# needed (run on the host, not through stcmd)
test:
	$(MAKE) -C tests/host run

# Everything CI runs, in the order it runs it, as one command - so
# the workflows do not carry a second copy of the build recipe and a
# developer can reproduce a CI failure with `make ci`. Host-side: it
# shells out to stcmd itself, so do not run it inside the container.
# clang for the host tests because that is what the library is
# developed against; gcc passes too.
STCMD ?= STCMD_NO_TTY=1 stcmd

ci:
	$(MAKE) -C tests/host run CC=clang
	$(STCMD) make
	$(STCMD) make cmini
	$(MAKE) bundle

# The example binaries and the assets they load, in one zip for the
# release page - the examples are how a port author sees an API
# working, so they are only useful if they are downloadable. Built
# from the EXAMPLES list and examples/assets/ rather than from
# whatever dist/ happens to hold, so a hand-built probe left on the
# test drive cannot ride along. Host-side, and it compiles nothing:
# run `stcmd make` first.
BUNDLE = stdl-examples.zip

bundle:
	rm -rf $(OBJDIR)/bundle $(BUNDLE)
	mkdir -p $(OBJDIR)/bundle/STDL
	cp $(EXAMPLES) examples/assets/* $(OBJDIR)/bundle/STDL/
	cp examples/LICENSE $(OBJDIR)/bundle/STDL/LICENSE.TXT
	cd $(OBJDIR)/bundle && zip -qr ../../$(BUNDLE) STDL
	@echo "$(BUNDLE): `ls $(OBJDIR)/bundle/STDL | wc -l | tr -d ' '` files"

clean:
	rm -rf $(OBJDIR)
	rm -f $(CMINILIB) $(LIB) $(EXAMPLES) $(BUNDLE)
	$(MAKE) -C tests/host clean

# Run an example in Hatari (host-side): make run-TSPRITE
run-%:
	hatari --machine ste --memsize 4 --fast-boot on dist/$*.TOS

.PHONY: sizecheck all clean assets test cmini bundle ci
