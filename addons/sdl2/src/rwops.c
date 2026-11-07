/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Streams: a stdio file, or a block of memory, read-only or not, and
 * the endian reads and writes over either. Offsets are SDL2's 64 bits,
 * held to 32 inside, where a TOS file and a TT's memory both fit.
 */

#include "internal.h"

/* ------------------------------------------------------------------ */
/* files                                                               */

static Sint64 file_seek(SDL_RWops *c, Sint64 offset, int whence)
{
    static const int how[3] = { SEEK_SET, SEEK_CUR, SEEK_END };
    if (whence < 0 || whence > 2) {
        return SDL_SetError("Unknown value for 'whence'");
    }
    if (fseek(c->hidden.stdio.fp, (long)offset, how[whence]) != 0) {
        return SDL_SetError("Couldn't seek in the file");
    }
    return ftell(c->hidden.stdio.fp);
}

static Sint64 file_size(SDL_RWops *c)
{
    const long here = ftell(c->hidden.stdio.fp);
    long n;
    if (here < 0 || fseek(c->hidden.stdio.fp, 0, SEEK_END) != 0) {
        return -1;
    }
    n = ftell(c->hidden.stdio.fp);
    fseek(c->hidden.stdio.fp, here, SEEK_SET);
    return n;
}

static size_t file_read(SDL_RWops *c, void *p, size_t size, size_t n)
{
    return fread(p, size, n, c->hidden.stdio.fp);
}

static size_t file_write(SDL_RWops *c, const void *p, size_t size, size_t n)
{
    return fwrite(p, size, n, c->hidden.stdio.fp);
}

static int file_close(SDL_RWops *c)
{
    int rc = 0;
    if (c->hidden.stdio.autoclose && fclose(c->hidden.stdio.fp) != 0) {
        rc = SDL_SetError("Couldn't close the file");
    }
    SDL_FreeRW(c);
    return rc;
}

SDL_RWops *SDL_RWFromFP(FILE *fp, SDL_bool autoclose)
{
    SDL_RWops *c;
    if (!fp) {
        SDL_InvalidParamError("fp");
        return NULL;
    }
    c = SDL_AllocRW();
    if (c) {
        c->size = file_size;
        c->seek = file_seek;
        c->read = file_read;
        c->write = file_write;
        c->close = file_close;
        c->type = SDL_RWOPS_STDFILE;
        c->hidden.stdio.fp = fp;
        c->hidden.stdio.autoclose = autoclose;
    }
    return c;
}

SDL_RWops *SDL_RWFromFile(const char *file, const char *mode)
{
    FILE *fp;
    SDL_RWops *c;
    if (!file || !mode) {
        SDL_SetError("SDL_RWFromFile(): No file or no mode specified");
        return NULL;
    }
    fp = fopen(file, mode);
    if (!fp) {
        SDL_SetError("Couldn't open %s", file);
        return NULL;
    }
    c = SDL_RWFromFP(fp, SDL_TRUE);
    if (!c) {
        fclose(fp);
    }
    return c;
}

/* ------------------------------------------------------------------ */
/* memory                                                              */

static Sint64 mem_size(SDL_RWops *c)
{
    return c->hidden.mem.stop - c->hidden.mem.base;
}

static Sint64 mem_seek(SDL_RWops *c, Sint64 offset, int whence)
{
    long at;
    const long size = (long)(c->hidden.mem.stop - c->hidden.mem.base);
    switch (whence) {
    case RW_SEEK_SET:
        at = (long)offset;
        break;
    case RW_SEEK_CUR:
        at = (long)(c->hidden.mem.here - c->hidden.mem.base) + (long)offset;
        break;
    case RW_SEEK_END:
        at = size + (long)offset;
        break;
    default:
        return SDL_SetError("Unknown value for 'whence'");
    }
    if (at < 0) {
        at = 0;
    }
    if (at > size) {
        at = size;
    }
    c->hidden.mem.here = c->hidden.mem.base + at;
    return at;
}

static size_t mem_read(SDL_RWops *c, void *p, size_t size, size_t n)
{
    const size_t left = (size_t)(c->hidden.mem.stop - c->hidden.mem.here);
    size_t bytes;
    if (!size || !n) {
        return 0;
    }
    /* whole objects only, as SDL2 reads them */
    if (n > left / size) {
        n = left / size;
    }
    bytes = n * size;
    memcpy(p, c->hidden.mem.here, bytes);
    c->hidden.mem.here += bytes;
    return n;
}

