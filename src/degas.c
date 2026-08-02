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

static uint8_t *load_pi1(const char *file, uint16_t hwpal[16])
{
    FILE *f = stdl_fopen_ci(file, "rb");
    uint8_t head[34];
    uint8_t *data;
    int i;

    if (f == NULL) {
        STDL_SetError("cannot open Degas picture");
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

STDL_Surface *STDL_LoadDegas(const char *file, STDL_Palette *pal_out)
{
    uint16_t hwpal[16];
    uint8_t *data = load_pi1(file, hwpal);
    STDL_Surface *s;
    int i;

    if (data == NULL) {
        return NULL;
    }
    s = STDL_CreateSurface(320, 200);
    if (s != NULL) {
        memcpy(s->pixels, data, 32000);
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

/*
 * Splash convenience: copy the picture straight onto the current
 * draw page and program its palette (hardware and logical). Call
 * after STDL_SetVideoMode; with double buffering, follow with
 * STDL_Flip to show it.
 */
int STDL_ShowDegas(const char *file)
{
    uint16_t hwpal[16];
    uint8_t *data;
    STDL_Surface *screen = STDL_GetVideoSurface();
    int i;

    if (screen == NULL) {
        STDL_SetError("no video mode set");
        return -1;
    }
    data = load_pi1(file, hwpal);
    if (data == NULL) {
        return -1;
    }
    memcpy(screen->pixels, data, 32000);
    free(data);
    for (i = 0; i < 16; i++) {
        hw_to_rgb(hwpal[i], &stdl.colours[i]);
        STDL_SetColour(i, hwpal[i]);
    }
    return 0;
}
