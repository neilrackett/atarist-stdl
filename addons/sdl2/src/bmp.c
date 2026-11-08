/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * SDL_LoadBMP_RW: an uncompressed indexed BMP, 1, 4 or 8 bits a pixel,
 * bottom-up or top-down, with Windows' 40-byte header or a later one
 * (V2-V5, 52-124 bytes, of which the first 40 are read), as an
 * SDL_PIXELFORMAT_INDEX8 surface with the file's palette, its ncolors
 * the file's count, as SDL2 gives it. SDL2 also reads RLE and 16-32
 * bits; here those are refused, in SDL2's own words, as the one
 * surface format is 8-bit and `stdlconv bmp8` makes the files this
 * takes. So is OS/2's 12-byte header, which SDL2 reads: it cost 120
 * bytes, and stdlconv, Pillow and GIMP write 40 or more.
 *
 * An object of its own, so a program that never loads one links none
 * of it, and built -Os (sdl2.mk): it runs once a file, and -O2 made it
 * 1,280 bytes against 1,036. 1- and 4-bit files cost 140 of them.
 */

#include "internal.h"

SDL_Surface *SDL_LoadBMP_RW(SDL_RWops *src, int freesrc)
{
    /* the file header (14), and the first 40 of the info header's */
    Uint8 h[14 + 40];
    /* what a failure says, set just before it; NULL leaves the error
     * a call made (the surface's, a file's seek) standing */
    const char *why = "Error reading from datastream";
    SDL_Surface *s = NULL;
    SDL_Color *c;
    Uint8 *p;
    long start;
    Uint32 hsize, n;
    int w = 0, ht = 0, bits, row, step, y, x;
    if (!src) {
        return NULL;            /* the stream's own error stands */
    }
    start = (long)SDL_RWtell(src);
    if (SDL_RWread(src, h, 18, 1) != 1) {
        goto fail;
    }
    if (h[0] != 'B' || h[1] != 'M') {
        why = "File is not a Windows BMP file";
        goto fail;
    }
    hsize = stdl_sdl2_le32(h + 14);
    if (hsize >= 40) {
        if (SDL_RWread(src, h + 18, 36, 1) != 1) {
            goto fail;
        }
        w = (int)stdl_sdl2_le32(h + 18);
        ht = (int)stdl_sdl2_le32(h + 22);
    }
    /* a header of another size leaves 0x0, as SDL2 says of one it does
     * not know; 32,767 a side keeps pitch * height inside 32 bits */
    if (w <= 0 || w > 32767 || ht == 0 || ht < -32767 || ht > 32767) {
        why = "BMP file with bad dimensions (%dx%d)";
        goto fail;
    }
    bits = stdl_sdl2_le16(h + 28);
    if (bits != 1 && bits != 4 && bits != 8) {
        why = "%d-bpp BMP images are not supported";
        w = bits;               /* the message's one number */
        goto fail;
    }
    if (stdl_sdl2_le32(h + 30) != 0) {
        why = "Compressed BMP files not supported";
        goto fail;
    }
    /* as SDL2: no count, or more than a surface holds, is them all */
    n = stdl_sdl2_le32(h + 46);
    if (n == 0 || n > 256) {
        n = 1u << bits;
    }
    s = SDL_CreateRGBSurface(0, w, ht < 0 ? -ht : ht, 8, 0, 0, 0, 0);
    if (!s) {
        why = NULL;
        goto fail;
    }

    /* the palette straight after the header, BGR and a reserved byte
     * an entry, read into the colours and turned round in place */
    c = s->format->palette->colors;
    if (SDL_RWseek(src, start + 14 + (long)hsize, RW_SEEK_SET) < 0) {
        why = NULL;
        goto fail;
    }
    if (SDL_RWread(src, c, 4, n) != n) {
        goto fail;
    }
    for (x = 0; x < (int)n; x++) {
        const Uint8 b = c[x].r;
        c[x].r = c[x].b;
        c[x].b = b;
        c[x].a = SDL_ALPHA_OPAQUE;
    }
    s->format->palette->ncolors = (int)n;

    /* the pixels, a row padded to 4 bytes: an 8-bit row is exactly a
     * surface's pitch, and a 1- or 4-bit one shorter, so each is read
     * straight into its row and spread out there */
    if (SDL_RWseek(src, start + (long)stdl_sdl2_le32(h + 10), RW_SEEK_SET) < 0) {
        why = NULL;
        goto fail;
    }
    row = ((w * bits + 31) >> 5) << 2;
    p = (Uint8 *)s->pixels;
    step = s->pitch;
    if (ht > 0) {               /* bottom-up, as most are */
        p += (ht - 1) * step;
        step = -step;
    }
    for (y = s->h; y > 0; y--, p += step) {
        if (SDL_RWread(src, p, (size_t)row, 1) != 1) {
            goto fail;
        }
        /* from the last pixel back: pixel x comes from byte
         * x * bits / 8, never past x, so it is read before anything
         * is written over it */
        if (bits < 8) {
            const int mask = (1 << bits) - 1;
            for (x = w; x-- > 0;) {
                const int at = x * bits;
                p[x] = (Uint8)(p[at >> 3] >> (8 - bits - (at & 7)) & mask);
            }
        }
    }
    goto done;

fail:
    if (why) {
        SDL_SetError(why, w, ht);
    }
    SDL_FreeSurface(s);
    s = NULL;
done:
    if (freesrc) {
        SDL_RWclose(src);
    }
    return s;
}
