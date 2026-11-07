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
          src/sprite.c src/tileset.c src/text.c src/shapes.c \
          src/asset.c \
          src/compat.c src/bmp.c \
          src/audio.c src/cursor.c src/music.c src/mixer.c \
          src/sfx.c src/degas.c src/ym.c src/blitter.c \
          src/planes.c src/vbl.c src/indexed.c src/drawchar.c \
          src/surfacefrom.c src/surfacedup.c src/blit8.c \
          src/voice.c $(XPAD)/xpad.c src/stdl_xpad.c src/overscan.c \
          src/hwscroll.c src/tone.c src/cpuspeed.c src/opl.c \
          src/refresh.c src/mouse.c src/stram.c src/wav.c src/ring.c \
          src/palstage.c src/polygon.c src/polypat.c src/raster.c \
          src/spritebatch.c src/responsive.c src/planes8.c src/planes8ex.c \
          src/planes8tile.c src/asset8.c src/tall4.c src/tallst.c \
          src/hz38400.c
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
           dist/HWSCROLL.TOS dist/TONEDEMO.TOS dist/OPLDEMO.TOS \
           dist/MUSICVU.TOS dist/POLYDEMO.TOS dist/PATDEMO.TOS \
           dist/RASTERS.TOS dist/RESPDEMO.TOS dist/OVHOST.TOS

# Pinned, not left to ordering: a rule added above this one would
# silently become the default goal, and `make` would then build
# that one thing, produce nothing else and exit zero. A build that
# appears to work while producing nothing is the hardest kind to
# notice - a port lost a cycle to exactly this.
.DEFAULT_GOAL := all

all: $(LIB) sizecheck libcallcheck ovhostcheck $(EXAMPLES) assets

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
#
# 2026-09-29: blitter.o joined the set, with the ceiling raised by
# its own 740 bytes so the change is neutral - logic moved into the
# driver must not escape the check. And tilesets and text left
# sprite.o for tileset.o and text.o (2744 bytes), so a port that
# draws sprites without them stops paying for them; those two are
# opt-in objects like surfacefrom.o and drawchar.o and are not
# counted here, which is where the room this made came from.
#
# Raised 33740 -> 34700 the same day, deliberately: STDL_BlitSprite on
# the BLiTTER (sprite.o +828 bytes, only in programs that draw
# sprites). Measured on an emulated STE, unshifted sprites averaged
# over phases are 2.4-3.5x faster (32x32 6.5ms -> 1.8ms) and
# pre-shifted ones 1.2-1.7x; on a BLiTTER machine PRESHIFT's 16x RAM
# then buys nothing, which is worth far more than the code to a port
# that pre-shifted to be fast. A plain ST is unchanged within 1%.
#
# Raised 34700 -> 35300 on 2026-11-01: a responsive screen's 8-plane
# surfaces (stdl_responsive.h). Every pixel call takes them through a
# test of `planes` at its entry, 604 bytes over these five objects
# (draw.o 248, surface.o 244, blit.o 68, sprite.o 44), so that a TT
# or a Falcon draws 256 colours through the same calls. Measured on a
# plain ST, the tests cost 0.3% of SPRCOST's and PRIMCOST's geometric
# means (a clipped-out blit 5%, PutPixel and DrawChar 2.9%) against a
# layout floor of 0.03%, and a program 135-178 bytes; the 8-plane code
# itself is linked only by a program that asks.
PIXEL_OBJS = $(addprefix $(OBJDIR)/src/,draw.o blit.o sprite.o \
             surface.o blitter.o)
PIXEL_MAX  = 35300

# The responsive screens' own code (stdl_responsive.h), which only a
# program that asks for an option links: the colour is responsive.o,
# planes8.o, planes8ex.o and planes8tile.o, 25K (1.5K of it the blit
# flags and borrowed 8-plane surfaces, 1.5K the tiles, 0.6K the 256
# colours staged to the blank), a bank's 256-colour art
# (STDL_RESPONSIVE_BANKS) asset8.o, 2.1K more, and the tall screen
# alone responsive.o and tall4.o, 5.1K.
# A title for a 512K ST pays it in free memory as well as on disk, so
# it has a budget of its own: 28,620 bytes on 2026-11-03.
RESP_OBJS  = $(addprefix $(OBJDIR)/src/,responsive.o planes8.o \
             planes8ex.o planes8tile.o asset8.o tall4.o)
RESP_MAX   = 28750

