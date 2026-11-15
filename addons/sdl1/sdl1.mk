# STDL - Planar Display Library for Atari ST
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# The SDL1 add-on (addons/sdl1, docs/sdl1.md), experimental: SDL 1.2's
# chunky picture for every machine, for getting an SDL 1.2 program
# running before it is ported to planes. Included by the root Makefile
# for its targets alone, after the SDL2 add-on's, whose experimental
# 68000 build it draws through - `all`, `make sdl2` and the release
# build none of it:
#
#   STCMD_NO_TTY=1 stcmd make sdl1      # libstdl-sdl1.a, -cmini.a
#   m68k-atari-mint-gcc -m68000 -I<stdl>/addons/sdl1/include/SDL1 \
#       -I<stdl>/include ... prog.o <stdl>/libstdl-sdl1.a \
#       <stdl>/libstdl-sdl2-st.a <stdl>/libstdl.a
#
# Its two sources never see each other's SDL.h: video.c is compiled
# against this add-on's SDL 1.2 headers, bridge.c against the SDL2
# add-on's (bridge.h says why).

SDL1DIR      = addons/sdl1
SDL1LIB      = libstdl-sdl1.a
SDL1CMINILIB = libstdl-sdl1-cmini.a

SDL1_CFLAGS  = -O2 -fomit-frame-pointer -std=gnu99 $(SDL2_WARN) -MMD -MP \
               -m68000 -Iinclude
SDL1_INC1    = -I$(SDL1DIR)/include/SDL1
SDL1_INC2    = -I$(SDL2DIR)/include/SDL2

SDL1OBJS      = $(OBJDIR)/$(SDL1DIR)/src/video.o \
                $(OBJDIR)/$(SDL1DIR)/src/bridge.o
SDL1CMINIOBJS = $(SDL1OBJS:.o=.cmini.o)

$(OBJDIR)/$(SDL1DIR)/src/video.o: $(SDL1DIR)/src/video.c
	@mkdir -p $(dir $@)
	$(CC) $(SDL1_CFLAGS) $(SDL1_INC1) -c -o $@ $<

$(OBJDIR)/$(SDL1DIR)/src/bridge.o: $(SDL1DIR)/src/bridge.c
	@mkdir -p $(dir $@)
	$(CC) $(SDL1_CFLAGS) $(SDL1_INC2) -c -o $@ $<

$(OBJDIR)/$(SDL1DIR)/src/video.cmini.o: $(SDL1DIR)/src/video.c
	@mkdir -p $(dir $@)
	$(CC) $(SDL1_CFLAGS) $(SDL1_INC1) -I$(CMINI)/include -c -o $@ $<

$(OBJDIR)/$(SDL1DIR)/src/bridge.cmini.o: $(SDL1DIR)/src/bridge.c
	@mkdir -p $(dir $@)
	$(CC) $(SDL1_CFLAGS) $(SDL1_INC2) -I$(CMINI)/include -c -o $@ $<

$(SDL1LIB): $(SDL1OBJS)
	$(AR) rcs $@ $(SDL1OBJS)

$(SDL1CMINILIB): $(SDL1CMINIOBJS)
	$(AR) rcs $@ $(SDL1CMINIOBJS)

# What the build promises of it:
#  - no SDL_ link symbol: libstdl.a's are the planar shim's, and a
#    program on this add-on links that archive too;
#  - its headers compile as gnu99 and as C++98 with warnings as errors;
#  - they stop the build where they meet the planar shim's;
#  - STDL_SDL1_STRICT stops a line that touches a surface's pixels, and
#    nothing else.
SDL1_HDR = $(SDL1_INC1) -Iinclude -m68000 -Wall -Wextra -Werror -fsyntax-only
SDL1_USE = '\#include "SDL.h"\n\#include "SDL_mixer.h"\nint f(SDL_Surface *s) { return s->w + (int)sizeof(Mix_Chunk); }\n'
SDL1_PIX = '\#include "SDL.h"\nvoid *f(SDL_Surface *s) { return s->pixels; }\n'

