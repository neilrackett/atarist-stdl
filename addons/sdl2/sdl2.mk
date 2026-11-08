# STDL - Planar Display Library for Atari ST
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# The SDL2 add-on (addons/sdl2): an SDL2-shaped library for a TT or a
# Falcon, apart from the core. Included by the root Makefile for its
# targets alone - `all`, libstdl.a and libstdl-cmini.a never build or
# name it - and a program links it only by asking:
#
#   STCMD_NO_TTY=1 stcmd make sdl2      # libstdl-sdl2.a, -cmini.a
#   m68k-atari-mint-gcc -I<stdl>/addons/sdl2/include/SDL2 \
#       -I<stdl>/include ... prog.o <stdl>/libstdl-sdl2.a <stdl>/libstdl.a
#
# Its objects build from these static pattern rules, never from the
# core's $(OBJDIR)/%.o, whose CFLAGS carry -Iinclude/compat: the 1.2
# shim's SDL.h would be found ahead of the add-on's own.

SDL2DIR      = addons/sdl2
SDL2LIB      = libstdl-sdl2.a
SDL2CMINILIB = libstdl-sdl2-cmini.a

SDL2SRCS      = $(SDL2DIR)/src/init.c $(SDL2DIR)/src/error.c \
                $(SDL2DIR)/src/hints.c $(SDL2DIR)/src/c2p.c \
                $(SDL2DIR)/src/c2pany.c $(SDL2DIR)/src/palette.c \
                $(SDL2DIR)/src/timer.c $(SDL2DIR)/src/video.c \
                $(SDL2DIR)/src/events.c $(SDL2DIR)/src/keymap.c \
                $(SDL2DIR)/src/joystick.c $(SDL2DIR)/src/surface.c \
                $(SDL2DIR)/src/rwops.c $(SDL2DIR)/src/log.c \
                $(SDL2DIR)/src/audio.c $(SDL2DIR)/src/audiomix.c \
                $(SDL2DIR)/src/wav.c $(SDL2DIR)/src/bmp.c
SDL2OBJS      = $(patsubst %.c,$(OBJDIR)/%.o,$(SDL2SRCS))
SDL2CMINIOBJS = $(patsubst %.c,$(OBJDIR)/%.cmini.o,$(SDL2SRCS))

# The conversion's loops for each fixed border index X, one object an
# X (c2px.c, which says why): a program naming its X as a constant
# links the one it names.
SDL2_XS        = 10 20 30 40 50 60 70 80 90 a0 b0 c0 d0 e0 f0
SDL2XOBJS      = $(foreach x,$(SDL2_XS),$(OBJDIR)/$(SDL2DIR)/src/c2px$(x).o)
SDL2XCMINIOBJS = $(SDL2XOBJS:.o=.cmini.o)

SDL2_WARN   = -Wall -Wextra -Wno-unused-parameter
SDL2_INC    = -Iinclude -I$(SDL2DIR)/include/SDL2 -I$(SDL2DIR)/src
SDL2_CFLAGS = -O2 -fomit-frame-pointer -std=gnu99 $(SDL2_WARN) -MMD -MP \
              $(SDL2_INC)

# The add-on draws for a TT's or a Falcon's 68030, so its drawing is
# built for the 68020 without FPU instructions (a Falcon has no FPU),
# linking against the toolchain's 68000 libraries as any program
# does. SDL_Init and what it calls are built for the 68000: an SDL2
# program started on an ST reaches SDL_Init first, and gets an error
# it can print rather than an illegal instruction.
SDL2_CPU    = -m68020-60 -msoft-float
SDL2_CPU000 = init error hints
$(foreach o,$(SDL2_CPU000),$(OBJDIR)/$(SDL2DIR)/src/$(o).o \
    $(OBJDIR)/$(SDL2DIR)/src/$(o).cmini.o): SDL2_CPU = -m68000

# For size: the BMP loader runs once a file (bmp.c gives the bytes).
SDL2_SIZE = bmp
$(foreach o,$(SDL2_SIZE),$(OBJDIR)/$(SDL2DIR)/src/$(o).o \
    $(OBJDIR)/$(SDL2DIR)/src/$(o).cmini.o): SDL2_CFLAGS += -Os

