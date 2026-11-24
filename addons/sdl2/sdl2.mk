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
                $(SDL2DIR)/src/c2pany.c $(SDL2DIR)/src/c2pp.c \
                $(SDL2DIR)/src/c2ppair.c $(SDL2DIR)/src/palette.c \
                $(SDL2DIR)/src/timer.c $(SDL2DIR)/src/video.c \
                $(SDL2DIR)/src/display.c \
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
# (c2pany.o) 940 to 704. 28,196. Then 592 for the SDL2 calls one
# port's compatibility layer had to supply itself, which any port of a
# PC program meets: SDL_PeepEvents, the relative mouse (the IKBD's own
# movement, summed as events are taken) and SDL_WarpMouseInWindow
# (events.o 2,160 to 2,592), and the joysticks' GUIDs, which programs
# keep in their settings (joystick.o 2,240 to 2,400; the GUIDs'
# strings, the byte swaps, the controller type and the paths inline in
# the headers, paid only by a program that calls them; the wheel's
# event defined and never sent, no code). 28,788.
#
# Raised 28876 -> 30784 on 2026-11-15, by 1,908 bytes for pixel pairs
# (STDL_SDL2_RenderCopyPairs): c2ppair.o's loops, the C twin's absence
# on the machine and the batch's marking, 1,584, linked only by a
# program that calls it; and 324 in video.o that every program has,
# the batch's second mask, tested at each copy and given a pass of its
# own at the flush. A low-detail game's view converted as pairs, 56
# instructions a group against 74: one port's low-detail frame on a TT
# 3.5ms faster, 68.2 -> 64.7 (Hatari). 30,696.
#
# Raised 30784 -> 30988 on 2026-11-16, by the 204 that 292 in video.o
# passed it: the rows a frame drew on and each page owes kept as
# ranges, as the experimental 68000 build kept them, so that a present
# reads and clears those rows of the masks and not all 240 of four of
# them. One mechanism for both builds; little time on a 68030, where
# one port's frame of play went from 11.64ms of work to 11.52 on a TT
# and from 22.89 to 22.80 on a Falcon (Hatari). 30,988.
#
# Raised 30988 -> 31300 on 2026-11-16, by 312 in video.o for copies
# compared (STDL_SDL2_HINT_COMPARE): the compare's loop and the chunky
# copy of the screen kept without the check. That port's frame of
# play, three groups in four of its pushes on screen already, 11.60ms
# to 9.90 on a TT and 22.84 to 19.57 on a Falcon. 31,300.
#
# Raised 31300 -> 31680 on 2026-11-17, by 380 in video.o for
# STDL_SDL2_SetCompare, the compare turned on and off as a program
# goes (168 for the call; the rest the chunky copy left unkept while
# the compare is off and the rows of it gone stale since, tested at
# each compared copy and clear). One port's view, all new pixels each
# frame, fills the screen or leaves a border that never changes as the
# player sizes it: the compare cost its frame 2-4% at the full size and
# saved 4-13% at the smaller ones (Hatari, a TT), where the hint alone
# had to take one or the other for the whole game. 31,680.
#
# Raised 31680 -> 31764 on 2026-11-18, by 84 in video.o for the compare
# turned off for one copy of a frame: a copy made while it is off takes
# its own groups out of those the chunky copy has, where turning it
# off had dropped the whole copy. One port's level, a playfield that is
# new every frame with status displays round it, on a TT at 16MHz
# (Hatari): 122.1ms a frame with every copy compared, 108.2 with none,
# 115.0 with the compare off for the playfield before this and 104.5
# with it. 31,764.
#
# Raised 31764 -> 31972 on 2026-11-19, by 208 in surface.o for
# SDL_FillRect: whole rows as one memset, any other box in longs a row
# with its own cut to the clip rectangle, where each row was a memset.
# A screen 7.0ms to 4.1 on a TT and 14.2 to 8.1 on a Falcon, a 16x16
# box 121us to 53 and 231 to 105, an 8x8 one 69us to 29 and 135 to 58
# (Hatari, ST-RAM). 31,972.
# Raised 31972 -> 32768 on 2026-11-21, by 796 to a round 32K: the
# budget was full, and the next change to the add-on, a fix or a
# feature, would have had to raise it first. 32,768.
# A TT or a Falcon has the room; the budget is there so that growth is
# a decision, raised with its reason or not at all.
SDL2_MAX = 32768

