/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Load-time BMP reader for the compat layer and simple ports:
 * uncompressed 1/4/8bpp indexed BMPs with at most 16 used colours.
 * This is a one-off conversion at load, not a rendering path;
 * anything richer should go through stdlconv offline.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stdl_internal.h"

STDL_Surface *STDL_LoadBMP(const char *file);

STDL_Surface *STDL_LoadBMP(const char *file)
{
    FILE *f = stdl_fopen_ci(file, "rb");
    uint8_t head[54];
    uint8_t palraw[256 * 4];
    uint8_t *rowbuf = NULL;
    STDL_Surface *s = NULL;
    uint32_t dataoff, hdrsize, compression;
    int32_t w, h;
    int bpp, topdown, rowbytes, ncolors, y;

    if (f == NULL) {
        STDL_SetError("cannot open BMP");
        return NULL;
    }
    if (fread(head, 1, 54, f) != 54 || head[0] != 'B'
        || head[1] != 'M') {
        STDL_SetError("not a BMP file");
        goto out;
    }
    dataoff = stdl_le32(head + 10);
    hdrsize = stdl_le32(head + 14);
    if (hdrsize < 40) {
        STDL_SetError("unsupported BMP header");
        goto out;
    }
    w = (int32_t)stdl_le32(head + 18);
    h = (int32_t)stdl_le32(head + 22);
    bpp = stdl_le16(head + 28);
    compression = stdl_le32(head + 30);
    topdown = 0;
    if (h < 0) {
        h = -h;
        topdown = 1;
    }
    if (compression != 0
        || (bpp != 1 && bpp != 4 && bpp != 8)
        || w <= 0 || h <= 0 || w > 2048 || h > 2048) {
        STDL_SetError("unsupported BMP (need uncompressed 1/4/8bpp; "
                      "convert with stdlconv)");
        goto out;
    }

    ncolors = (int)stdl_le32(head + 46);
    if (ncolors == 0) {
        ncolors = 1 << bpp;
    }
    if (ncolors > 256) {
        ncolors = 256;
    }
    if (fseek(f, (long)(14 + hdrsize), SEEK_SET) != 0
        || fread(palraw, 4, (size_t)ncolors, f) != (size_t)ncolors) {
        STDL_SetError("truncated BMP palette");
        goto out;
    }

    s = STDL_CreateSurface((int)w, (int)h);
    if (s == NULL) {
        goto out;
    }
    {
        int i, n = ncolors > 16 ? 16 : ncolors;
        for (i = 0; i < n; i++) {
            s->format->palette->colors[i].b = palraw[i * 4 + 0];
            s->format->palette->colors[i].g = palraw[i * 4 + 1];
            s->format->palette->colors[i].r = palraw[i * 4 + 2];
        }
        s->format->palette->ncolors = n;
    }

    rowbytes = (int)(((uint32_t)w * bpp + 31) / 32 * 4);
    rowbuf = malloc((size_t)rowbytes);
    if (rowbuf == NULL || fseek(f, (long)dataoff, SEEK_SET) != 0) {
        STDL_SetError("out of memory");
        STDL_FreeSurface(s);
        s = NULL;
        goto out;
    }

    for (y = 0; y < h; y++) {
        int dy = topdown ? y : (int)h - 1 - y;
        uint16_t *drow =
            (uint16_t *)(s->pixels + (uint32_t)dy * s->stride);
        int x;

        if (fread(rowbuf, 1, (size_t)rowbytes, f)
            != (size_t)rowbytes) {
            STDL_SetError("truncated BMP data");
            STDL_FreeSurface(s);
            s = NULL;
            goto out;
        }
        for (x = 0; x < w; x++) {
            uint8_t idx;
            uint16_t bit;
            uint16_t *grp;
            int p;

            if (bpp == 8) {
                idx = rowbuf[x];
            } else if (bpp == 4) {
                idx = (uint8_t)((x & 1)
                    ? (rowbuf[x >> 1] & 0x0F)
                    : (rowbuf[x >> 1] >> 4));
            } else {
                idx = (uint8_t)
                    ((rowbuf[x >> 3] >> (7 - (x & 7))) & 1);
            }
            if (idx > 15) {
                STDL_SetError("BMP uses more than 16 colours; "
                              "convert with stdlconv");
                STDL_FreeSurface(s);
                s = NULL;
                goto out;
            }
            bit = (uint16_t)(0x8000u >> (x & 15));
            grp = drow + (x >> 4) * 4;
            for (p = 0; p < 4; p++) {
                if (idx & (1 << p)) {
                    grp[p] |= bit;
                }
            }
        }
    }

out:
    free(rowbuf);
    fclose(f);
    return s;
}
