/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Sprites, tilesets, fonts, and the on-disk asset container
 * produced by tools/stdlconv (see docs/format.md for the layout).
 */

#ifndef STDL_ASSET_H
#define STDL_ASSET_H

#include <stdl/stdl_types.h>

/*
 * Sprite storage, per 16-pixel group, in draw order:
 *   [ mask ][ plane0 ][ plane1 ][ plane2 ][ plane3 ]
 * mask is 1 where the destination is preserved, so the inner loop is
 *   *dst = (*dst & mask) | *src;
 * Pre-shifted sprites store 16 complete variants, one per x & 15,
 * each padded one extra group horizontally.
 */
struct STDL_Sprite {
    int16_t   w, h;         /* visible pixel size                     */
    uint16_t  groups;       /* 16px groups per row (padded variant)   */
    uint16_t  nframes;
    uint8_t   nvariants;    /* 1, or 16 when pre-shifted              */
    uint8_t   planes;
    uint32_t  framesize;    /* words per frame in one variant         */
    uint16_t *data;
};

struct STDL_Tileset {
    int16_t   tw, th;       /* tile size; tw is a multiple of 16      */
    uint16_t  ntiles;
    uint16_t  groups;       /* groups per tile row                    */
    uint8_t   masked;       /* tiles carry masks like sprites         */
    uint8_t   planes;
    uint32_t  tilesize;     /* words per tile                         */
    uint16_t *data;
};

struct STDL_Font {
    int16_t   cw, ch;       /* fixed cell size                        */
    uint8_t   first, last;  /* covered ASCII range                    */
    uint16_t  bytes_per_row;
    uint8_t  *bits;         /* 1bpp glyph rows, glyph-major           */
};

#define STDL_PRESHIFT 0x0001u  /* 16x RAM for zero-cost unaligned use */

/* Build sprites/tilesets in RAM from a surface (mask from colourkey
 * when the surface has one). */
STDL_Sprite  *STDL_SpriteFromSurface(const STDL_Surface *s,
                                     int frame_w, uint32_t flags);
STDL_Tileset *STDL_TilesetFromSurface(const STDL_Surface *s,
                                      int tw, int th);
void STDL_FreeSprite(STDL_Sprite *spr);
void STDL_FreeTileset(STDL_Tileset *ts);
void STDL_FreeFont(STDL_Font *font);

/* Asset container: chunked "STDL" bank file written by stdlconv. */
STDL_Sprite  *STDL_LoadSprite(const char *bank, int id, uint32_t flags);
STDL_Tileset *STDL_LoadTileset(const char *bank, int id);
STDL_Font    *STDL_LoadFont(const char *bank, int id);
STDL_Surface *STDL_LoadSurface(const char *bank, int id);
int           STDL_LoadPalette(const char *bank, int id,
                               STDL_Palette *out);

void STDL_DrawText(STDL_Surface *dst, const STDL_Font *font,
                   int x, int y, const char *text, uint8_t col);

/* One glyph. Identical output to a one-character STDL_DrawText, but
 * without the string: use it wherever ported text code draws a
 * character at a time (status bars, scores, menus) rather than
 * assembling a buffer per call. Characters outside the font's range
 * draw nothing. */
void STDL_DrawChar(STDL_Surface *dst, const STDL_Font *font,
                   int x, int y, int ch, uint8_t col);

/* Load-time BMP reader: uncompressed 1/4/8bpp indexed BMPs with at
 * most 16 used colours (a one-off conversion, not a rendering
 * path; anything richer goes through stdlconv offline). */
STDL_Surface *STDL_LoadBMP(const char *file);

/* Degas PI1 pictures (uncompressed low-res). LoadDegas returns a
 * surface with the picture's palette in its format (and optionally
 * copied to pal_out); ShowDegas splats the picture onto the current
 * draw page and programs its palette - the classic loading splash. */
STDL_Surface *STDL_LoadDegas(const char *file, STDL_Palette *pal_out);
int           STDL_ShowDegas(const char *file);

#endif /* STDL_ASSET_H */