static size_t mem_write(SDL_RWops *c, const void *p, size_t size, size_t n)
{
    const size_t left = (size_t)(c->hidden.mem.stop - c->hidden.mem.here);
    size_t bytes;
    if (!size || !n) {
        return 0;
    }
    if (n > left / size) {
        n = left / size;
    }
    bytes = n * size;
    memcpy(c->hidden.mem.here, p, bytes);
    c->hidden.mem.here += bytes;
    return n;
}

static size_t mem_write_ro(SDL_RWops *c, const void *p, size_t size,
                           size_t n)
{
    (void)c;
    (void)p;
    (void)size;
    (void)n;
    SDL_SetError("Can't write to read-only memory");
    return 0;
}

static int mem_close(SDL_RWops *c)
{
    SDL_FreeRW(c);
    return 0;
}

static SDL_RWops *from_mem(Uint8 *mem, int size, int ro)
{
    SDL_RWops *c;
    if (!mem || size < 0) {
        SDL_InvalidParamError(!mem ? "mem" : "size");
        return NULL;
    }
    c = SDL_AllocRW();
    if (c) {
        c->size = mem_size;
        c->seek = mem_seek;
        c->read = mem_read;
        c->write = ro ? mem_write_ro : mem_write;
        c->close = mem_close;
        c->type = ro ? SDL_RWOPS_MEMORY_RO : SDL_RWOPS_MEMORY;
        c->hidden.mem.base = c->hidden.mem.here = mem;
        c->hidden.mem.stop = mem + size;
    }
    return c;
}

SDL_RWops *SDL_RWFromMem(void *mem, int size)
{
    return from_mem((Uint8 *)mem, size, 0);
}

SDL_RWops *SDL_RWFromConstMem(const void *mem, int size)
{
    return from_mem((Uint8 *)(size_t)mem, size, 1);
}

SDL_RWops *SDL_AllocRW(void)
{
    SDL_RWops *c = (SDL_RWops *)calloc(1, sizeof(*c));
    if (!c) {
        SDL_OutOfMemory();
    }
    return c;
}

void SDL_FreeRW(SDL_RWops *area)
{
    free(area);
}

/* ------------------------------------------------------------------ */
/* the endian reads and writes; a short read gives 0 bytes' worth      */

static Uint32 get(SDL_RWops *src, int n, int big)
{
    Uint8 b[4] = { 0, 0, 0, 0 };
    Uint32 v = 0;
    int i;
    SDL_RWread(src, b, (size_t)n, 1);
    for (i = 0; i < n; i++) {
        v |= (Uint32)b[big ? i : n - 1 - i] << (8 * (n - 1 - i));
    }
    return v;
}

static size_t put(SDL_RWops *dst, Uint32 v, int n, int big)
{
    Uint8 b[4];
    int i;
    for (i = 0; i < n; i++) {
        b[big ? n - 1 - i : i] = (Uint8)(v >> (8 * i));
    }
    return SDL_RWwrite(dst, b, (size_t)n, 1);
}

Uint8 SDL_ReadU8(SDL_RWops *src)
{
    return (Uint8)get(src, 1, 0);
}

Uint16 SDL_ReadLE16(SDL_RWops *src)
{
    return (Uint16)get(src, 2, 0);
}

Uint16 SDL_ReadBE16(SDL_RWops *src)
{
    return (Uint16)get(src, 2, 1);
}

Uint32 SDL_ReadLE32(SDL_RWops *src)
{
    return get(src, 4, 0);
}

Uint32 SDL_ReadBE32(SDL_RWops *src)
{
    return get(src, 4, 1);
}

size_t SDL_WriteU8(SDL_RWops *dst, Uint8 value)
{
    return put(dst, value, 1, 0);
}

size_t SDL_WriteLE16(SDL_RWops *dst, Uint16 value)
{
    return put(dst, value, 2, 0);
}

size_t SDL_WriteBE16(SDL_RWops *dst, Uint16 value)
{
    return put(dst, value, 2, 1);
}

size_t SDL_WriteLE32(SDL_RWops *dst, Uint32 value)
{
    return put(dst, value, 4, 0);
}

size_t SDL_WriteBE32(SDL_RWops *dst, Uint32 value)
{
    return put(dst, value, 4, 1);
}
