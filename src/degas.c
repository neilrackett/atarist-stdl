/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Degas PI1 pictures: the ST-native full-screen image format
 * (2-byte resolution, 16 hardware palette words, 32000 bytes of
 * planar data - our screen layout exactly). The classic use is a
 * splash screen shown while the game data loads.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stdl_internal.h"

/* ST/STE hardware palette word -> RGB888 */
static void hw_to_rgb(uint16_t hw, STDL_Colour *c)
{
    int i;
    uint8_t v[3];

    for (i = 0; i < 3; i++) {
        uint8_t n = (uint8_t)((hw >> (8 - i * 4)) & 0x0F);
        v[i] = (uint8_t)((((n & 7) << 1) | ((n >> 3) & 1)) * 17);
    }
    c->r = v[0];
    c->g = v[1];
    c->b = v[2];
    c->unused = 0;
}

#define PI1_SIZE (34L + 32000L)

/* trailer != 0: the picture is the last PI1_SIZE bytes of the file
 * rather than the whole of it */
static uint8_t *load_pi1_from(const char *file, int trailer,
                              uint16_t hwpal[16])
{
    FILE *f = stdl_fopen_ci(file, "rb");
    uint8_t head[34];
    uint8_t *data;
    int i;

    if (f == NULL) {
        STDL_SetError("cannot open Degas picture");
        return NULL;
    }
    if (trailer && fseek(f, -PI1_SIZE, SEEK_END) != 0) {
        STDL_SetError("file too short for a Degas trailer");
        fclose(f);
        return NULL;
    }
    if (fread(head, 1, 34, f) != 34
        || stdl_rd16(head) != 0) {
        STDL_SetError("not an uncompressed low-res Degas PI1");
        fclose(f);
        return NULL;
    }
    for (i = 0; i < 16; i++) {
        hwpal[i] = stdl_rd16(head + 2 + i * 2);
    }
    data = malloc(32000);
    if (data == NULL || fread(data, 1, 32000, f) != 32000) {
        STDL_SetError("truncated Degas picture");
        free(data);
        fclose(f);
        return NULL;
    }
    fclose(f);
    return data;
}

static STDL_Surface *load_degas(const char *file, int trailer,
                                STDL_Palette *pal_out)
{
    uint16_t hwpal[16];
    uint8_t *data = load_pi1_from(file, trailer, hwpal);
    STDL_Surface *s;
    int i;

    if (data == NULL) {
        return NULL;
    }
    s = STDL_CreateSurface(320, 200);
    if (s != NULL) {
        memcpy(s->pixels, data, 32000);
        stdl_planes_normalise(s->pixels, s->stride, s->h);
        for (i = 0; i < 16; i++) {
            hw_to_rgb(hwpal[i], &s->format->palette->colors[i]);
        }
        if (pal_out != NULL && pal_out->colors != NULL) {
            for (i = 0; i < 16 && i < pal_out->ncolors; i++) {
                pal_out->colors[i] =
                    s->format->palette->colors[i];
            }
        }
    }
    free(data);
    return s;
}

STDL_Surface *STDL_LoadDegas(const char *file, STDL_Palette *pal_out)
{
    return load_degas(file, 0, pal_out);
}

STDL_Surface *STDL_LoadDegasTrailer(const char *file,
                                    STDL_Palette *pal_out)
{
    return load_degas(file, 1, pal_out);
}

static int show_degas(const char *file, int trailer)
{
    uint16_t hwpal[16];
    uint8_t *data;
    STDL_Surface *screen = STDL_GetVideoSurface();
    int i;

    if (screen == NULL) {
        STDL_SetError("no video mode set");
        return -1;
    }
    data = load_pi1_from(file, trailer, hwpal);
    if (data == NULL) {
        return -1;
    }
    memcpy(screen->pixels, data, 32000);
    stdl_planes_normalise(screen->pixels, screen->stride, screen->h);
    free(data);
    for (i = 0; i < 16; i++) {
        hw_to_rgb(hwpal[i], &stdl.colours[i]);
        STDL_SetColour(i, hwpal[i]);
    }
    return 0;
}

/*
 * Splash convenience: copy the picture straight onto the current
 * draw page and program its palette (hardware and logical). Call
 * after STDL_SetVideoMode; with double buffering, follow with
 * STDL_Flip to show it.
 */
int STDL_ShowDegas(const char *file)
{
    return show_degas(file, 0);
}

/*
 * The same, but the picture is the last 32034 bytes of an arbitrary
 * file - classically the program's own .TOS/.PRG, with the PI1
 * simply appended after the executable image (cat PROG.TOS SPLASH.PI1).
 * GEMDOS loads only the segments named in the program header, so a
 * splash shipped this way needs no separate file and no RAM.
 */
int STDL_ShowDegasTrailer(const char *file)
{
    return show_degas(file, 1);
}
