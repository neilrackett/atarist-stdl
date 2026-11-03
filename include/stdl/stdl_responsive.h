/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Responsive programs: an ST program that also uses what a better
 * machine has, where it has it, asking for each thing by name. Ask
 * once, before the first STDL_SetVideoMode:
 *
 *     STDL_UseResponsive(STDL_RESPONSIVE_COLOUR | STDL_RESPONSIVE_TALL);
 *     screen = STDL_SetVideoMode(320, 240, 8, STDL_DOUBLEBUF);
 *
 * and branch on what the screen says: format->BitsPerPixel (8, or 4)
 * and h (240, or 200). Each option is a promise that the program
 * handles what it brings; nothing changes for a program that does not
 * ask, and a later release adds options rather than changing these.
 *
 * STDL_RESPONSIVE_COLOUR: 256 colours, asked for with a bpp of 8.
 *   - A TT: TT Low, each row on two of its 480 lines; 200 rows centred
 *     in 240, unless the screen is tall.
 *   - A Falcon: an 8-bit mode, 240 lines on VGA (the VIDEL doubles
 *     them), on RGB or a TV 200 or, tall, 240 in overscan, 384 pixels
 *     wide with the picture centred.
 *   - An ST, an STE or a Mega STE: its 16 colours, as without it.
 *   Every surface created while the screen is up has 8 interleaved
 *   planes, the display's own layout, so the screen is the display
 *   page itself; Flip copies what changed to the doubled lines on a TT
 *   and has nothing to do on a Falcon. Colours run 0-255 in every call
 *   that takes one; 4-plane data drawn onto one (a surface or a sprite
 *   made before the screen, an image a loader returns, STDL_PutGroup)
 *   keeps its colours 0-15. STDL_TRANSPARENT keeps its 4-plane meaning
 *   on a surface with a mask: a fill, line, pixel or polygon SET in it
 *   punches a hole over colour 0, and a key of it keeps the mask
 *   (stdl_surface.h), so a program's composition draws the same on
 *   both. On a surface with no mask, the screen among them, 16 is a
 *   colour like any other.
 *
 * STDL_RESPONSIVE_TALL: a 320x240 screen, asked for with a height over
 *   200, on every machine.
 *   - An ST, an STE or a Mega STE: its own screen with the bottom border
 *     open (stdl_overscan.h), 240 of the 245 lines; 200 if the border
 *     cannot open (rasters already on Timer B), with the error set.
 *   - A TT: TT Low; without the colour, a 4-plane surface that Flip
 *     compares with the display and copies across where it changed.
 *   - A Falcon: a VIDEL mode of 240 lines, 4 bits without the colour,
 *     in the ST's own plane layout.
 *   Without the colour the screen is an ordinary 4-plane surface
 *   everywhere: every call works on it, and so do a program's own
 *   writes to its pixels.
 *
 * STDL_RESPONSIVE_BANKS: with the colour, a bank's 256-colour art.
 *   On an 8-plane screen STDL_LoadSurface, STDL_LoadSprite,
 *   STDL_LoadTileset and STDL_LoadPalette look for the id's 8-plane
 *   version first - its own art and 256 colours, or planes 4-7 over
 *   the ST's art of the same id (docs/format.md section 6.1, stdlconv's
 *   depth=) - and give the ST's chunk, colours 0-15, where the bank has
 *   neither. An ST loads only its own chunks. 2.1K, linked by this
 *   option alone.
 *
 * STDL_RESPONSIVE_HEIGHT: the display's own height, asked for as the
 *   tall screen is. A TT and a Falcon give 320x240 as above, on a
 *   Falcon's RGB or TV in overscan too; an ST, an STE or a Mega STE
 *   keeps its 200 lines and opens no border, with no error set, so
 *   every machine shows a whole screen of its own pixels. It links
 *   none of the border code, 8.7K of a program. Asked for with
 *   STDL_RESPONSIVE_TALL, the tall screen wins.
 *
 * A responsive screen on a TT or a Falcon is always double-buffered,
 * and with STDL_TRIPLEBUF it has a third page, as an ST's screen has
 * (stdl_video.h): another of the display's, 153.6K of ST RAM on a TT
 * (and 38.4K more for the tall 16-colour screen's own copy), 64-92K on
 * a Falcon in 256 colours and 38-46K in 16. Without the memory it has
 * two, its flags say so and the error says why.
 * Its colours 0-15 start as an ST screen's do, and every palette call
 * reaches the TT's and the Falcon's own registers, staged ones and
 * STDL_SetColour too; a word for the ST's registers sets its entry at
 * the ST's 4 bits a channel.
 *
 * A surface over a program's own block of 8 planes is
 * STDL_CreateSurfaceFrom8Planes's (stdl_surface.h): a stride that is a
 * whole number of 16-byte groups, the caller's mask, if any, its key
 * and priority plane, the block the caller's to free;
 * STDL_CreateSurfaceFrom stays 4 planes whatever the screen. The blit
 * flags (STDL_BlitSurfaceEx's UNDER and MARK) work onto an 8-plane
 * surface, from 8 planes or from 4, through a CPU path of their own.
 *
 * A tileset cut from an 8-plane surface has 8 planes, and a 4-plane
 * tileset's tiles draw onto one in colours 0-15, as its sprites do.
 *
 * What an 8-plane surface does not take: a blit from it onto a 4-plane
 * surface, or one of its sprites or tiles onto one; patterned polygons;
 * the cursor on a responsive screen. Each sets the error and draws
 * nothing. The first has no right answer
 * - 4 planes hold no colour above 15 - and stays refused; the others
 * are 4-plane formats and kernels not built for 8 planes yet, which a
 * later release can add when a program needs them (docs/limits.md).
 * Plane budgets apply to 4-plane surfaces alone. A TT or a Falcon has
 * no borders, rasters or hardware scrolling for any screen
 * (stdl_overscan.h, stdl_raster.h, stdl_hwscroll.h).
 *
 * The mode set moves the console's cursor below the rows a centred
 * screen leaves above it, so text printed after it lands in the
 * picture, as on an ST; text a program puts there with its own cursor
 * escapes lands where it says. On a TT the console writes the display,
 * not the screen alone: the Flip after it has printed shows what the
 * screen holds and clears what is round it. A tall 16-colour screen
 * never holds the text, so the text goes; on a 256-colour one it stays
 * until the program draws over it, as on an ST.
 *
 * STDL_UpdateRects tells an 8-plane screen what a program wrote to its
 * pixels itself, as SDL_UpdateRect would; the library's own calls need
 * nothing. What such a program writes into:
 *   - the page drawn, which is the next one after each Flip: read
 *     pixels again then;
 *   - a row every stride bytes, 16 bytes a group of 16 pixels (planes
 *     0-7 a word each);
 *   - on a TT, a stride of two display lines, the row's second line
 *     stride / 2 on. Flip copies a row to its second line only where
 *     the library drew or STDL_UpdateRects reported (one box a page,
 *     around everything reported; none named is the whole screen), so
 *     a program may write both lines itself and report nothing;
 *   - on a Falcon, the display itself, nothing to report; on RGB or a
 *     TV with 240 rows, 384-pixel lines with pixels 32 bytes into one.
 * Staged (STDL_StagePalette), colours set just before Flip are shown
 * with its page, triple-buffered too (stdl_palette.h).
 */

#ifndef STDL_RESPONSIVE_H
#define STDL_RESPONSIVE_H

#include <stdl/stdl_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* the things a program can ask for; one call may ask for several */
#define STDL_RESPONSIVE_COLOUR 0x0001u
#define STDL_RESPONSIVE_TALL   0x0002u
#define STDL_RESPONSIVE_HEIGHT 0x0004u
#define STDL_RESPONSIVE_BANKS 0x0008u

/*
 * 0, or -1 for an option this library does not know, or while a
 * responsive screen is up (ask before the mode set). Inline so that a
 * constant `what` links the code of the options it names and no other:
 * the colour's 8-plane routines and the tall screen's border code are
 * each several kilobytes.
 */
struct stdl_resp_model;
struct stdl_resp_banks;
extern const struct stdl_resp_model stdl_resp_colour, stdl_resp_tall;
extern const struct stdl_resp_banks stdl_resp_banks;
void stdl_resp_tall_st(int h);
int stdl_use_responsive(unsigned what, const struct stdl_resp_model *colour,
                        const struct stdl_resp_model *tall,
                        void (*st_tall)(int h),
                        const struct stdl_resp_banks *banks);

/* the height and the tall screen are one screen on a TT and a Falcon;
 * only the tall one opens an ST's border. Always inline: gcc 4.6 left
 * one copy of it out of line in a program that called it twice from one
 * large function, its `what` a variable there, and so linked every
 * option - the ST's border code, 8.7K, in a program that never asked */
static __inline__ __attribute__((always_inline))
int STDL_UseResponsive(unsigned what)
{
    return stdl_use_responsive(what,
        (what & STDL_RESPONSIVE_COLOUR) ? &stdl_resp_colour : 0,
        (what & (STDL_RESPONSIVE_TALL | STDL_RESPONSIVE_HEIGHT))
            ? &stdl_resp_tall : 0,
        (what & STDL_RESPONSIVE_TALL) ? stdl_resp_tall_st : 0,
        /* the art is the colour's: alone it could never load */
        (what & STDL_RESPONSIVE_BANKS) && (what & STDL_RESPONSIVE_COLOUR)
            ? &stdl_resp_banks : 0);
}

#ifdef __cplusplus
}
#endif

#endif /* STDL_RESPONSIVE_H */
