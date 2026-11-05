/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The SDL2 add-on's streams, SDL2's layout: a file through the C
 * library's stdio, or a block of memory. Paths are TOS's, upper or
 * lower case as the file system takes them.
 */

#ifndef STDL_SDL2_RWOPS_H
#define STDL_SDL2_RWOPS_H

#include "SDL_stdinc.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SDL_RWOPS_UNKNOWN   0u
#define SDL_RWOPS_STDFILE   2u
#define SDL_RWOPS_MEMORY    4u
#define SDL_RWOPS_MEMORY_RO 5u

#define RW_SEEK_SET 0
#define RW_SEEK_CUR 1
#define RW_SEEK_END 2

typedef struct SDL_RWops {
    Sint64 (*size)(struct SDL_RWops *context);
    Sint64 (*seek)(struct SDL_RWops *context, Sint64 offset, int whence);
    size_t (*read)(struct SDL_RWops *context, void *ptr, size_t size,
                   size_t maxnum);
    size_t (*write)(struct SDL_RWops *context, const void *ptr, size_t size,
                    size_t num);
    int (*close)(struct SDL_RWops *context);
    Uint32 type;
    union {
        struct {
            FILE *fp;
            SDL_bool autoclose;
        } stdio;
        struct {
            Uint8 *base;
            Uint8 *here;
            Uint8 *stop;
        } mem;
        struct {
            void *data1;
            void *data2;
        } unknown;
    } hidden;
} SDL_RWops;

#define SDL_RWFromFile     stdl_sdl2_RWFromFile
#define SDL_RWFromFP       stdl_sdl2_RWFromFP
#define SDL_RWFromMem      stdl_sdl2_RWFromMem
#define SDL_RWFromConstMem stdl_sdl2_RWFromConstMem
#define SDL_AllocRW        stdl_sdl2_AllocRW
#define SDL_FreeRW         stdl_sdl2_FreeRW
#define SDL_ReadU8         stdl_sdl2_ReadU8
#define SDL_ReadLE16       stdl_sdl2_ReadLE16
#define SDL_ReadBE16       stdl_sdl2_ReadBE16
#define SDL_ReadLE32       stdl_sdl2_ReadLE32
#define SDL_ReadBE32       stdl_sdl2_ReadBE32
#define SDL_WriteU8        stdl_sdl2_WriteU8
#define SDL_WriteLE16      stdl_sdl2_WriteLE16
#define SDL_WriteBE16      stdl_sdl2_WriteBE16
#define SDL_WriteLE32      stdl_sdl2_WriteLE32
#define SDL_WriteBE32      stdl_sdl2_WriteBE32

extern SDL_RWops *SDL_RWFromFile(const char *file, const char *mode);
extern SDL_RWops *SDL_RWFromFP(FILE *fp, SDL_bool autoclose);
extern SDL_RWops *SDL_RWFromMem(void *mem, int size);
extern SDL_RWops *SDL_RWFromConstMem(const void *mem, int size);
extern SDL_RWops *SDL_AllocRW(void);
extern void SDL_FreeRW(SDL_RWops *area);

#define SDL_RWsize(ctx)         (ctx)->size(ctx)
#define SDL_RWseek(ctx, off, w) (ctx)->seek(ctx, off, w)
#define SDL_RWtell(ctx)         (ctx)->seek(ctx, 0, RW_SEEK_CUR)
#define SDL_RWread(ctx, p, s, n)  (ctx)->read(ctx, p, s, n)
#define SDL_RWwrite(ctx, p, s, n) (ctx)->write(ctx, p, s, n)
#define SDL_RWclose(ctx)        (ctx)->close(ctx)

extern Uint8 SDL_ReadU8(SDL_RWops *src);
extern Uint16 SDL_ReadLE16(SDL_RWops *src);
extern Uint16 SDL_ReadBE16(SDL_RWops *src);
extern Uint32 SDL_ReadLE32(SDL_RWops *src);
extern Uint32 SDL_ReadBE32(SDL_RWops *src);
extern size_t SDL_WriteU8(SDL_RWops *dst, Uint8 value);
extern size_t SDL_WriteLE16(SDL_RWops *dst, Uint16 value);
extern size_t SDL_WriteBE16(SDL_RWops *dst, Uint16 value);
extern size_t SDL_WriteLE32(SDL_RWops *dst, Uint32 value);
extern size_t SDL_WriteBE32(SDL_RWops *dst, Uint32 value);

#ifdef __cplusplus
}
#endif

#endif /* STDL_SDL2_RWOPS_H */
