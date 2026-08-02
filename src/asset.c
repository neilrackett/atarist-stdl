/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_Asset: the chunked bank container written by stdlconv.
 * Everything is big-endian (68k native); see docs/format.md.
 *
 * Layout:
 *   "STDL" u16 version(1) u16 nchunks
 *   nchunks * dir entry: u16 type, u16 id, u32 offset, u32 length
 *   payloads...
 * Chunk types: 1 palette, 2 surface, 3 sprite, 4 tileset, 5 font.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "stdl_internal.h"

/*
 * GEMDOS stores 8.3 uppercase names; ported code habitually opens
 * "icon.bmp". Try the path as given, then retry fully uppercased.
 * (Returns FILE* as void* so stdl_internal.h needn't pull stdio
 * into every module.)
 */
void *stdl_fopen_ci(const char *path, const char *mode)
{
    FILE *f = fopen(path, mode);
    char upper[256];
    size_t i, len;

    if (f != NULL) {
        return f;
    }
    len = strlen(path);
    if (len >= sizeof(upper)) {
        return NULL;
    }
    for (i = 0; i <= len; i++) {
        upper[i] = (char)toupper((unsigned char)path[i]);
    }
    if (strcmp(upper, path) == 0) {
        return NULL;            /* already uppercase: a real miss */
    }
    return fopen(upper, mode);
}

#define CHUNK_PALETTE 1
#define CHUNK_SURFACE 2
#define CHUNK_SPRITE  3
#define CHUNK_TILESET 4
#define CHUNK_FONT    5

/* Find chunk (type, id) in a bank and return its payload, malloc'd.
 * id -1 matches the first chunk of the type. */
static uint8_t *load_chunk(const char *bank, int type, int id,
                           uint32_t *out_len)
{
    FILE *f = stdl_fopen_ci(bank, "rb");
    uint8_t head[8];
    uint8_t *dir = NULL, *payload = NULL;
    uint16_t nchunks;
    int i;

    if (f == NULL) {
        STDL_SetError("cannot open asset bank");
        return NULL;
    }
    if (fread(head, 1, 8, f) != 8 || memcmp(head, "STDL", 4) != 0
        || stdl_rd16(head + 4) != 1) {
        STDL_SetError("not an STDL bank");
        goto fail;
    }
    nchunks = stdl_rd16(head + 6);
    dir = malloc((size_t)nchunks * 12);
    if (dir == NULL
        || fread(dir, 1, (size_t)nchunks * 12, f)
           != (size_t)nchunks * 12) {
        STDL_SetError("truncated bank directory");
        goto fail;
    }
    for (i = 0; i < nchunks; i++) {
        const uint8_t *e = dir + i * 12;
        if (stdl_rd16(e) == type && (id < 0 || stdl_rd16(e + 2) == id)) {
            uint32_t off = stdl_rd32(e + 4);
            uint32_t len = stdl_rd32(e + 8);
            payload = malloc(len);
            if (payload == NULL || fseek(f, (long)off, SEEK_SET) != 0
                || fread(payload, 1, len, f) != len) {
                free(payload);
                payload = NULL;
                STDL_SetError("truncated bank chunk");
                goto fail;
            }
            *out_len = len;
            break;
        }
    }
    if (payload == NULL) {
        STDL_SetError("chunk not found in bank");
    }
fail:
    free(dir);
    fclose(f);
    return payload;
}

int STDL_LoadPalette(const char *bank, int id, STDL_Palette *out)
{
    uint32_t len;
    uint8_t *p = load_chunk(bank, CHUNK_PALETTE, id, &len);
    uint16_t n;
    int i;

    if (p == NULL) {
        return -1;
    }
    n = stdl_rd16(p);
    if (len < 2u + n * 4u || out == NULL || out->colors == NULL) {
        free(p);
        STDL_SetError("bad palette chunk");
        return -1;
    }
    if (n > (uint16_t)out->ncolors) {
        n = (uint16_t)out->ncolors;
    }
    for (i = 0; i < n; i++) {
        out->colors[i].r = p[2 + i * 4];
        out->colors[i].g = p[3 + i * 4];
        out->colors[i].b = p[4 + i * 4];
        out->colors[i].unused = 0;
    }
    free(p);
    return n;
}

/* surface payload: u16 w, u16 h, u8 haskey, u8 key, planar data */
STDL_Surface *STDL_LoadSurface(const char *bank, int id)
{
    uint32_t len;
    uint8_t *p = load_chunk(bank, CHUNK_SURFACE, id, &len);
    STDL_Surface *s = NULL;
    uint16_t w, h;
    uint32_t datalen;

    if (p == NULL) {
        return NULL;
    }
    w = stdl_rd16(p);
    h = stdl_rd16(p + 2);
    datalen = (uint32_t)((w + 15) >> 4) * 8 * h;
    if (len < 6 + datalen) {
        STDL_SetError("bad surface chunk");
        free(p);
        return NULL;
    }
    s = STDL_CreateSurface(w, h);
    if (s != NULL) {
        memcpy(s->pixels, p + 6, datalen);
        if (p[4]) {
            STDL_SetColourKey(s, 1, p[5]);
        }
    }
    free(p);
    return s;
}