sizecheck: $(PIXEL_OBJS) $(RESP_OBJS)
	@n=`$(CROSS)size $(PIXEL_OBJS) | awk 'NR>1 {t+=$$1} END {print t}'`; \
	 echo "pixel-path text $$n bytes (budget $(PIXEL_MAX))"; \
	 if [ $$n -gt $(PIXEL_MAX) ]; then \
	   echo "*** over the pixel-path size budget"; exit 1; \
	 fi
	@n=`$(CROSS)size $(RESP_OBJS) | awk 'NR>1 {t+=$$1} END {print t}'`; \
	 echo "responsive text $$n bytes (budget $(RESP_MAX))"; \
	 if [ $$n -gt $(RESP_MAX) ]; then \
	   echo "*** over the responsive size budget"; exit 1; \
	 fi

# Calls into libgcc's arithmetic, per function, against an allowlist.
#
# The 68000 multiplies 16 bits by 16 and divides 32 by 16; gcc 4.6
# turns anything it cannot prove fits into a __mulsi3 or __divsi3
# call, 350-630 cycles on a plain ST, and what it can prove changes
# with the code around the expression - a cast that compiles to
# muls.w in one function is a call inside a loop in the next, and
# two bytes multiplied are a call everywhere. Found by profiling, one
# at a time, for months: the ring device's resampler (37% of an STE,
# silent), the BLiTTER's size rule (8% of a tile blit), the music
# tick's volume. A grep cannot see them and neither can the host
# tests, so the cross build lists them and fails on any the
# allowlist does not name. tools/libcalls.sh is the same check for a
# port's own objects.
libcallcheck: $(LIBOBJS)
	@sh tools/libcalls.sh -a tests/libcalls.allow $(LIBOBJS)

# Border overscan for a program that runs the machine itself
# (stdl_overscan.h): one object, compiled from src/overscan_host.c
# alone and linked instead of this archive, by programs that link no
# C library either. Built here as such a program builds it, with
# -Iinclude and nothing else, and the build fails if it needs anything
# but libgcc's arithmetic. One port compiled overscan.c beside its
# own copies of the library's internals before this file existed, and
# a hook overscan.c gained broke its build with nothing here to say
# so; a new hook now fails this check instead, and goes into
# overscan_host.c.
OVHOST_OBJ   = $(OBJDIR)/src/overscan_host.o
OVHOST_ALLOW = ___divsi3 ___mulsi3 ___udivsi3

$(OVHOST_OBJ): src/overscan_host.c
	@mkdir -p $(dir $@)
	$(CC) -O2 -fomit-frame-pointer -std=gnu99 -Wall -Wextra \
	    -Wno-unused-parameter -MMD -MP -Iinclude -c -o $@ $<

ovhostcheck: $(OVHOST_OBJ)
	@u=`$(CROSS)nm -u $(OVHOST_OBJ) | awk '{print $$NF}' \
	    | grep -v -x $(addprefix -e ,$(OVHOST_ALLOW))`; \
	 if [ -n "$$u" ]; then \
	   echo "*** overscan_host.o needs more than libgcc:" $$u; exit 1; \
	 fi; \
	 echo "overscan_host.o needs libgcc only"

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

# for size, in every program asking for the colour: the 8-plane blit
# flags, a rare path (planes8ex.c says why), and the 8-plane tiles,
# whose loops come out the same but for the count (1,524 bytes, 1,808
# at -O2); and a bank's 8-plane art, loaded once (2,088 against 2,456)
SIZE_OBJS = planes8ex planes8tile asset8
$(foreach o,$(SIZE_OBJS),$(OBJDIR)/src/$(o).o $(OBJDIR)/src/$(o).cmini.o): \
    CFLAGS += -Os

# libcmini variant, what a game links unless it needs mintlib
# (docs/building.md): -nostdlib -lcmini instead of mintlib. libstdl.a compiled
# against mintlib headers cannot be mixed with that (different FILE,
# different startup), so build a parallel archive against the libcmini
# headers:
#
#   STCMD_NO_TTY=1 stcmd make libstdl-cmini.a
#   m68k-atari-mint-gcc -nostdlib -L$(CMINI)/lib -Wl,--traditional-format \
#       -o P.TOS $(CMINI)/lib/crt0.o objs... libstdl-cmini.a -lcmini -lgcc
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
# BLITCHK links the counted build of the pixel paths ahead of the
# archive (the archive's own copies are then never pulled in): the
# counters are how it proves each BLiTTER path actually fired, and
# stdl_blit_force is how it reaches the small corner cases the size
# thresholds would keep on the CPU. Everything else is the normal
# library.
STATSOBJS = $(addprefix $(OBJDIR)/stats/src/,blit.o sprite.o draw.o \
            blitter.o)