# A program calling the conversion alone (SDL_stdl_c2p.h), built for
# the 68000 with no SDL.h on its path; SDL2_C2PLINK links it against an
# archive and its libcmini twin with nothing else of STDL ($(1), $(2);
# $(3) the programs' name, $(4) words for the message).
SDL2C2PLINK = $(OBJDIR)/$(SDL2DIR)/c2plink.o
$(SDL2C2PLINK): $(SDL2DIR)/tests/c2plink.c \
                $(SDL2DIR)/include/SDL2/SDL_stdl_c2p.h
	@mkdir -p $(dir $@)
	$(CC) -std=gnu99 $(SDL2_WARN) -Werror -I$(SDL2DIR)/include -m68000 \
	    -O2 -c -o $@ $<

define SDL2_C2PLINK
@$(CC) -m68000 -o $(OBJDIR)/$(SDL2DIR)/$(3).tos $(SDL2C2PLINK) $(1) || { \
  echo "*** the$(4) conversion alone does not link without STDL"; exit 1; }
@$(CC) -nostdlib -L$(CMINI)/lib -Wl,--traditional-format -m68000 \
    -o $(OBJDIR)/$(SDL2DIR)/$(3)c.tos $(CMINI)/lib/crt0.o $(SDL2C2PLINK) \
    $(2) -lcmini -lgcc || { \
  echo "*** the$(4) conversion alone does not link without STDL (cmini)"; \
  exit 1; }
endef

# The checks a build of the add-on must pass:
# - no SDL_ link symbol in either archive (SDL_stdinc.h says why);
# - the public headers compile as gnu99 and as C++98 with warnings as
#   errors, and refuse to meet the 1.2 shim;
# - its text within SDL2_MAX;
# - each conversion loop, with the code round it that runs once a row
#   (c2p_asm.h's labels 1 to 9), within the 240 bytes that sit in the
#   68030's 256-byte instruction cache whatever their alignment: one
#   that outgrew it would fetch part of itself again every row, from
#   ST-RAM on a Falcon. The assembler cannot test that span itself, as
#   it has forward branches, but works it out once they are sized into
#   an absolute symbol, _NAME_cachebytes, which nm reads. The labels
#   leave no text symbol (c2p_asm.h says why), and a text symbol
#   _NAME_cache0 or _NAME_cache1, as they once were, is refused;
# - the conversion alone (SDL_stdl_c2p.h) compiles with no SDL.h on the
#   path, as gnu99 for the 68000 and as C++98, and a program calling
#   it (tests/c2plink.c) links against each archive with nothing else:
#   no libstdl.a, so the renderer, which calls STDL, cannot come too;
# - no libgcc arithmetic at all, as libcallcheck checks the core's
#   against tests/libcalls.allow: the drawing is built for the 68020,
#   which multiplies and divides 32 bits itself, so a call would be a
#   64-bit or soft-float operation or code built for the 68000. The
#   first one with a reason gets an allowlist of its own, and -a.
sdl2check: $(SDL2LIB) $(SDL2CMINILIB) $(SDL2C2PLINK)
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
	   $$2 == "a" && $$3 ~ /_cachebytes$$/ { n = $$3; \
	     sub(/_cachebytes$$/, "", n); d = $$1 + 0; c++; \
	     if (d <= 0 || d > 240) { bad = 1; \
	       print "*** " n ": " d " bytes of loop, past the 240 that sit in the 68030 instruction cache" } } \
	   $$2 ~ /^[Tt]$$/ && $$3 ~ /_cache[01]$$/ { bad = 1; \
	     print "*** " $$3 ": a loop label as a text symbol, which a profiler takes for a function" } \
	   END { if (c < 38) { bad = 1; \
	           print "*** " c + 0 " conversion loops measured, 38 expected" } \
	         exit bad }'
	@$(CROSS)g++ -std=c++98 $(SDL2_WARN) -Werror -I$(SDL2DIR)/include \
	    -m68000 -fsyntax-only -x c++ $(SDL2DIR)/tests/c2plink.c
	$(call SDL2_C2PLINK,$(SDL2LIB),$(SDL2CMINILIB),c2plink,)
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
SDL2EXAMPLES  = dist/SDL2DEMO.TOS dist/SDL2C2P.TOS
SDL2_PROGFLAGS = -O2 -fomit-frame-pointer -std=gnu99 $(SDL2_WARN) \
                 -Iinclude -I$(SDL2DIR)/include/SDL2 -m68020-60 -msoft-float

