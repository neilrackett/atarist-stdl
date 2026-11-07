/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * 8-bit surfaces, SDL2's layout: a byte a pixel, a palette of 256, a
 * clip rectangle and a colour key. A blit copies bytes, skipping the
 * key's; there is no conversion, as there is one format. The surface,
 * its format and its key are one allocation.
 */

#include "internal.h"

struct surface {
    SDL_Surface s;              /* first: the program's pointer */
    SDL_PixelFormat f;
    int keyed;
    Uint8 key;
};

#define KEY(S) ((struct surface *)(void *)(S))

static SDL_Surface *make(void *pixels, int w, int h, int pitch)
{
    struct surface *m;
    SDL_Palette *pal;
    if (w < 0 || h < 0) {
        SDL_SetError("A surface of %dx%d", w, h);
        return NULL;
    }
    m = (struct surface *)calloc(1, sizeof(*m));
    pal = SDL_AllocPalette(256);
    if (!m || !pal) {
        free(m);
        SDL_FreePalette(pal);
        SDL_OutOfMemory();
        return NULL;
    }
    m->f.format = SDL_PIXELFORMAT_INDEX8;
    m->f.palette = pal;
    m->f.BitsPerPixel = 8;
    m->f.BytesPerPixel = 1;
    m->f.Rloss = m->f.Gloss = m->f.Bloss = m->f.Aloss = 8;
    m->f.refcount = 1;
    m->s.format = &m->f;
    m->s.w = w;
    m->s.h = h;
    m->s.clip_rect.w = w;
    m->s.clip_rect.h = h;
    m->s.map = &m->key;
    m->s.refcount = 1;
    if (pixels) {
        m->s.flags = SDL_PREALLOC;
        m->s.pixels = pixels;
        m->s.pitch = pitch;
    } else {
        /* SDL2 rounds a row to 4 bytes */
        m->s.pitch = (w + 3) & ~3;
        if (w && h) {
            m->s.pixels = calloc((size_t)m->s.pitch * h, 1);
            if (!m->s.pixels) {
                SDL_FreePalette(pal);
                free(m);
                SDL_OutOfMemory();
                return NULL;
            }
        }
    }
    return &m->s;
}

static int eight(int depth, Uint32 rm, Uint32 gm, Uint32 bm, Uint32 am)
{
    if (depth != 8 || rm || gm || bm || am) {
        SDL_SetError("Surfaces are 8-bit and palettised here, not %d-bit",
                     depth);
        return 0;
    }
    return 1;
}

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height,
                                  int depth, Uint32 Rmask, Uint32 Gmask,
                                  Uint32 Bmask, Uint32 Amask)
{
    (void)flags;
    if (!eight(depth, Rmask, Gmask, Bmask, Amask)) {
        return NULL;
    }
    return make(NULL, width, height, 0);
}

SDL_Surface *SDL_CreateRGBSurfaceWithFormat(Uint32 flags, int width,
                                            int height, int depth,
                                            Uint32 format)
{
    (void)flags;
    (void)depth;
    if (format != SDL_PIXELFORMAT_INDEX8) {
        SDL_SetError("Surfaces are SDL_PIXELFORMAT_INDEX8 here");
        return NULL;
    }
    return make(NULL, width, height, 0);
}

SDL_Surface *SDL_CreateRGBSurfaceFrom(void *pixels, int width, int height,
                                      int depth, int pitch, Uint32 Rmask,
                                      Uint32 Gmask, Uint32 Bmask,
                                      Uint32 Amask)
{
    if (!eight(depth, Rmask, Gmask, Bmask, Amask)) {
        return NULL;
    }
    if (pitch < width) {
        SDL_SetError("A pitch of %d for %d pixels", pitch, width);
        return NULL;
    }
    return make(pixels, width, height, pitch);
}

void SDL_FreeSurface(SDL_Surface *surface)
{
    if (!surface || (surface->flags & SDL_DONTFREE) ||
        --surface->refcount > 0) {
        return;
    }
    SDL_FreePalette(surface->format->palette);
    if (!(surface->flags & SDL_PREALLOC)) {
        free(surface->pixels);
    }
    free(KEY(surface));
}

int SDL_SetSurfacePalette(SDL_Surface *surface, SDL_Palette *palette)
{
    if (!surface) {
        return SDL_InvalidParamError("surface");
    }
    if (palette) {
        palette->refcount++;
    }
    SDL_FreePalette(surface->format->palette);
    surface->format->palette = palette;
    return 0;
}

int SDL_LockSurface(SDL_Surface *surface)
{
    if (!surface) {
        return SDL_InvalidParamError("surface");
    }
    surface->locked++;
    return 0;
}

void SDL_UnlockSurface(SDL_Surface *surface)
{
    if (surface && surface->locked > 0) {
        surface->locked--;
    }
}

int SDL_SetColorKey(SDL_Surface *surface, int flag, Uint32 key)
{
    if (!surface) {
        return SDL_InvalidParamError("surface");
    }
    KEY(surface)->keyed = flag != 0;
    KEY(surface)->key = (Uint8)key;
    return 0;
}

int SDL_GetColorKey(SDL_Surface *surface, Uint32 *key)
{
    if (!surface) {
        return SDL_InvalidParamError("surface");
    }
    if (!KEY(surface)->keyed) {
        return SDL_SetError("Surface doesn't have a colorkey");
    }
    if (key) {
        *key = KEY(surface)->key;
    }
    return 0;
}

