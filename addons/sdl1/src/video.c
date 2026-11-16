/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The SDL1 add-on's SDL 1.2 half: 8-bit surfaces as SDL 1.2 has them,
 * and the screen surface, whose pixels the SDL2 add-on shows through a
 * texture laid over them (bridge.h). Blits clip as SDL 1.2's do and,
 * as its 8-bit blits do, put each source colour on the nearest of the
 * destination's palette where the two palettes differ: an unconverted
 * picture then comes out in the screen's colours, which SDL's own
 * tests count on.
 */

#include "SDL.h"
#include <stdl/stdl_math.h>
#include "bridge.h"

/* A surface with its format, palette and colours, and its colours'
 * places in the palette it was last blitted to: made again when
 * either palette's version has moved (a surface's format_version, one
 * number run for them all, so a surface freed and another made at its
 * address never share one). */
typedef struct {
    SDL_Surface s;
    SDL_PixelFormat fmt;
    SDL_Palette pal;
    SDL_Color colours[256];
    unsigned int from, to;      /* the versions the map is for */
    int same;                   /* no colour moves: the pixels as they are */
    Uint8 map[256];
} surf_t;

static SDL_Surface *screen;
static unsigned int version;
static int ox, oy;              /* the picture's place, for the mouse */

/* ---------------------------------------------------------------- */
/* surfaces                                                         */

static int eight(int depth)
{
    if (depth != 0 && depth != 8) {
        STDL_SetError("The SDL1 add-on's surfaces are 8 bits a pixel");
        return 0;
    }
    return 1;
}

/* over pixels, or (NULL) with a block of its own, zeroed, its rows a
 * multiple of 4 bytes as SDL 1.2's are. The pixels are freed with it
 * unless it is marked SDL_PREALLOC, as in SDL 1.2. */
static SDL_Surface *make(void *pixels, int w, int h, int pitch)
{
    surf_t *t;
    if (!pixels) {
        pitch = (w + 3) & ~3;
    }
    if (w < 1 || h < 1 || w > 32767 || h > 32767 || pitch < w ||
        pitch > 65535) {
        STDL_SetError("A surface of that size cannot be made");
        return NULL;
    }
    t = (surf_t *)calloc(1, sizeof(*t));
    if (t && !pixels) {
        pixels = calloc((size_t)h, (size_t)pitch);
    }
    if (!t || !pixels) {
        free(t);
        STDL_SetError("Out of memory");
        return NULL;
    }
    t->pal.ncolors = 256;
    t->pal.colors = t->colours;
    t->fmt.palette = &t->pal;
    t->fmt.BitsPerPixel = 8;
    t->fmt.BytesPerPixel = 1;
    t->s.format = &t->fmt;
    t->s.w = w;
    t->s.h = h;
    t->s.pitch = (Uint16)pitch;
    t->s.STDL_SDL1_PIXELS = pixels;
    t->s.clip_rect.w = (Uint16)w;
    t->s.clip_rect.h = (Uint16)h;
    t->s.format_version = ++version;
    t->s.refcount = 1;
    return &t->s;
}

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height,
                                  int depth, Uint32 Rmask, Uint32 Gmask,
                                  Uint32 Bmask, Uint32 Amask)
{
    (void)flags; (void)Rmask; (void)Gmask; (void)Bmask; (void)Amask;
    return eight(depth) ? make(NULL, width, height, 0) : NULL;
}

SDL_Surface *SDL_CreateRGBSurfaceFrom(void *pixels, int width, int height,
                                      int depth, int pitch, Uint32 Rmask,
                                      Uint32 Gmask, Uint32 Bmask,
                                      Uint32 Amask)
{
    SDL_Surface *s;
    (void)Rmask; (void)Gmask; (void)Bmask; (void)Amask;
    if (!pixels) {
        STDL_SetError("SDL_CreateRGBSurfaceFrom: no pixels");
        return NULL;
    }
    s = eight(depth) ? make(pixels, width, height, pitch) : NULL;
    if (s) {
        s->flags |= SDL_PREALLOC;
    }
    return s;
}