sdl1check: $(SDL1LIB) $(SDL1CMINILIB)
	@if $(CROSS)nm --defined-only $(SDL1LIB) $(SDL1CMINILIB) \
	     | grep -E ' [A-Za-z] _SDL_'; then \
	   echo "*** the SDL1 add-on defines SDL_ link symbols"; exit 1; \
	 fi
	@printf $(SDL1_USE) | $(CC) -std=gnu99 $(SDL1_HDR) -x c - \
	   || { echo "*** the SDL1 add-on's headers do not compile as C"; exit 1; }
	@printf $(SDL1_USE) | $(CROSS)g++ -std=c++98 $(SDL1_HDR) -x c++ - \
	   || { echo "*** the SDL1 add-on's headers do not compile as C++98"; exit 1; }
	@printf $(SDL1_USE) | $(CC) -std=gnu99 $(SDL1_HDR) -DSTDL_SDL1_STRICT -x c - \
	   || { echo "*** STDL_SDL1_STRICT stops more than the pixels"; exit 1; }
	@if printf $(SDL1_PIX) | $(CC) -std=gnu99 $(SDL1_HDR) -DSTDL_SDL1_STRICT \
	     -x c - 2>/dev/null; then \
	   echo "*** STDL_SDL1_STRICT let a surface's pixels through"; exit 1; \
	 fi
	@if printf '\#include "SDL.h"\n\#include <compat/SDL.h>\n' \
	     | $(CC) -std=gnu99 $(SDL1_HDR) -x c - 2>/dev/null; then \
	   echo "*** the SDL1 add-on's SDL.h met the planar shim's and built"; \
	   exit 1; \
	 fi

# Its examples are SDL 1.2's own tests, unchanged (examples/args.c
# gives each the command line a TOS program has not got): a 320x200
# screen, and sixteen sprites where a PC had a hundred.
# SDL1_EXTRA="-DSDL1_RUN=15 -DSDL1_CHECK" builds them to end by
# themselves after 15 seconds, with the screen check on and its result
# printed (args.c).
SDL1TESTS      = dist/S1SPRITE.TOS dist/S1GRAY.TOS
SDL1EXAMPLES   = $(SDL1TESTS) dist/S1PORTED.TOS
SDL1_PROGFLAGS = -O2 -fomit-frame-pointer -std=gnu99 -m68000 \
                 -Iinclude $(SDL1_INC1) -I$(SDL1DIR)/examples
SDL1_LIBS      = $(SDL1LIB) $(SDL2STLIB) $(LIB)

dist/S1SPRITE.TOS: SDL1_TEST = testsprite.c
dist/S1SPRITE.TOS: SDL1_ARGS = "-width", "320", "-height", "200", "16"
dist/S1GRAY.TOS:   SDL1_TEST = graywin.c
dist/S1GRAY.TOS:   SDL1_ARGS = "-width", "320", "-height", "200"

dist/S1SPRITE.TOS: $(SDL1DIR)/examples/testsprite.c
dist/S1GRAY.TOS:   $(SDL1DIR)/examples/graywin.c

$(SDL1TESTS): $(SDL1DIR)/examples/args.c $(SDL1_LIBS) | dist
	$(CC) $(SDL1_PROGFLAGS) $(SDL1_EXTRA) -DSDL1_TEST='"$(SDL1_TEST)"' \
	    -DSDL1_ARGS='$(SDL1_ARGS)' -o $@ $(SDL1DIR)/examples/args.c \
	    $(SDL1_LIBS) -lm && $(STRIP) $@

# And where a port ends up: STDL's own port of testsprite to planes
# (examples/testsprite.c, dist/TSPRITE.TOS on the planar shim), built
# here from the same source with STDL_SDL1_STRICT, which it passes, as
# no line of it touches a surface's pixels any more. One source, both
# front ends: a program moved that far changes its include path and
# archives, and nothing else.
dist/S1PORTED.TOS: examples/testsprite.c $(SDL1_LIBS) | dist
	$(CC) $(SDL1_PROGFLAGS) -Wall -DSTDL_SDL1_STRICT -o $@ $< \
	    $(SDL1_LIBS) -lm && $(STRIP) $@

sdl1: $(SDL1LIB) $(SDL1CMINILIB) sdl1check $(SDL1EXAMPLES)

-include $(SDL1OBJS:.o=.d) $(SDL1CMINIOBJS:.o=.d)

.PHONY: sdl1 sdl1check