dist/SDL2DEMO.TOS: examples/sdl2demo.c $(SDL2LIB) $(LIB) | dist
	@mkdir -p $(OBJDIR)/examples
	$(CC) $(SDL2_PROGFLAGS) -c -o $(OBJDIR)/examples/sdl2demo.o $<
	$(CC) -m68000 -o $@ $(OBJDIR)/examples/sdl2demo.o $(SDL2LIB) $(LIB) \
	    && $(STRIP) $@

# The conversion alone (SDL_stdl_c2p.h) in a program that draws its own
# chunky screen: built for the 68000, as a port that also runs on an ST
# is, with no SDL.h on its path, and linked against this archive for
# the conversion and libstdl.a for its screen.
SDL2C2P_FLAGS = $(filter-out -m68020-60 -msoft-float \
                  -I$(SDL2DIR)/include/SDL2,$(SDL2_PROGFLAGS)) \
                -I$(SDL2DIR)/include -m68000
dist/SDL2C2P.TOS: examples/sdl2c2p.c $(SDL2LIB) $(LIB) | dist
	@mkdir -p $(OBJDIR)/examples
	$(CC) $(SDL2C2P_FLAGS) -c -o $(OBJDIR)/examples/sdl2c2p.o $<
	$(CC) -m68000 -o $@ $(OBJDIR)/examples/sdl2c2p.o $(SDL2LIB) $(LIB) \
	    && $(STRIP) $@

sdl2: $(SDL2LIB) $(SDL2CMINILIB) sdl2check $(SDL2EXAMPLES)

# The ST build, experimental (docs/sdl2.md, "An experimental build for
# every machine"): the whole add-on for the 68000, which on a TT
# or a Falcon is the 256-colour screen as above and on an ST an ST's
# screen in 16 colours reduced from the palette's 256. Archives of
# their own, every object built for the 68000 with STDL_SDL2_ST, which
# internal.h refuses on anything above one; `make sdl2` builds none of
# this, the archives above have none of it, and nothing here is
# shipped. st.c is the reduction and the four-plane conversion, beside
# the 68030's (c2p.c, c2pany.c, c2px.c), whose assembler a 68000 runs
# too; stpair.c the conversion of pixel pairs, which only c2ppair.c's
# pointers name, so only a program asking for pairs links it. A
# program is compiled -m68000 and links libstdl-sdl2-st.a where it
# linked libstdl-sdl2.a.
SDL2STLIB      = libstdl-sdl2-st.a
SDL2STCMINILIB = libstdl-sdl2-st-cmini.a
SDL2STSRCS     = $(SDL2SRCS) $(SDL2DIR)/src/st.c $(SDL2DIR)/src/stpair.c
SDL2STXOBJS      = $(SDL2XOBJS:.o=.st.o)
SDL2STXCMINIOBJS = $(SDL2XOBJS:.o=.st.cmini.o)
SDL2STOBJS      = $(patsubst %.c,$(OBJDIR)/%.st.o,$(SDL2STSRCS))
SDL2STCMINIOBJS = $(patsubst %.c,$(OBJDIR)/%.st.cmini.o,$(SDL2STSRCS))
SDL2ST_CPU      = -m68000 -DSTDL_SDL2_ST
$(foreach o,$(SDL2_SIZE),$(OBJDIR)/$(SDL2DIR)/src/$(o).st.o \
    $(OBJDIR)/$(SDL2DIR)/src/$(o).st.cmini.o): SDL2_CFLAGS += -Os

