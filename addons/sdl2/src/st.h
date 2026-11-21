/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The ST build of the add-on, experimental (docs/sdl2.md, "An
 * experimental build for every machine"): the whole add-on for the
 * 68000, under STDL_SDL2_ST, which `make sdl2st` defines for every
 * object of libstdl-sdl2-st.a and nothing else does. A TT or a Falcon
 * gets the 256-colour screen from it; an ST gets its own screen in 16
 * colours, for testing, which is what is here. It is code for the
 * 68000 alone; internal.h refuses the macro on anything above one, so
 * none of it can reach the add-on built for a TT or a Falcon.
 */

#ifndef STDL_SDL2_ST_H
#define STDL_SDL2_ST_H

#include "internal.h"

/* the maps in use: a colour's first slot and its second (the same
 * without the checkerboard), and the error they were made with */
struct stdl_sdl2_st_maps {
    Uint8 a[256], b[256];
    Uint32 made;
    int made_valid;
};

/*
 * The palette's first ncol colours (256 at most) as 16, for a machine
 * showing `bits` a channel (3 an ST, 4 an STE; anything else: as they
 * are), made from the colours seen (seen[i] not 0; NULL: all): a[i]
 * and b[i] are colour i's two slots, which a checkerboard mixes (b is a
 * without dither), and
 * out[] the 16 slots' colours. Slot 0 is black and takes every black,
 * every index past ncol and the border index x (when not 0); the other
 * 15 are made from the colours seen, and a colour not seen goes to the
 * nearest. Returns the error, as stdl_sdl2_st_fit does.
 */
Uint32 stdl_sdl2_st_reduce(const SDL_Color *col, int ncol, unsigned x,
                           int bits, int dither, const Uint8 *seen,
                           Uint8 *a, Uint8 *b, SDL_Color *out);

/* maps' 16 colours over a palette, each slot the mean of the colours
 * seen whose first slot it is, as shown, and slot 0 black; returns how
 * far the colours seen are from what they are shown as, as a share of
 * those colours' own size */
Uint32 stdl_sdl2_st_fit(const SDL_Color *col, int ncol, unsigned x, int bits,
                        const Uint8 *seen, const Uint8 *a, const Uint8 *b,
                        SDL_Color *out);

/* 1 if the maps in use still fit the palette and the colours seen
 * (st.c says when), out their colours; 0 if new maps are wanted */
int stdl_sdl2_st_keep(const SDL_Color *col, int ncol, unsigned x, int bits,
                      const Uint8 *seen, const struct stdl_sdl2_st_maps *m,
                      SDL_Color *out);

/* second slots for first slots a and 16 colours c over a palette, as
 * the reduction makes them (b is a without dither): for maps a program
 * gives (STDL_SDL2_SetSTColours) */
void stdl_sdl2_st_seconds(const SDL_Color *col, int ncol, unsigned x,
                          const SDL_Color *c, const Uint8 *a, Uint8 *b,
                          int dither);

/* maps a and b in m's place; 1 when they moved */
int stdl_sdl2_st_use(struct stdl_sdl2_st_maps *m, const Uint8 *a,
                     const Uint8 *b);

/* new maps from the palette and the colours seen; 1 when a map moved */
int stdl_sdl2_st_remake(const SDL_Color *col, int ncol, unsigned x, int bits,
                        int dither, const Uint8 *seen,
                        struct stdl_sdl2_st_maps *m, SDL_Color *out);

/* the maps as the conversion's two tables of 4 by 256 longs each, for
 * even rows and for odd: a slot's four bits, a byte each, shifted 3,
 * 2, 1 and 0, the first slot where x + y is even */
void stdl_sdl2_st_spread(const Uint8 *a, const Uint8 *b, Uint32 *tab);

/* h rows of `groups` groups of 16 pixels from src, sp bytes a row,
 * to four planes at dst, dp bytes a row, 8 bytes a group; at least one
 * of each, 32,768 groups at most. The first row goes through tab, the
 * next through `next`, and so on in turn (stdl_sdl2_st_conv_fn,
 * internal.h). */
stdl_sdl2_st_conv_fn stdl_sdl2_st_c2p;

/* Pixel pairs (stpair.c, reached through c2ppair.c's pointers): from
 * the conversion's tables above, the pair tables, two sets of 4 by 256
 * longs again (8K), a pair's two bits for its place among four in a
 * byte of each plane; and h rows of groups through them, as
 * stdl_sdl2_st_c2p, for rows whose pixels at 2i and 2i + 1 are the
 * same: one lookup a pair, of the second pixel's colour. src and sp
 * even, as the pairs are read a word each. */
void stdl_sdl2_st_pairtabs(const Uint32 *tab, Uint32 *ptab);
stdl_sdl2_st_conv_fn stdl_sdl2_st_c2pp;

#ifdef __m68k__
/*
 * The frame both loops have (st.c, stpair.c): `name` as
 * stdl_sdl2_st_conv_fn, SETUP run once and G a group, from (a0)+ into
 * d0 (its plane words' high bytes) and d2 (their low ones), which
 * movep writes. a2-a5 are the row's four tables, a6 the next row's,
 * exchanged a row; d3 is groups - 1, d6 the group count, d4 and d5
 * what is left of each row's strides, and the rows are counted in
 * their argument. d1 and d7 are G's (SETUP's) to use.
 */
#define ST_FUNC(name, setup, g) \
    "	.text\n" \
    "	.even\n" \
    "	.globl	_" name "\n" \
    "_" name ":\n" \
    "	movem.l	%d2-%d7/%a2-%a6,-(%sp)\n" \
    "	move.l	48(%sp),%a0\n"            /* src */ \
    "	move.l	52(%sp),%d4\n"            /* sp */ \
    "	move.l	56(%sp),%a1\n"            /* dst */ \
    "	move.l	60(%sp),%d5\n"            /* dp */ \
    "	move.l	64(%sp),%d3\n"            /* groups; 68: h */ \
    "	move.l	72(%sp),%a2\n"            /* this row's tables */ \
    "	move.l	76(%sp),%a6\n"            /* and the next's */ \
    setup \
    "	move.l	%d3,%d0\n" \
    "	lsl.l	#3,%d0\n" \
    "	sub.l	%d0,%d5\n"                /* dp - 8 * groups */ \
    "	add.l	%d0,%d0\n" \
    "	sub.l	%d0,%d4\n"                /* sp - 16 * groups */ \
    "	subq.w	#1,%d3\n" \
    "1:	lea	1024(%a2),%a3\n" \
    "	lea	1024(%a3),%a4\n" \
    "	lea	1024(%a4),%a5\n" \
    "	move.w	%d3,%d6\n" \
    "2:\n" g \
    "	movep.l	%d0,0(%a1)\n" \
    "	movep.l	%d2,1(%a1)\n" \
    "	addq.l	#8,%a1\n" \
    "	dbra	%d6,2b\n" \
    "	add.l	%d4,%a0\n" \
    "	add.l	%d5,%a1\n" \
    "	exg	%a2,%a6\n"                /* the rows' tables change places */ \
    "	subq.l	#1,68(%sp)\n" \
    "	bne	1b\n" \
    "	movem.l	(%sp)+,%d2-%d7/%a2-%a6\n" \
    "	rts\n"
#else
/* the C twins' store of a group's two longs, a byte of each plane word */
static __inline__ void stdl_sdl2_st_store(Uint8 *d, Uint32 a, Uint32 b)
{
    d[0] = (Uint8)(a >> 24);
    d[2] = (Uint8)(a >> 16);
    d[4] = (Uint8)(a >> 8);
    d[6] = (Uint8)a;
    d[1] = (Uint8)(b >> 24);
    d[3] = (Uint8)(b >> 16);
    d[5] = (Uint8)(b >> 8);
    d[7] = (Uint8)b;
}
#endif

/* video.c: the renderer's maps as its pages have them, what it reduces
 * with and the colours it has seen, for the tests */
const struct stdl_sdl2_st_maps *stdl_sdl2_st_view(SDL_Renderer *renderer,
                                                  int *bits, int *dither,
                                                  const Uint8 **seen);

#endif /* STDL_SDL2_ST_H */