/* sprite payload: u16 w,h,nframes, u8 nvariants, u8 planes,
 * u16 groups, u32 framesize(words), data words */
STDL_Sprite *STDL_LoadSprite(const char *bank, int id, uint32_t flags)
{
    uint32_t len;
    uint8_t *p = load_chunk(bank, CHUNK_SPRITE, id, &len);
    STDL_Sprite *spr;
    uint32_t datawords;

    if (p == NULL) {
        return NULL;
    }
    spr = calloc(1, sizeof(STDL_Sprite));
    if (spr == NULL) {
        free(p);
        STDL_SetError("out of memory");
        return NULL;
    }
    spr->w = (int16_t)stdl_rd16(p);
    spr->h = (int16_t)stdl_rd16(p + 2);
    spr->nframes = stdl_rd16(p + 4);
    spr->nvariants = p[6];
    spr->planes = p[7];
    spr->groups = stdl_rd16(p + 8);
    spr->framesize = stdl_rd32(p + 10);
    datawords = spr->framesize * spr->nframes * spr->nvariants;
    if (len < 14 + datawords * 2) {
        STDL_SetError("bad sprite chunk");
        free(p);
        free(spr);
        return NULL;
    }
    spr->data = malloc(datawords * 2);
    if (spr->data == NULL) {
        STDL_SetError("out of memory");
        free(p);
        free(spr);
        return NULL;
    }
    memcpy(spr->data, p + 14, datawords * 2);
    free(p);

    /* expand to 16 variants on demand */
    if ((flags & STDL_PRESHIFT) && spr->nvariants == 1) {
        STDL_Sprite *ps = stdl_sprite_preshift(spr);
        if (ps == NULL) {
            STDL_FreeSprite(spr);
            return NULL;
        }
        spr = ps;
    }
    return spr;
}

/* tileset payload: u16 tw,th,ntiles,groups, u8 masked, u8 planes,
 * u32 tilesize(words), data words */
STDL_Tileset *STDL_LoadTileset(const char *bank, int id)
{
    uint32_t len;
    uint8_t *p = load_chunk(bank, CHUNK_TILESET, id, &len);
    STDL_Tileset *ts;
    uint32_t datawords;

    if (p == NULL) {
        return NULL;
    }
    ts = calloc(1, sizeof(STDL_Tileset));
    if (ts == NULL) {
        free(p);
        STDL_SetError("out of memory");
        return NULL;
    }
    ts->tw = (int16_t)stdl_rd16(p);
    ts->th = (int16_t)stdl_rd16(p + 2);
    ts->ntiles = stdl_rd16(p + 4);
    ts->groups = stdl_rd16(p + 6);
    ts->masked = p[8];
    ts->planes = p[9];
    ts->tilesize = stdl_rd32(p + 10);
    datawords = ts->tilesize * ts->ntiles;
    if (len < 14 + datawords * 2) {
        STDL_SetError("bad tileset chunk");
        free(p);
        free(ts);
        return NULL;
    }
    ts->data = malloc(datawords * 2);
    if (ts->data == NULL) {
        STDL_SetError("out of memory");
        free(p);
        free(ts);
        return NULL;
    }
    memcpy(ts->data, p + 14, datawords * 2);
    free(p);
    return ts;
}

/* font payload: u16 cw,ch, u8 first,last, u16 bytes_per_row, bits */
STDL_Font *STDL_LoadFont(const char *bank, int id)
{
    uint32_t len;
    uint8_t *p = load_chunk(bank, CHUNK_FONT, id, &len);
    STDL_Font *font;
    uint32_t bits_len;

    if (p == NULL) {
        return NULL;
    }
    font = calloc(1, sizeof(STDL_Font));
    if (font == NULL) {
        free(p);
        STDL_SetError("out of memory");
        return NULL;
    }
    font->cw = (int16_t)stdl_rd16(p);
    font->ch = (int16_t)stdl_rd16(p + 2);
    font->first = p[4];
    font->last = p[5];
    font->bytes_per_row = stdl_rd16(p + 6);
    bits_len = (uint32_t)(font->last - font->first + 1)
             * font->bytes_per_row * font->ch;
    if (len < 8 + bits_len) {
        STDL_SetError("bad font chunk");
        free(p);
        free(font);
        return NULL;
    }
    font->bits = malloc(bits_len);
    if (font->bits == NULL) {
        STDL_SetError("out of memory");
        free(p);
        free(font);
        return NULL;
    }
    memcpy(font->bits, p + 8, bits_len);
    free(p);
    return font;
}