/* a rectangle cut to another; 0 when nothing is left */
static int cut(SDL_Rect *r, const SDL_Rect *by)
{
    int x0 = r->x > by->x ? r->x : by->x;
    int y0 = r->y > by->y ? r->y : by->y;
    int x1 = r->x + r->w < by->x + by->w ? r->x + r->w : by->x + by->w;
    int y1 = r->y + r->h < by->y + by->h ? r->y + r->h : by->y + by->h;
    r->x = x0;
    r->y = y0;
    r->w = x1 > x0 ? x1 - x0 : 0;
    r->h = y1 > y0 ? y1 - y0 : 0;
    return r->w && r->h;
}

SDL_bool SDL_SetClipRect(SDL_Surface *surface, const SDL_Rect *rect)
{
    SDL_Rect all;
    if (!surface) {
        return SDL_FALSE;
    }
    all.x = all.y = 0;
    all.w = surface->w;
    all.h = surface->h;
    if (!rect) {
        surface->clip_rect = all;
        return SDL_TRUE;
    }
    surface->clip_rect = *rect;
    return cut(&surface->clip_rect, &all) ? SDL_TRUE : SDL_FALSE;
}

void SDL_GetClipRect(SDL_Surface *surface, SDL_Rect *rect)
{
    if (surface && rect) {
        *rect = surface->clip_rect;
    }
}

int SDL_FillRect(SDL_Surface *dst, const SDL_Rect *rect, Uint32 color)
{
    SDL_Rect r;
    Uint8 *p;
    int y;
    if (!dst) {
        return SDL_InvalidParamError("dst");
    }
    r = rect ? *rect : dst->clip_rect;
    if (!cut(&r, &dst->clip_rect)) {
        return 0;
    }
    p = (Uint8 *)dst->pixels + r.y * dst->pitch + r.x;
    for (y = 0; y < r.h; y++, p += dst->pitch) {
        memset(p, (int)(Uint8)color, (size_t)r.w);
    }
    return 0;
}

int SDL_UpperBlit(SDL_Surface *src, const SDL_Rect *srcrect,
                  SDL_Surface *dst, SDL_Rect *dstrect)
{
    SDL_Rect whole, d;
    int sx, sy, w, h, dx, y;
    const Uint8 *s;
    Uint8 *o;
    if (!src || !dst) {
        return SDL_SetError("SDL_UpperBlit: passed a NULL surface");
    }
    if (!dstrect) {
        whole.x = whole.y = 0;
        dstrect = &whole;
    }
    /* the source cut to its surface, the destination moved with it */
    if (srcrect) {
        sx = srcrect->x;
        sy = srcrect->y;
        w = srcrect->w;
        h = srcrect->h;
        if (sx < 0) {
            w += sx;
            dstrect->x -= sx;
            sx = 0;
        }
        if (sy < 0) {
            h += sy;
            dstrect->y -= sy;
            sy = 0;
        }
        if (w > src->w - sx) {
            w = src->w - sx;
        }
        if (h > src->h - sy) {
            h = src->h - sy;
        }
    } else {
        sx = sy = 0;
        w = src->w;
        h = src->h;
    }
    /* then to the destination's clip rectangle */
    d.x = dstrect->x;
    d.y = dstrect->y;
    d.w = w;
    d.h = h;
    if (w <= 0 || h <= 0 || !cut(&d, &dst->clip_rect)) {
        dstrect->w = dstrect->h = 0;
        return 0;
    }
    sx += d.x - dstrect->x;
    sy += d.y - dstrect->y;
    *dstrect = d;
    s = (const Uint8 *)src->pixels + sy * src->pitch + sx;
    o = (Uint8 *)dst->pixels + d.y * dst->pitch + d.x;
    if (!KEY(src)->keyed) {
        /* memmove: a surface may be blitted onto itself */
        if (src == dst && o > s) {
            s += (d.h - 1) * src->pitch;
            o += (d.h - 1) * dst->pitch;
            for (y = 0; y < d.h; y++, s -= src->pitch, o -= dst->pitch) {
                memmove(o, s, (size_t)d.w);
            }
        } else {
            for (y = 0; y < d.h; y++, s += src->pitch, o += dst->pitch) {
                memmove(o, s, (size_t)d.w);
            }
        }
    } else {
        const Uint8 key = KEY(src)->key;
        for (y = 0; y < d.h; y++, s += src->pitch, o += dst->pitch) {
            for (dx = 0; dx < d.w; dx++) {
                if (s[dx] != key) {
                    o[dx] = s[dx];
                }
            }
        }
    }
    return 0;
}

SDL_Texture *SDL_CreateTextureFromSurface(SDL_Renderer *renderer,
                                          SDL_Surface *surface)
{
    SDL_Texture *t;
    if (!surface) {
        SDL_InvalidParamError("surface");
        return NULL;
    }
    t = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_INDEX8,
                          SDL_TEXTUREACCESS_STATIC, surface->w, surface->h);
    if (t && SDL_UpdateTexture(t, NULL, surface->pixels, surface->pitch) < 0) {
        SDL_DestroyTexture(t);
        t = NULL;
    }
    return t;
}