static void destroy(SDL_Surface *surface)
{
    if (surface && !(surface->flags & SDL_PREALLOC)) {
        free(surface->STDL_SDL1_PIXELS);
    }
    free(surface);
}

void SDL_FreeSurface(SDL_Surface *surface)
{
    /* the screen is the video mode's, as in SDL 1.2 */
    if (surface && surface != screen && --surface->refcount <= 0) {
        destroy(surface);
    }
}

SDL_Surface *SDL_LoadBMP(const char *file)
{
    unsigned char colours[256 * 4];
    int w, h, pitch, n;
    void *pixels = stdl_sdl1_br_bmp(file, &w, &h, &pitch, colours, &n);
    SDL_Surface *s = pixels ? make(pixels, w, h, pitch) : NULL;
    if (!s) {
        free(pixels);
        return NULL;
    }
    /* SDL_Color's four bytes are the bridge's */
    memcpy(s->format->palette->colors, colours, (size_t)n * 4);
    s->format->palette->ncolors = n;
    return s;
}

/* ---------------------------------------------------------------- */
/* colours                                                          */

static Uint8 nearest(const SDL_Color *pal, int n, const SDL_Color *c)
{
    Uint32 best = 0xFFFFFFFFu;
    int i, k = 0;
    for (i = 0; i < n; i++) {
        const int dr = pal[i].r - c->r, dg = pal[i].g - c->g,
                  db = pal[i].b - c->b;
        const Uint32 d = (Uint32)(STDL_MulS16(dr, dr) + STDL_MulS16(dg, dg) +
                                  STDL_MulS16(db, db));
        if (d < best) {
            k = i;
            if (!d) {
                break;
            }
            best = d;
        }
    }
    return (Uint8)k;
}

/* src's colours as dst's pixel values, or NULL where they are the
 * same: SDL 1.2's map between two palettes, an entry that is the same
 * colour in both staying where it is */
static const Uint8 *map_for(SDL_Surface *src, const SDL_Surface *dst)
{
    surf_t *t = (surf_t *)src;
    /* one version: one palette's colours (SDL_DisplayFormat's copies) */
    if (src == dst || src->format_version == dst->format_version) {
        return NULL;
    }
    if (t->from != src->format_version || t->to != dst->format_version) {
        const SDL_Palette *sp = src->format->palette;
        const SDL_Palette *dp = dst->format->palette;
        int i;
        t->same = 1;
        for (i = 0; i < 256; i++) {
            t->map[i] = (Uint8)i;
            if (i < sp->ncolors &&
                (i >= dp->ncolors ||
                 memcmp(&sp->colors[i], &dp->colors[i], 3) != 0)) {
                t->map[i] = nearest(dp->colors, dp->ncolors, &sp->colors[i]);
                t->same = 0;
            }
        }
        t->from = src->format_version;
        t->to = dst->format_version;
    }
    return t->same ? NULL : t->map;
}

int SDL_SetPalette(SDL_Surface *surface, int flags, SDL_Color *colors,
                   int firstcolor, int ncolors)
{
    int all = 1, i;
    if (!surface || !colors || firstcolor < 0 || ncolors < 0) {
        return 0;
    }
    if (firstcolor + ncolors > 256) {
        ncolors = 256 - firstcolor;
        all = 0;
    }
    if (ncolors <= 0) {
        return 0;
    }
    if (flags & SDL_LOGPAL) {
        SDL_Color *c = surface->format->palette->colors + firstcolor;
        for (i = 0; i < ncolors; i++) {
            c[i].r = colors[i].r;
            c[i].g = colors[i].g;
            c[i].b = colors[i].b;
        }
        surface->format_version = ++version;
    }
    if ((flags & SDL_PHYSPAL) && surface == screen) {
        stdl_sdl1_br_colours(colors, firstcolor, ncolors);
    }
    return all;
}

int SDL_SetColors(SDL_Surface *surface, SDL_Color *colors, int firstcolor,
                  int ncolors)
{
    return SDL_SetPalette(surface, SDL_LOGPAL | SDL_PHYSPAL, colors,
                          firstcolor, ncolors);
}