$(SDL2OBJS): $(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(SDL2_CFLAGS) $(SDL2_CPU) -c -o $@ $<

$(SDL2CMINIOBJS): $(OBJDIR)/%.cmini.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(SDL2_CFLAGS) $(SDL2_CPU) -I$(CMINI)/include -c -o $@ $<

$(SDL2XOBJS): $(OBJDIR)/$(SDL2DIR)/src/c2px%.o: $(SDL2DIR)/src/c2px.c
	@mkdir -p $(dir $@)
	$(CC) $(SDL2_CFLAGS) $(SDL2_CPU) -DSTDL_SDL2_XS=$* -c -o $@ $<

$(SDL2XCMINIOBJS): $(OBJDIR)/$(SDL2DIR)/src/c2px%.cmini.o: \
                   $(SDL2DIR)/src/c2px.c
	@mkdir -p $(dir $@)
	$(CC) $(SDL2_CFLAGS) $(SDL2_CPU) -I$(CMINI)/include \
	    -DSTDL_SDL2_XS=$* -c -o $@ $<

$(SDL2LIB): $(SDL2OBJS) $(SDL2XOBJS)
	$(AR) rcs $@ $(SDL2OBJS) $(SDL2XOBJS)

$(SDL2CMINILIB): $(SDL2CMINIOBJS) $(SDL2XCMINIOBJS)
	$(AR) rcs $@ $(SDL2CMINIOBJS) $(SDL2XCMINIOBJS)

# The add-on's text, which only a program that links it pays, and
# only the objects it calls: every object, and the largest of the X
# variants (a fixed X's or c2pany.o), of which it takes one at most.
# On 2026-11-04: 1,032 bytes for the skeleton (SDL_Init, errors,
# hints), 1,704 for c2p.o (the C twin and X = 0's loops, one line a
# row and two), 940 for the largest variant (c2pany.o; a fixed X's are
# 900-924), 9,444 for the screen (video.o 8,836: the window, the
# renderer, its textures and the compositor; palettes, the clock and
# the platform). Then the input, 4,424 (events.o 2,208 with the
# queue, joystick.o 1,672 with the controller, keymap.o 544 with its
# table), which a program that pumps events takes whole; surfaces
# 2,484, streams 2,132 and the log 752, each taken only if called.
# Then audio, 4,236: the device and its queue 2,572 (audio.o), mixing
# 540 and WAV files 1,124, the last two only if called. 27,148 in all,
# 504 of it (2026-11-05) a copy's short path, with the clipping and
# the partial groups in functions of their own, and the catch-up's
# copy a loop for each layout: a copy inside the texture and the
# screen went from 269 instructions to 141, 2,033 of a Falcon's cycles
# to 1,061. Then, on 2026-11-07, two raises, each deliberate and
# kept to its bytes so the 152-byte headroom stays:
# - 1,036 for SDL_LoadBMP (bmp.o, -Os), taken only if called, so that
#   a port keeps SDL2's own call for its art rather than a reader of
#   its own;
# - 536 for every joystick STDL has (STDL_SetNumJoysticks, six at
#   most) as SDL2's joysticks and controllers, each with its own state
#   and events, and the pump's SDL_JOYDEVICEADDED and REMOVED pairs as
#   the count moves (joystick.o 1,672 to 2,024, events.o 2,208 to
#   2,392). The readers it names for every joystick are the core's own
#   calls, which take the joystick (v1.23.0); the code behind joysticks
#   1-5 is linked only by a program that asks for more than one.
# 28,720 in all, from 27,148. Then 104 bytes of the headroom for the
# merge setting (STDL_UseJoystickMerge), at whose change every
# joystick goes and comes back (events.o 2,392 to 2,496), and 32 when
# the joysticks were made simpler: their buttons read from STDL
# (STDL_JoystickInputHeld) rather than kept, their handles in BSS
# (192 bytes of initialised data gone), each index set where a handle
# is first asked after, and a controller's buttons read only at a pump
# that brought its joystick an event or after a re-map (joystick.o
# 2,024 to 2,048, events.o 2,496 to 2,504). 28,840 in all as
# released. Then 36 for the pump once a VBL (STDL_PumpEventsIfDue):
# SDL_PollEvent pumps on STDL_PollEvent's beat and its waits pump
# every look (events.o 2,504 to 2,540), which took a Falcon's frame of
# 16 keys with four pads from 18-19ms to 3.5; the budget was raised by
# the 4 that passed it. 28,876 in all. Then 188 less when the device
# events and the axes and hats came from the core (STDL_JOYDEVICE*,
# STDL_GetJoystickAxis and Hat): events.o 2,540 to 2,160 without its
# own device bookkeeping, joystick.o 2,048 to 2,240 translating the
# device events and without its copies of the axes. 28,688. Then 492
# less when the conversion's loops lost the copy of the group before
# them (c2p_asm.h): c2p.o 1,704 to 1,452, the largest variant
# (c2pany.o) 940 to 704. 28,196.
# A TT or a Falcon has the room; the budget is there so that growth is
# a decision, raised with its reason or not at all.
SDL2_MAX = 28876

# The checks a build of the add-on must pass:
# - no SDL_ link symbol in either archive (SDL_stdinc.h says why);
# - the public headers compile as gnu99 and as C++98 with warnings as
#   errors, and refuse to meet the 1.2 shim;
# - its text within SDL2_MAX;
# - each conversion loop, with the code round it that runs once a row
#   (c2p_asm.h's _cache0 to _cache1), within the 240 bytes that sit in
#   the 68030's 256-byte instruction cache whatever their alignment:
#   one that outgrew it would fetch part of itself again every row,
#   from ST-RAM on a Falcon. The assembler cannot measure that span
#   itself, as it has forward branches, so nm does;
# - no libgcc arithmetic at all, as libcallcheck checks the core's
#   against tests/libcalls.allow: the drawing is built for the 68020,
#   which multiplies and divides 32 bits itself, so a call would be a
#   64-bit or soft-float operation or code built for the 68000. The
#   first one with a reason gets an allowlist of its own, and -a.
sdl2check: $(SDL2LIB) $(SDL2CMINILIB)
	@if $(CROSS)nm --defined-only $(SDL2LIB) $(SDL2CMINILIB) \
	     | grep -E ' [A-Za-z] _SDL_'; then \
	   echo "*** the SDL2 add-on defines SDL_ link symbols"; exit 1; \
	 fi
	@$(CC) -std=gnu99 $(SDL2_WARN) -Werror $(SDL2_INC) -m68020-60 \
	    -msoft-float -fsyntax-only $(SDL2DIR)/tests/hdrcheck.c
	@$(CROSS)g++ -std=c++98 $(SDL2_WARN) -Werror $(SDL2_INC) \
	    -m68020-60 -msoft-float -fsyntax-only -x c++ \
	    $(SDL2DIR)/tests/hdrcheck.c
	@if $(CC) -std=gnu99 $(SDL2_INC) -DSTDL_COMPAT_SDL_H -fsyntax-only \
	     $(SDL2DIR)/tests/hdrcheck.c 2>/dev/null; then \
	   echo "*** the add-on's headers met the 1.2 shim's guard"; exit 1; \
	 fi
	@n=`$(CROSS)size $(filter-out %/c2pany.o,$(SDL2OBJS)) | \
	    awk 'NR>1 {t+=$$1} END {print t}'`; \
	 x=`$(CROSS)size $(SDL2XOBJS) $(OBJDIR)/$(SDL2DIR)/src/c2pany.o | \
	    awk 'NR>1 && $$1>m {m=$$1} END {print m}'`; \
	 n=`expr $$n + $$x`; \
	 echo "SDL2 add-on text $$n bytes (budget $(SDL2_MAX))"; \
	 if [ $$n -gt $(SDL2_MAX) ]; then \
	   echo "*** over the SDL2 add-on's size budget"; exit 1; \
	 fi
	@$(CROSS)nm -t d $(SDL2LIB) | awk ' \
	   $$3 ~ /_cache0$$/ { n = $$3; sub(/_cache0$$/, "", n); s[n] = $$1 + 0 } \
	   $$3 ~ /_cache1$$/ { n = $$3; sub(/_cache1$$/, "", n); e[n] = $$1 + 0 } \
	   END { for (n in s) { d = e[n] - s[n]; c++; \
	           if (d <= 0 || d > 240) { bad = 1; \
	             print "*** " n ": " d " bytes of loop, past the 240 that sit in the 68030 instruction cache" } } \
	         if (c < 34) { bad = 1; \
	           print "*** " c + 0 " conversion loops measured, 34 expected" } \
	         exit bad }'
	@calls=`sh tools/libcalls.sh $(SDL2OBJS) $(SDL2XOBJS)` || exit 1; \
	 if [ -n "$$calls" ]; then \
	   echo "$$calls"; \
	   echo "*** libgcc arithmetic in the SDL2 add-on"; exit 1; \
	 fi

# The add-on's example, built here and shipped in the examples' bundle
# beside the core's, which `all` never builds: compiled for the 68020
# without FPU instructions and linked against the 68000 libraries, as
# a program using the add-on is (the toolchain's 68020 ones assume an
# FPU, and a stock Falcon refuses a program linked with them).
SDL2EXAMPLES  = dist/SDL2DEMO.TOS
SDL2_PROGFLAGS = -O2 -fomit-frame-pointer -std=gnu99 $(SDL2_WARN) \
                 -Iinclude -I$(SDL2DIR)/include/SDL2 -m68020-60 -msoft-float

dist/SDL2DEMO.TOS: examples/sdl2demo.c $(SDL2LIB) $(LIB) | dist
	@mkdir -p $(OBJDIR)/examples
	$(CC) $(SDL2_PROGFLAGS) -c -o $(OBJDIR)/examples/sdl2demo.o $<
	$(CC) -m68000 -o $@ $(OBJDIR)/examples/sdl2demo.o $(SDL2LIB) $(LIB) \
	    && $(STRIP) $@

sdl2: $(SDL2LIB) $(SDL2CMINILIB) sdl2check $(SDL2EXAMPLES)

-include $(SDL2OBJS:.o=.d) $(SDL2CMINIOBJS:.o=.d) $(SDL2XOBJS:.o=.d) \
         $(SDL2XCMINIOBJS:.o=.d)

.PHONY: sdl2 sdl2check