$(SDL2STXOBJS): $(OBJDIR)/$(SDL2DIR)/src/c2px%.st.o: $(SDL2DIR)/src/c2px.c
	@mkdir -p $(dir $@)
	$(CC) $(SDL2_CFLAGS) $(SDL2ST_CPU) -DSTDL_SDL2_XS=$* -c -o $@ $<

$(SDL2STXCMINIOBJS): $(OBJDIR)/$(SDL2DIR)/src/c2px%.st.cmini.o: \
                     $(SDL2DIR)/src/c2px.c
	@mkdir -p $(dir $@)
	$(CC) $(SDL2_CFLAGS) $(SDL2ST_CPU) -I$(CMINI)/include \
	    -DSTDL_SDL2_XS=$* -c -o $@ $<

$(SDL2STOBJS): $(OBJDIR)/%.st.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(SDL2_CFLAGS) $(SDL2ST_CPU) -c -o $@ $<

$(SDL2STCMINIOBJS): $(OBJDIR)/%.st.cmini.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(SDL2_CFLAGS) $(SDL2ST_CPU) -I$(CMINI)/include -c -o $@ $<

$(SDL2STLIB): $(SDL2STOBJS) $(SDL2STXOBJS)
	$(AR) rcs $@ $(SDL2STOBJS) $(SDL2STXOBJS)

$(SDL2STCMINILIB): $(SDL2STCMINIOBJS) $(SDL2STXCMINIOBJS)
	$(AR) rcs $@ $(SDL2STCMINIOBJS) $(SDL2STXCMINIOBJS)

# The add-on's example built for this archive, three ways: SDL2ST as an
# ST runs it; SDL2ST16 with the program's own 16 colours on an ST
# (STDL_SDL2_SetSTColours, sdl2demo.c's SDL2DEMO_ST16); and SDL2WIDE at
# 640x480 in 16 colours on a TT or a Falcon (SDL2DEMO_WIDE:
# STDL_RESPONSIVE_WIDE through the ST build's 16 colours).
SDL2STDEMOS = dist/SDL2ST.TOS dist/SDL2ST16.TOS dist/SDL2WIDE.TOS
dist/SDL2ST.TOS:   SDL2DEMO_DEF =
dist/SDL2ST16.TOS: SDL2DEMO_DEF = -DSDL2DEMO_ST16
dist/SDL2WIDE.TOS: SDL2DEMO_DEF = -DSDL2DEMO_WIDE

$(SDL2STDEMOS): examples/sdl2demo.c $(SDL2STLIB) $(LIB) | dist
	@mkdir -p $(OBJDIR)/examples
	$(CC) $(filter-out -m68020-60 -msoft-float,$(SDL2_PROGFLAGS)) -m68000 \
	    $(SDL2DEMO_DEF) -c -o $(OBJDIR)/examples/$(notdir $@).o $<
	$(CC) -m68000 -o $@ $(OBJDIR)/examples/$(notdir $@).o $(SDL2STLIB) \
	    $(LIB) && $(STRIP) $@

# no SDL_ link symbol here either. What keeps this code out of the
# archives above is the #error in internal.h and st.c, not a check here.
sdl2st: $(SDL2STLIB) $(SDL2STCMINILIB) $(SDL2C2PLINK) $(SDL2STDEMOS)
	@if $(CROSS)nm --defined-only $(SDL2STLIB) $(SDL2STCMINILIB) \
	     | grep -E ' [A-Za-z] _SDL_'; then \
	   echo "*** the SDL2 add-on's ST build defines SDL_ link symbols"; \
	   exit 1; \
	 fi
	$(call SDL2_C2PLINK,$(SDL2STLIB),$(SDL2STCMINILIB),c2plinkst, ST build's)

-include $(SDL2OBJS:.o=.d) $(SDL2CMINIOBJS:.o=.d) $(SDL2XOBJS:.o=.d) \
         $(SDL2XCMINIOBJS:.o=.d) $(SDL2STOBJS:.o=.d) \
         $(SDL2STCMINIOBJS:.o=.d) $(SDL2STXOBJS:.o=.d) \
         $(SDL2STXCMINIOBJS:.o=.d)

.PHONY: sdl2 sdl2check sdl2st