$(OBJDIR)/stats/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -DSTDL_BLIT_STATS -c -o $@ $<

dist/BLITCHK.TOS: examples/blitchk.c $(STATSOBJS) $(LIB)
	$(CC) $(CFLAGS) -DSTDL_BLIT_STATS -Iinclude/compat -o $@ $< \
	    $(STATSOBJS) $(LIB) && $(STRIP) $@
dist/OVERSCAN.TOS: examples/overscan.c $(LIB)
	$(CC) $(CFLAGS) -o $@ $< $(LIB) && $(STRIP) $@
# no libstdl.a: the host object is all of STDL this program has
dist/OVHOST.TOS: examples/ovhost.c $(OVHOST_OBJ)
	$(CC) $(CFLAGS) -o $@ $< $(OVHOST_OBJ) && $(STRIP) $@
dist/VBLCHK.TOS: examples/vblchk.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude/compat -o $@ $< $(LIB) && $(STRIP) $@
dist/HWSCROLL.TOS: examples/hwscroll.c $(LIB)
	$(CC) $(CFLAGS) -o $@ $< $(LIB) && $(STRIP) $@

dist/TONEDEMO.TOS: examples/tonedemo.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude/compat -o $@ $< $(LIB) && $(STRIP) $@
dist/MUSICVU.TOS: examples/musicvu.c $(LIB)
	$(CC) $(CFLAGS) -o $@ $< $(LIB) && $(STRIP) $@
dist/POLYDEMO.TOS: examples/polydemo.c $(LIB)
	$(CC) $(CFLAGS) -o $@ $< $(LIB) && $(STRIP) $@
dist/PATDEMO.TOS: examples/patdemo.c $(LIB)
	$(CC) $(CFLAGS) -o $@ $< $(LIB) && $(STRIP) $@
dist/RASTERS.TOS: examples/rasters.c $(LIB)
	$(CC) $(CFLAGS) -o $@ $< $(LIB) && $(STRIP) $@
dist/RESPDEMO.TOS: examples/respdemo.c $(LIB)
	$(CC) $(CFLAGS) -o $@ $< $(LIB) && $(STRIP) $@
dist/OPLDEMO.TOS: examples/opldemo.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude/compat -o $@ $< $(LIB) && $(STRIP) $@

dist/CHUNKY.TOS: examples/chunky.c $(LIB)
	$(CC) $(CFLAGS) -Iinclude/compat -o $@ $< $(LIB) && $(STRIP) $@

# The generated header dependencies, if any objects exist yet.
-include $(LIBOBJS:.o=.d) $(CMINIOBJS:.o=.d) $(STATSOBJS:.o=.d) \
         $(OVHOST_OBJ:.o=.d)

# The SDL2 add-on's targets (`make sdl2`), which nothing above builds:
# the core neither includes nor links it.
include addons/sdl2/sdl2.mk

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
	$(MAKE) -C addons/sdl2/tests run CC=clang
	$(STCMD) make
	$(STCMD) make cmini
	$(STCMD) make sdl2
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
	cp $(EXAMPLES) $(SDL2EXAMPLES) examples/assets/* $(OBJDIR)/bundle/STDL/
	cp examples/LICENSE $(OBJDIR)/bundle/STDL/LICENSE.TXT
	cd $(OBJDIR)/bundle && zip -qr ../../$(BUNDLE) STDL
	@echo "$(BUNDLE): `ls $(OBJDIR)/bundle/STDL | wc -l | tr -d ' '` files"

clean:
	rm -rf $(OBJDIR)
	rm -f $(CMINILIB) $(LIB) $(EXAMPLES) $(BUNDLE)
	rm -f $(SDL2LIB) $(SDL2CMINILIB) $(SDL2EXAMPLES)
	$(MAKE) -C tests/host clean
	$(MAKE) -C addons/sdl2/tests clean

# Run an example in Hatari (host-side): make run-TSPRITE
run-%:
	hatari --machine ste --memsize 4 --fast-boot on dist/$*.TOS

.PHONY: sizecheck libcallcheck ovhostcheck all clean assets test cmini bundle ci