int SDL_SetColorKey(SDL_Surface *surface, Uint32 flag, Uint32 key)
{
    if (!surface) {
        return -1;
    }
    if (flag & SDL_SRCCOLORKEY) {
        surface->flags |= SDL_SRCCOLORKEY;
        surface->format->colorkey = key & 0xFF;
    } else {
        surface->flags &= ~SDL_SRCCOLORKEY;
        surface->format->colorkey = 0;
    }
    return 0;
}

SDL_Surface *SDL_DisplayFormat(SDL_Surface *surface)
{
    SDL_Surface *d;
    Uint32 keyed;
    if (!surface) {
        return NULL;
    }
    if (!screen) {
        STDL_SetError("No video mode has been set");
        return NULL;
    }
    d = make(NULL, surface->w, surface->h, 0);
    if (!d) {
        return NULL;
    }
    /* the screen's colours, and the pixels and the key put on them */
    memcpy(d->format->palette->colors, screen->format->palette->colors,
           256 * sizeof(SDL_Color));
    /* and their version: the same colours, so a map made for the
     * screen serves this one until either's palette changes */
    d->format_version = screen->format_version;
    /* every pixel, the key's too: a blit without the key */
    keyed = surface->flags & SDL_SRCCOLORKEY;
    surface->flags &= ~SDL_SRCCOLORKEY;
    SDL_UpperBlit(surface, NULL, d, NULL);
    surface->flags |= keyed;
    if (keyed) {
        const Uint8 *map = map_for(surface, d);
        const Uint8 key = (Uint8)surface->format->colorkey;
        d->flags |= SDL_SRCCOLORKEY;
        d->format->colorkey = map ? map[key] : key;
    }
    return d;
}

/* ---------------------------------------------------------------- */
/* drawing                                                          */

/* a cut to b, as SDL_IntersectRect; 0 if nothing is left */
static int cut(const SDL_Rect *a, const SDL_Rect *b, SDL_Rect *out)
{
    int x0 = a->x > b->x ? a->x : b->x;
    int y0 = a->y > b->y ? a->y : b->y;
    int x1 = a->x + a->w < b->x + b->w ? a->x + a->w : b->x + b->w;
    int y1 = a->y + a->h < b->y + b->h ? a->y + a->h : b->y + b->h;
    const int some = x1 > x0 && y1 > y0;
    out->x = (Sint16)x0;
    out->y = (Sint16)y0;
    out->w = (Uint16)(some ? x1 - x0 : 0);
    out->h = (Uint16)(some ? y1 - y0 : 0);
    return some;
}

SDL_bool SDL_SetClipRect(SDL_Surface *surface, const SDL_Rect *rect)
{
    SDL_Rect full;
    if (!surface) {
        return SDL_FALSE;
    }
    full.x = full.y = 0;
    full.w = (Uint16)surface->w;
    full.h = (Uint16)surface->h;
    if (!rect) {
        surface->clip_rect = full;
        return SDL_TRUE;
    }
    return cut(rect, &full, &surface->clip_rect) ? SDL_TRUE : SDL_FALSE;
}

void SDL_GetClipRect(SDL_Surface *surface, SDL_Rect *rect)
{
    if (surface && rect) {
        *rect = surface->clip_rect;
    }
}

int SDL_FillRect(SDL_Surface *dst, SDL_Rect *dstrect, Uint32 color)
{
    Uint8 *o;
    int y;
    if (!dst) {
        return -1;
    }
    /* cut to the clip rectangle, and handed back so, as SDL 1.2 does */
    if (!dstrect) {
        dstrect = &dst->clip_rect;
    } else if (!cut(dstrect, &dst->clip_rect, dstrect)) {
        return 0;
    }
    o = (Uint8 *)dst->STDL_SDL1_PIXELS +
        STDL_MulU16(dstrect->y, dst->pitch) + dstrect->x;
    if (dstrect->w == dst->pitch) {
        /* whole rows are one block: the library's fill has a start-up
         * a row would pay 200 times a screen */
        memset(o, (int)(color & 0xFF), STDL_MulU16(dstrect->h, dst->pitch));
        return 0;
    }
    for (y = 0; y < dstrect->h; y++, o += dst->pitch) {
        memset(o, (int)(color & 0xFF), dstrect->w);
    }
    return 0;
}

