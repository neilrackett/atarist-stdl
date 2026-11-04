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
                $(SDL2DIR)/src/hints.c
SDL2OBJS      = $(patsubst %.c,$(OBJDIR)/%.o,$(SDL2SRCS))
SDL2CMINIOBJS = $(patsubst %.c,$(OBJDIR)/%.cmini.o,$(SDL2SRCS))

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

$(SDL2OBJS): $(OBJDIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(SDL2_CFLAGS) $(SDL2_CPU) -c -o $@ $<

$(SDL2CMINIOBJS): $(OBJDIR)/%.cmini.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(SDL2_CFLAGS) $(SDL2_CPU) -I$(CMINI)/include -c -o $@ $<

$(SDL2LIB): $(SDL2OBJS)
	$(AR) rcs $@ $(SDL2OBJS)

$(SDL2CMINILIB): $(SDL2CMINIOBJS)
	$(AR) rcs $@ $(SDL2CMINIOBJS)

# The add-on's text, which only a program that links it pays: 1,032
# bytes on 2026-11-04 for the skeleton (SDL_Init, errors, hints); the
# screen's code will set the level this budget really holds.
SDL2_MAX = 1200

# The checks a build of the add-on must pass:
# - no SDL_ link symbol in either archive (SDL_stdinc.h says why);
# - the public headers compile as gnu99 and as C++98 with warnings as
#   errors, and refuse to meet the 1.2 shim;
# - its text within SDL2_MAX;
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
	@n=`$(CROSS)size $(SDL2OBJS) | awk 'NR>1 {t+=$$1} END {print t}'`; \
	 echo "SDL2 add-on text $$n bytes (budget $(SDL2_MAX))"; \
	 if [ $$n -gt $(SDL2_MAX) ]; then \
	   echo "*** over the SDL2 add-on's size budget"; exit 1; \
	 fi
	@calls=`sh tools/libcalls.sh $(SDL2OBJS)` || exit 1; \
	 if [ -n "$$calls" ]; then \
	   echo "$$calls"; \
	   echo "*** libgcc arithmetic in the SDL2 add-on"; exit 1; \
	 fi

sdl2: $(SDL2LIB) $(SDL2CMINILIB) sdl2check

-include $(SDL2OBJS:.o=.d) $(SDL2CMINIOBJS:.o=.d)

.PHONY: sdl2 sdl2check