int SDL_UpperBlit(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst,
                  SDL_Rect *dstrect)
{
    SDL_Rect whole;
    const SDL_Rect *clip;
    const Uint8 *map, *s;
    Uint8 *o;
    int sx, sy, w, h, d, x, y, sp, dp;
    if (!src || !dst) {
        STDL_SetError("SDL_UpperBlit: passed a NULL surface");
        return -1;
    }
    if (!dstrect) {
        whole.x = whole.y = 0;
        dstrect = &whole;
    }
    /* SDL 1.2's clip: the source to its surface, the destination moved
     * with it, then to the destination's clip rectangle */
    if (srcrect) {
        sx = srcrect->x;
        sy = srcrect->y;
        w = srcrect->w;
        h = srcrect->h;
        if (sx < 0) {
            w += sx;
            dstrect->x = (Sint16)(dstrect->x - sx);
            sx = 0;
        }
        if (sy < 0) {
            h += sy;
            dstrect->y = (Sint16)(dstrect->y - sy);
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
    clip = &dst->clip_rect;
    d = clip->x - dstrect->x;
    if (d > 0) {
        w -= d;
        dstrect->x = (Sint16)(dstrect->x + d);
        sx += d;
    }
    d = dstrect->x + w - clip->x - clip->w;
    if (d > 0) {
        w -= d;
    }
    d = clip->y - dstrect->y;
    if (d > 0) {
        h -= d;
        dstrect->y = (Sint16)(dstrect->y + d);
        sy += d;
    }
    d = dstrect->y + h - clip->y - clip->h;
    if (d > 0) {
        h -= d;
    }
    if (w <= 0 || h <= 0) {
        dstrect->w = dstrect->h = 0;
        return 0;
    }
    dstrect->w = (Uint16)w;
    dstrect->h = (Uint16)h;

    map = map_for(src, dst);
    sp = src->pitch;
    dp = dst->pitch;
    s = (const Uint8 *)src->STDL_SDL1_PIXELS + STDL_MulU16(sy, sp) + sx;
    o = (Uint8 *)dst->STDL_SDL1_PIXELS + STDL_MulU16(dstrect->y, dp) +
        dstrect->x;
    if (!map && !(src->flags & SDL_SRCCOLORKEY)) {
        if (w == sp && w == dp) {
            /* whole rows are one block, whichever way it moves */
            memmove(o, s, STDL_MulU16(h, w));
            return 0;
        }
        /* rows as they are; from the bottom where a surface is moved
         * down onto itself, and memmove for along a row */
        if (src == dst && o > s) {
            s += STDL_MulU16(h - 1, sp);
            o += STDL_MulU16(h - 1, dp);
            sp = -sp;
            dp = -dp;
        }
        for (y = 0; y < h; y++, s += sp, o += dp) {
            memmove(o, s, (size_t)w);
        }
    } else if (!(src->flags & SDL_SRCCOLORKEY)) {
        for (y = 0; y < h; y++, s += sp, o += dp) {
            for (x = 0; x < w; x++) {
                o[x] = map[s[x]];
            }
        }
    } else {
        /* a loop for each: gcc 4.6 leaves a test of the map in the
         * pixel loop */
        const Uint8 key = (Uint8)src->format->colorkey;
        for (y = 0; y < h; y++, s += sp, o += dp) {
            if (map) {
                for (x = 0; x < w; x++) {
                    const Uint8 v = s[x];
                    if (v != key) {
                        o[x] = map[v];
                    }
                }
            } else {
                for (x = 0; x < w; x++) {
                    const Uint8 v = s[x];
                    if (v != key) {
                        o[x] = v;
                    }
                }
            }
        }
    }
    return 0;
}

/* STDL's own single-pixel calls: its clip rectangle for a write, as
 * the planar one's, and the surface for a read */
void STDL_PutPixel(SDL_Surface *dst, int x, int y, Uint8 col)
{
    if (dst && x >= dst->clip_rect.x && y >= dst->clip_rect.y &&
        x < dst->clip_rect.x + dst->clip_rect.w &&
        y < dst->clip_rect.y + dst->clip_rect.h) {
        ((Uint8 *)dst->STDL_SDL1_PIXELS)[STDL_MulU16(y, dst->pitch) + x] = col;
    }
}

Uint8 STDL_GetPixel(const SDL_Surface *src, int x, int y)
{
    return (src && x >= 0 && y >= 0 && x < src->w && y < src->h)
           ? ((const Uint8 *)src->STDL_SDL1_PIXELS)[STDL_MulU16(y, src->pitch) + x]
           : 0;
}

int STDL_SDL1_CountColours(const SDL_Surface *surface, Uint8 *used)
{
    Uint8 seen[256];
    const Uint8 *s;
    int x, y, n = 0;
    if (!surface) {
        return 0;
    }
    memset(seen, 0, sizeof(seen));
    s = (const Uint8 *)surface->STDL_SDL1_PIXELS;
    for (y = 0; y < surface->h; y++, s += surface->pitch) {
        for (x = 0; x < surface->w; x++) {
            seen[s[x]] = 1;
        }
    }
    for (x = 0; x < 256; x++) {
        n += seen[x];
    }
    if (used) {
        memcpy(used, seen, sizeof(seen));
    }
    return n;
}

/* ---------------------------------------------------------------- */
/* the screen                                                       */

SDL_Surface *SDL_SetVideoMode(int width, int height, int bpp, Uint32 flags)
{
    SDL_Surface *s, *const old = screen;
    SDL_Color *c;
    int i;
    if (bpp != 0 && bpp != 8 && !(flags & SDL_ANYFORMAT)) {
        STDL_SetError("The SDL1 add-on's screen is 8 bits a pixel");
        return NULL;
    }
    if (!SDL_VideoModeOK(width, height, 8, flags)) {
        STDL_SetError("The SDL1 add-on's screen is at most 320x240");
        return NULL;
    }
    /* an even pitch at an even address: the SDL2 add-on compares such
     * rows a group at a time */
    s = make(NULL, width, height, 0);
    if (!s) {
        return NULL;
    }
    if (stdl_sdl1_br_open(s->STDL_SDL1_PIXELS, width, height, s->pitch,
                          &ox, &oy) < 0) {
        destroy(s);
        return NULL;
    }
    /* the texture over the old screen's pixels has gone */
    destroy(old);
    screen = s;
    s->flags = SDL_SWSURFACE | SDL_HWPALETTE | (flags & SDL_FULLSCREEN);
    /* SDL 1.2's palette for a new 8-bit screen: 3 bits of red, 3 of
     * green, 2 of blue, each spread over 0-255 */
    c = s->format->palette->colors;
    for (i = 0; i < 256; i++) {
        int v = i & 0xE0;
        c[i].r = (Uint8)(v | v >> 3 | v >> 6);
        v = (i << 3) & 0xE0;
        c[i].g = (Uint8)(v | v >> 3 | v >> 6);
        v = i & 3;
        v |= v << 2;
        c[i].b = (Uint8)(v | v << 4);
    }
    stdl_sdl1_br_colours(c, 0, 256);
    return s;
}

SDL_Surface *SDL_GetVideoSurface(void)
{
    return screen;
}

/* a rectangle of the screen to the display, widened to its 16-pixel
 * groups, which the SDL2 add-on compares or converts whole */
static void push(int x, int y, int w, int h)
{
    int x1 = (x + w + 15) & ~15, y1 = y + h;
    x &= ~15;
    if (x < 0) {
        x = 0;
    }
    if (y < 0) {
        y = 0;
    }
    if (x1 > screen->w) {
        x1 = screen->w;
    }
    if (y1 > screen->h) {
        y1 = screen->h;
    }
    if (x1 > x && y1 > y) {
        stdl_sdl1_br_push(x, y, x1 - x, y1 - y);
    }
}

void SDL_UpdateRects(SDL_Surface *surface, int numrects, SDL_Rect *rects)
{
    int i;
    if (!surface || surface != screen) {
        return;
    }
    for (i = 0; i < numrects; i++) {
        push(rects[i].x, rects[i].y, rects[i].w, rects[i].h);
    }
    stdl_sdl1_br_present();
}

void SDL_UpdateRect(SDL_Surface *surface, Sint32 x, Sint32 y, Uint32 w,
                    Uint32 h)
{
    if (!surface || surface != screen) {
        return;
    }
    if ((w | h) == 0) {
        push(0, 0, screen->w, screen->h);
    } else {
        push((int)x, (int)y, (int)w, (int)h);
    }
    stdl_sdl1_br_present();
}

int SDL_Flip(SDL_Surface *surface)
{
    if (!surface || surface != screen) {
        return -1;
    }
    push(0, 0, screen->w, screen->h);
    stdl_sdl1_br_present();
    return 0;
}

const SDL_VideoInfo *SDL_GetVideoInfo(void)
{
    static SDL_VideoInfo info;
    memset(&info, 0, sizeof(info));
    info.vfmt = screen ? screen->format : NULL;
    info.current_w = screen ? screen->w : 320;
    info.current_h = screen ? screen->h : 240;
    return &info;
}

SDL_Rect **SDL_ListModes(SDL_PixelFormat *format, Uint32 flags)
{
    static SDL_Rect tall = { 0, 0, 320, 240 }, low = { 0, 0, 320, 200 };
    static SDL_Rect *modes[3] = { &tall, &low, NULL };
    (void)format; (void)flags;
    return modes;
}

int SDL_VideoModeOK(int width, int height, int bpp, Uint32 flags)
{
    (void)bpp; (void)flags;
    return (width >= 1 && height >= 1 && width <= 320 && height <= 240)
           ? 8 : 0;
}

char *SDL_VideoDriverName(char *namebuf, int maxlen)
{
    if (!namebuf || maxlen < 1) {
        return NULL;
    }
    strncpy(namebuf, "stdl-sdl1", (size_t)maxlen - 1);
    namebuf[maxlen - 1] = '\0';
    return namebuf;
}

int STDL_SDL1_SetHint(const char *name, const char *value)
{
    return stdl_sdl1_br_hint(name, value);
}

int STDL_SDL1_SetBorderIndex(unsigned x)
{
    return stdl_sdl1_br_border(x);
}

int STDL_SDL1_GetCheck(Uint32 *flips, Uint32 *bad)
{
    unsigned long f, b;
    if (stdl_sdl1_br_check(&f, &b) < 0) {
        return -1;
    }
    if (flips) {
        *flips = (Uint32)f;
    }
    if (bad) {
        *bad = (Uint32)b;
    }
    return 0;
}

/* ---------------------------------------------------------------- */
/* the mouse, from the picture's top left                           */

static void place(SDL_Event *e)
{
    if (e->type == SDL_MOUSEMOTION) {
        e->motion.x = (Uint16)(e->motion.x - ox);
        e->motion.y = (Uint16)(e->motion.y - oy);
    } else if (e->type == SDL_MOUSEBUTTONDOWN ||
               e->type == SDL_MOUSEBUTTONUP) {
        e->button.x = (Uint16)(e->button.x - ox);
        e->button.y = (Uint16)(e->button.y - oy);
    }
}

int SDL_PollEvent(SDL_Event *event)
{
    const int got = STDL_PollEvent(event);
    if (got && event) {
        place(event);
    }
    return got;
}

int SDL_WaitEvent(SDL_Event *event)
{
    const int got = STDL_WaitEvent(event);
    if (got && event) {
        place(event);
    }
    return got;
}

Uint8 SDL_GetMouseState(int *x, int *y)
{
    int sx, sy;
    const Uint8 buttons = STDL_GetMouseState(&sx, &sy);
    if (x) {
        *x = sx - ox;
    }
    if (y) {
        *y = sy - oy;
    }
    return buttons;
}

void SDL_WarpMouse(Uint16 x, Uint16 y)
{
    STDL_WarpMouse((uint16_t)(x + ox), (uint16_t)(y + oy));
}
