/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * The window, the renderer and its textures, and the compositor: a
 * screen that keeps what was drawn on it (SDL_render.h says why that
 * is within SDL2's rules), on STDL's 256-colour responsive screen.
 *
 * The screen is tracked in groups of 16 pixels, 20 to a row, a row's
 * groups a 32-bit mask. Each page keeps the mask of groups it owes:
 * drawn on the screen since that page was last drawn on. A copy joins
 * a batch - its texture and its offset on the screen, its whole groups
 * in one mask a row and the groups it covers in part as pixel masks -
 * which is converted at the present, or sooner when the next copy
 * comes from elsewhere or the texture is about to change. The present
 * converts the last batch, copies into the page what it owes and this
 * frame did not draw from the page last shown, which holds the screen
 * as it stands, hands this frame's groups to the other pages' masks,
 * stages the colours and flips. So a page is right whatever the
 * program does with its pictures between presents, and a frame costs
 * what it drew and what the pages missed, not the screen.
 *
 * Partial groups are converted from 16 gathered pixels and merged
 * plane by plane under their pixel mask, after the group has been
 * caught up from the page last shown if this page owes it. A TT's row
 * is two lines, which the conversion and the copies write both of, so
 * STDL's Flip has nothing to copy (nothing is reported to
 * STDL_UpdateRects).
 */

#include "internal.h"
#include "SDL_stdl.h"
#include "SDL_stdl_bench.h"
#include "c2p.h"

#define SCREEN_W  320
#define SCREEN_H  240
#define GROUPS    (SCREEN_W / 16)
#define ALLGROUPS ((Uint32)((1UL << GROUPS) - 1))

struct SDL_Window {
    int w, h;
    SDL_Renderer *renderer;
};

struct SDL_Texture {
    SDL_Renderer *r;
    SDL_Texture *next;
    Uint8 *pixels;
    int w, h, pitch;
    int access;
    int borrowed;               /* the program's pixels, not ours */
};

struct SDL_Renderer {
    SDL_Window *win;
    Uint32 flags;
    STDL_Surface *screen;
    long stride;                /* bytes a row */
    long dp;                    /* bytes a line: a TT's row is two */
    long margin;                /* bytes before the picture in a line */
    int dbl;
    int npages;
    int cur;                    /* the page being drawn on */
    Uint8 *page[3];             /* each page's picture, as STDL has it */
    Uint8 clear[3];             /* cleared when next drawn on */
    Uint32 owed[3][SCREEN_H];
    Uint32 now[SCREEN_H];       /* this frame's groups */
    const Uint8 *shown;         /* the page last flipped */
    int lw, lh, xoff, yoff;     /* the logical size and its place */

    unsigned x;                 /* the border index */
    Uint32 x2, x0;
    const struct stdl_sdl2_c2pv *v;

    SDL_Palette *pal;
    Uint32 palver;
    int paldirty;
    SDL_Color regs[256];
    SDL_Color drawrgb;
    Uint8 drawindex;
    int useindex;

    int drawn;                  /* anything drawn yet */
    int frame;                  /* drawn on since the last present */

    SDL_Texture *bt;            /* the batch: its texture */
    long bdx, bdy;              /* screen = texture + (bdx, bdy) */
    int by0, by1;
    Uint32 bwhole[SCREEN_H];
    Uint32 bpart[SCREEN_H];
    Uint16 bpix[SCREEN_H][GROUPS];

    Uint8 *model;               /* the check's chunky screen */
    Uint8 *scratch;             /* and its page */
    STDL_SDL2_Check chk;

    Uint32 tconv, tflip;
    SDL_Texture *textures;
};

static SDL_Window *the_window;
static STDL_Surface *the_screen;
static int screen_used;         /* a renderer has drawn on it */

/* a plane word, big-endian as the 68000 has it */
#ifdef __m68k__
#define RD16(p)    (*(const Uint16 *)(const void *)(p))
#define WR16(p, v) (*(Uint16 *)(void *)(p) = (v))
#else
static Uint16 RD16(const Uint8 *p)
{
    return (Uint16)(p[0] << 8 | p[1]);
}

static void WR16(Uint8 *p, Uint16 v)
{
    p[0] = (Uint8)(v >> 8);
    p[1] = (Uint8)v;
}
#endif

/* ------------------------------------------------------------------ */
/* the window                                                          */

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h,
                             Uint32 flags)
{
    SDL_Window *win;
    (void)title;
    (void)x;
    (void)y;
    (void)flags;
    if (!(stdl_sdl2_inited & SDL_INIT_VIDEO)) {
        SDL_SetError("SDL_Init(SDL_INIT_VIDEO) comes first");
        return NULL;
    }
    if (the_window) {
        SDL_SetError("One window at a time");
        return NULL;
    }
    if (w <= 0 || h <= 0 || w > SCREEN_W || h > SCREEN_H) {
        SDL_SetError("A window is at most %dx%d, not %dx%d", SCREEN_W,
                     SCREEN_H, w, h);
        return NULL;
    }
    if (!the_screen) {
        const char *pages = SDL_GetHint(STDL_SDL2_HINT_PAGES);
        Uint32 vflags = (pages && strcmp(pages, "2") == 0)
                        ? STDL_DOUBLEBUF : STDL_TRIPLEBUF;
        STDL_Surface *s;
        /* a constant, so that only these two options' code links */
        if (STDL_UseResponsive(STDL_RESPONSIVE_COLOUR |
                               STDL_RESPONSIVE_HEIGHT) < 0) {
            SDL_SetError("No responsive screen: %s", STDL_GetError());
            return NULL;
        }
        s = STDL_SetVideoMode(SCREEN_W, SCREEN_H, 8, vflags);
        if (!s || s->format->BitsPerPixel != 8 || s->h != SCREEN_H) {
            SDL_SetError("This program needs a TT or a Falcon with a colour"
                         " monitor: %s", STDL_GetError());
            return NULL;
        }
        /* colours set before a flip are shown with its page */
        STDL_StagePalette(1);
        the_screen = s;
    }
    win = (SDL_Window *)calloc(1, sizeof(*win));
    if (!win) {
        SDL_OutOfMemory();
        return NULL;
    }
    win->w = w;
    win->h = h;
    the_window = win;
    return win;
}

void SDL_DestroyWindow(SDL_Window *window)
{
    if (!window) {
        return;
    }
    if (window->renderer) {
        SDL_DestroyRenderer(window->renderer);
    }
    if (window == the_window) {
        the_window = NULL;
    }
    free(window);
}

void SDL_GetWindowSize(SDL_Window *window, int *w, int *h)
{
    if (w) {
        *w = window ? window->w : 0;
    }
    if (h) {
        *h = window ? window->h : 0;
    }
}

void stdl_sdl2_screen_origin(int *x, int *y)
{
    const SDL_Window *w = the_window;
    if (w && w->renderer) {
        *x = w->renderer->xoff;
        *y = w->renderer->yoff;
    } else if (w) {
        *x = ((SCREEN_W - w->w) / 2) & ~15;
        *y = (SCREEN_H - w->h) / 2;
    } else {
        *x = *y = 0;
    }
}

void SDL_SetWindowTitle(SDL_Window *window, const char *title)
{
    (void)window;
    (void)title;
}

/* ------------------------------------------------------------------ */
/* pages                                                               */

static int page_index(SDL_Renderer *r, Uint8 *pix)
{
    int i;
    for (i = 0; i < r->npages; i++) {
        if (r->page[i] == pix) {
            return i;
        }
        if (!r->page[i]) {
            r->page[i] = pix;
            return i;
        }
    }
    return 0;
}

/* the page being drawn on, cleared first if it is owed a clear */
static Uint8 *back_page(SDL_Renderer *r)
{
    Uint8 *pix = r->screen->pixels;
    int p = page_index(r, pix);
    r->cur = p;
    if (r->clear[p]) {
        memset(pix - r->margin, 0, (size_t)r->stride * SCREEN_H);
        r->clear[p] = 0;
    }
    return pix;
}

/* ------------------------------------------------------------------ */
/* the renderer                                                        */

static void place(SDL_Renderer *r, int w, int h)
{
    r->lw = w;
    r->lh = h;
    r->xoff = ((SCREEN_W - w) / 2) & ~15;
    r->yoff = (SCREEN_H - h) / 2;
}

SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags)
{
    SDL_Renderer *r;
    (void)index;
    if (!window) {
        SDL_InvalidParamError("window");
        return NULL;
    }
    if (window->renderer) {
        SDL_SetError("One renderer a window");
        return NULL;
    }
    r = (SDL_Renderer *)calloc(1, sizeof(*r));
    if (!r) {
        SDL_OutOfMemory();
        return NULL;
    }
    r->win = window;
    r->flags = flags;
    r->screen = the_screen;
    r->stride = the_screen->stride;
    /* a TT's 320x240 is TT Low's 480 lines, a row on two of them */
    r->dbl = r->stride == 2 * SCREEN_W;
    r->dp = r->dbl ? r->stride / 2 : r->stride;
    /* a Falcon's RGB line is 384 pixels, the picture in its middle */
    r->margin = (r->dp - SCREEN_W) / 2;
    r->npages = (the_screen->flags & STDL_TRIPLEBUF) ? 3 : 2;
    r->v = &stdl_sdl2_c2p_plain;
    r->by0 = SCREEN_H;
    r->by1 = -1;
    r->drawrgb.a = 255;
    place(r, window->w, window->h);
    /* a screen an earlier renderer drew on: every page is cleared when
     * next drawn on, as the one on show is not known yet */
    if (screen_used) {
        r->clear[0] = r->clear[1] = r->clear[2] = 1;
    }
    screen_used = 1;
    if (SDL_GetHintBoolean(STDL_SDL2_HINT_CHECK, SDL_FALSE)) {
        r->model = (Uint8 *)malloc(SCREEN_W * SCREEN_H);
        r->scratch = (Uint8 *)malloc((size_t)r->stride * SCREEN_H);
        if (!r->model || !r->scratch) {
            free(r->model);
            free(r->scratch);
            free(r);
            SDL_OutOfMemory();
            return NULL;
        }
        /* the pages start at 0, the colour X is converted to */
        memset(r->model, 0, SCREEN_W * SCREEN_H);
        r->chk.first_row = -1;
    }
    window->renderer = r;
    return r;
}

static void drop_batch(SDL_Renderer *r)
{
    int y;
    for (y = r->by0; y <= r->by1; y++) {
        r->bwhole[y] = 0;
        if (r->bpart[y]) {
            memset(r->bpix[y], 0, sizeof(r->bpix[y]));
            r->bpart[y] = 0;
        }
    }
    r->by0 = SCREEN_H;
    r->by1 = -1;
    r->bt = NULL;
}

void SDL_DestroyRenderer(SDL_Renderer *renderer)
{
    SDL_Texture *t, *next;
    if (!renderer) {
        return;
    }
    drop_batch(renderer);
    for (t = renderer->textures; t; t = next) {
        next = t->next;
        if (!t->borrowed) {
            free(t->pixels);
        }
        free(t);
    }
    if (renderer->win) {
        renderer->win->renderer = NULL;
    }
    free(renderer->model);
    free(renderer->scratch);
    free(renderer);
}

static void flush(SDL_Renderer *r);

int SDL_RenderSetLogicalSize(SDL_Renderer *renderer, int w, int h)
{
    if (!renderer) {
        return SDL_InvalidParamError("renderer");
    }
    if (w == 0 && h == 0) {
        w = renderer->win->w;
        h = renderer->win->h;
    }
    if (w <= 0 || h <= 0 || w > SCREEN_W || h > SCREEN_H) {
        return SDL_SetError("A logical size is at most %dx%d, not %dx%d",
                            SCREEN_W, SCREEN_H, w, h);
    }
    flush(renderer);
    place(renderer, w, h);
    return 0;
}

void SDL_RenderGetLogicalSize(SDL_Renderer *renderer, int *w, int *h)
{
    if (w) {
        *w = renderer ? renderer->lw : 0;
    }
    if (h) {
        *h = renderer ? renderer->lh : 0;
    }
}

/* ------------------------------------------------------------------ */
/* colours                                                             */

int STDL_SDL2_SetRenderPalette(SDL_Renderer *renderer, SDL_Palette *palette)
{
    if (!renderer) {
        return SDL_InvalidParamError("renderer");
    }
    renderer->pal = palette;
    renderer->paldirty = 1;
    return 0;
}

static int pal_changed(const SDL_Renderer *r)
{
    return r->paldirty || (r->pal && r->pal->version != r->palver);
}

/* register i takes colour i ^ X, register 0 black when X is set */
static void stage(SDL_Renderer *r)
{
    STDL_Colour c[256];
    int i;
    for (i = 0; i < 256; i++) {
        const unsigned j = (unsigned)i ^ r->x;
        SDL_Color s = { 0, 0, 0, 255 };
        if (r->pal && (int)j < r->pal->ncolors && !(r->x && i == 0)) {
            s = r->pal->colors[j];
        }
        r->regs[i] = s;
        c[i].r = s.r;
        c[i].g = s.g;
        c[i].b = s.b;
        c[i].unused = 0;
    }
    STDL_SetColours(r->screen, c, 0, 256);
    r->palver = r->pal ? r->pal->version : 0;
    r->paldirty = 0;
}

int STDL_SDL2_StagePalette(SDL_Renderer *renderer)
{
    if (!renderer) {
        return SDL_InvalidParamError("renderer");
    }
    stage(renderer);
    return 0;
}

const SDL_Color *STDL_SDL2_GetRegisterColours(SDL_Renderer *renderer)
{
    return renderer ? renderer->regs : NULL;
}

int SDL_SetRenderDrawColor(SDL_Renderer *renderer, Uint8 r, Uint8 g, Uint8 b,
                           Uint8 a)
{
    if (!renderer) {
        return SDL_InvalidParamError("renderer");
    }
    renderer->drawrgb.r = r;
    renderer->drawrgb.g = g;
    renderer->drawrgb.b = b;
    renderer->drawrgb.a = a;
    renderer->useindex = 0;
    return 0;
}

int STDL_SDL2_SetRenderDrawIndex(SDL_Renderer *renderer, Uint8 index)
{
    if (!renderer) {
        return SDL_InvalidParamError("renderer");
    }
    renderer->drawindex = index;
    renderer->useindex = 1;
    return 0;
}

/* the nearest palette entry, the lowest of equals */
static Uint8 draw_index(const SDL_Renderer *r)
{
    long best = -1;
    int i, n, bi = 0;
    if (r->useindex) {
        return r->drawindex;
    }
    if (!r->pal) {
        return 0;
    }
    n = r->pal->ncolors < 256 ? r->pal->ncolors : 256;
    for (i = 0; i < n; i++) {
        const SDL_Color *c = &r->pal->colors[i];
        const long dr = (long)c->r - r->drawrgb.r;
        const long dg = (long)c->g - r->drawrgb.g;
        const long db = (long)c->b - r->drawrgb.b;
        const long d = dr * dr + dg * dg + db * db;
        if (best < 0 || d < best) {
            best = d;
            bi = i;
        }
    }
    return (Uint8)bi;
}

int stdl_sdl2_set_border(SDL_Renderer *renderer, unsigned x,
                         const struct stdl_sdl2_c2pv *v)
{
    if (!renderer) {
        return SDL_InvalidParamError("renderer");
    }
    if ((x & 0x0F) || x > 0xF0) {
        return SDL_SetError("The border index is a multiple of 16 up to 0xF0,"
                            " not 0x%X", x);
    }
    if (renderer->drawn) {
        return SDL_SetError("The border index is set before anything is"
                            " drawn");
    }
    renderer->x = x;
    stdl_sdl2_c2p_xor(x, &renderer->x2, &renderer->x0);
    renderer->v = x ? v : &stdl_sdl2_c2p_plain;
    if (renderer->model) {
        /* pages of zeroes are colour X now */
        memset(renderer->model, (int)x, SCREEN_W * SCREEN_H);
    }
    renderer->paldirty = 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* the batch                                                           */

/* pixels a to b of a group, pixel 0 the top bit as in a plane word */
static Uint16 pixmask(int a, int b)
{
    return (Uint16)((0xFFFFu >> a) & (0xFFFFu << (15 - b)));
}

static void part(SDL_Renderer *r, int y, int g, Uint16 m)
{
    const Uint32 bit = 1UL << g;
    Uint16 *pm = &r->bpix[y][g];
    if (r->bwhole[y] & bit) {
        return;
    }
    *pm |= m;
    if (*pm == 0xFFFF) {
        r->bwhole[y] |= bit;
        r->bpart[y] &= ~bit;
        *pm = 0;
    } else {
        r->bpart[y] |= bit;
    }
}

/* screen pixels sx..sx+w-1 of rows sy..sy+h-1 */
static void batch_add(SDL_Renderer *r, int sx, int sy, int w, int h)
{
    const int x1 = sx + w;
    const int g0 = sx >> 4, g1 = (x1 - 1) >> 4;
    Uint32 whole = 0;
    int lg = -1, rg = -1, y;
    Uint16 lm = 0, rm = 0;
    if (g0 == g1) {
        const Uint16 m = pixmask(sx & 15, (x1 - 1) & 15);
        if (m == 0xFFFF) {
            whole = 1UL << g0;
        } else {
            lg = g0;
            lm = m;
        }
    } else {
        int fg = g0, lgw = g1;
        if (sx & 15) {
            lg = g0;
            lm = pixmask(sx & 15, 15);
            fg = g0 + 1;
        }
        if (x1 & 15) {
            rg = g1;
            rm = pixmask(0, (x1 - 1) & 15);
            lgw = g1 - 1;
        }
        if (fg <= lgw) {
            whole = (Uint32)(((1UL << (lgw + 1)) - 1) & ~((1UL << fg) - 1));
        }
    }
    for (y = sy; y < sy + h; y++) {
        r->bwhole[y] |= whole;
        if (lg >= 0) {
            part(r, y, lg, lm);
        }
        if (rg >= 0) {
            part(r, y, rg, rm);
        }
    }
    if (sy < r->by0) {
        r->by0 = sy;
    }
    if (sy + h - 1 > r->by1) {
        r->by1 = sy + h - 1;
    }
}

/* a group this page owes and this frame has not drawn, from the page
 * last shown */
static void catch_group(SDL_Renderer *r, Uint8 *d, int y, int g)
{
    const Uint8 *s;
    if (!r->shown || !((r->owed[r->cur][y] & ~r->now[y]) >> g & 1)) {
        return;
    }
    s = r->shown + y * r->stride + g * 16;
    memcpy(d, s, 16);
    if (r->dbl) {
        memcpy(d + r->dp, s, 16);
    }
}

static void merge_part(SDL_Renderer *r, Uint8 *back, int y, int g, Uint16 pm)
{
    const SDL_Texture *t = r->bt;
    const Uint8 *srow = t->pixels + (y - r->bdy) * t->pitch - r->bdx + g * 16;
    Uint8 *d = back + y * r->stride + g * 16;
    Uint8 in[16], out[16];
    int i, p;
    catch_group(r, d, y, g);
    for (i = 0; i < 16; i++) {
        in[i] = (pm & (0x8000u >> i)) ? srow[i] : 0;
    }
    stdl_sdl2_c2p_c(in, 16, out, 16, 1, 1, 0, r->x2, r->x0);
    for (p = 0; p < 8; p++) {
        const Uint16 w = (Uint16)((RD16(d + 2 * p) & ~pm) |
                                  (RD16(out + 2 * p) & pm));
        WR16(d + 2 * p, w);
        if (r->dbl) {
            WR16(d + r->dp + 2 * p, w);
        }
    }
}

/* n rows from y whose whole groups are m, a run of groups a call */
static void convert_rows(SDL_Renderer *r, Uint8 *back, int y, int n, Uint32 m)
{
    const SDL_Texture *t = r->bt;
    int g = 0;
    while (m) {
        int ge;
        while (!(m & 1)) {
            m >>= 1;
            g++;
        }
        ge = g;
        while (m & 1) {
            m >>= 1;
            ge++;
        }
        stdl_sdl2_c2p(t->pixels + (y - r->bdy) * t->pitch + (g * 16 - r->bdx),
                      t->pitch, back + y * r->stride + g * 16, r->dp, ge - g,
                      n, r->dbl, r->v, r->x2, r->x0);
        g = ge;
    }
}

static void flush(SDL_Renderer *r)
{
    Uint8 *back;
    int y;
    if (!r->bt) {
        return;
    }
    back = back_page(r);
    /* the partial groups first, the whole ones' bits not among them */
    for (y = r->by0; y <= r->by1; y++) {
        Uint32 pm = r->bpart[y];
        if (!pm) {
            continue;
        }
        pm &= ~r->bwhole[y];
        if (pm) {
            int g;
            for (g = 0; g < GROUPS; g++) {
                if (pm >> g & 1) {
                    merge_part(r, back, y, g, r->bpix[y][g]);
                }
            }
            r->now[y] |= pm;
        }
        memset(r->bpix[y], 0, sizeof(r->bpix[y]));
        r->bpart[y] = 0;
    }
    /* then runs of rows with the same whole groups, one call a run */
    for (y = r->by0; y <= r->by1; ) {
        const Uint32 m = r->bwhole[y];
        int y2 = y + 1, i;
        if (!m) {
            y++;
            continue;
        }
        while (y2 <= r->by1 && r->bwhole[y2] == m) {
            y2++;
        }
        convert_rows(r, back, y, y2 - y, m);
        for (i = y; i < y2; i++) {
            r->now[i] |= m;
            r->bwhole[i] = 0;
        }
        y = y2;
    }
    r->by0 = SCREEN_H;
    r->by1 = -1;
    r->bt = NULL;
}

/* ------------------------------------------------------------------ */
/* drawing                                                             */

int SDL_RenderClear(SDL_Renderer *renderer)
{
    SDL_Renderer *r = renderer;
    Uint8 pat[16], *back;
    Uint8 c;
    unsigned v;
    int y, g, p;
    if (!r) {
        return SDL_InvalidParamError("renderer");
    }
    drop_batch(r);
    c = draw_index(r);
    v = c ^ r->x;
    for (p = 0; p < 8; p++) {
        pat[2 * p] = pat[2 * p + 1] = (Uint8)((v >> p & 1) ? 0xFF : 0);
    }
    back = back_page(r);
    for (y = 0; y < SCREEN_H; y++) {
        Uint8 *line = back + y * r->stride;
        for (g = 0; g < GROUPS; g++) {
            memcpy(line + g * 16, pat, 16);
            if (r->dbl) {
                memcpy(line + r->dp + g * 16, pat, 16);
            }
        }
        r->now[y] = ALLGROUPS;
    }
    if (r->model) {
        memset(r->model, c, SCREEN_W * SCREEN_H);
    }
    r->frame = r->drawn = 1;
    return 0;
}

int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture,
                   const SDL_Rect *srcrect, const SDL_Rect *dstrect)
{
    SDL_Renderer *r = renderer;
    SDL_Rect s, d;
    long dx, dy;
    int sx, sy;
    if (!r || !texture || texture->r != r) {
        return SDL_SetError("RenderCopy: no such texture for this renderer");
    }
    if (srcrect) {
        s = *srcrect;
    } else {
        s.x = s.y = 0;
        s.w = texture->w;
        s.h = texture->h;
    }
    if (dstrect) {
        d = *dstrect;
    } else {
        d.x = d.y = 0;
        d.w = r->lw;
        d.h = r->lh;
    }
    if (s.w != d.w || s.h != d.h) {
        return SDL_SetError("RenderCopy would scale %dx%d to %dx%d: the"
                            " SDL2 add-on does not scale", s.w, s.h, d.w,
                            d.h);
    }
    /* the source within the texture, the destination moved with it */
    if (s.x < 0) {
        d.x -= s.x;
        d.w += s.x;
        s.w += s.x;
        s.x = 0;
    }
    if (s.y < 0) {
        d.y -= s.y;
        d.h += s.y;
        s.h += s.y;
        s.y = 0;
    }
    if (s.x + s.w > texture->w) {
        s.w = d.w = texture->w - s.x;
    }
    if (s.y + s.h > texture->h) {
        s.h = d.h = texture->h - s.y;
    }
    /* and the destination within the logical size */
    if (d.x < 0) {
        s.x -= d.x;
        s.w += d.x;
        d.w += d.x;
        d.x = 0;
    }
    if (d.y < 0) {
        s.y -= d.y;
        s.h += d.y;
        d.h += d.y;
        d.y = 0;
    }
    if (d.x + d.w > r->lw) {
        d.w = s.w = r->lw - d.x;
    }
    if (d.y + d.h > r->lh) {
        d.h = s.h = r->lh - d.y;
    }
    if (d.w <= 0 || d.h <= 0) {
        return 0;
    }
    sx = d.x + r->xoff;
    sy = d.y + r->yoff;
    dx = sx - s.x;
    dy = sy - s.y;
    if (r->bt && (r->bt != texture || r->bdx != dx || r->bdy != dy)) {
        flush(r);
    }
    r->bt = texture;
    r->bdx = dx;
    r->bdy = dy;
    batch_add(r, sx, sy, d.w, d.h);
    if (r->model) {
        int row;
        for (row = 0; row < d.h; row++) {
            memcpy(r->model + (sy + row) * SCREEN_W + sx,
                   texture->pixels + (s.y + row) * texture->pitch + s.x,
                   (size_t)d.w);
        }
    }
    r->frame = r->drawn = 1;
    return 0;
}

/* the groups the page owes and this frame did not draw, from the page
 * last shown, which holds the screen as it stands; a TT's row read
 * from its first line and written to both */
static void catch_up(SDL_Renderer *r, Uint8 *back)
{
    const long odd = r->dp >> 2;
    int y;
    if (!r->shown || r->shown == back) {
        return;
    }
    for (y = 0; y < SCREEN_H; y++) {
        Uint32 m = r->owed[r->cur][y] & ~r->now[y];
        int g = 0;
        while (m) {
            const Uint32 *s;
            Uint32 *d;
            int ge, n;
            while (!(m & 1)) {
                m >>= 1;
                g++;
            }
            ge = g;
            while (m & 1) {
                m >>= 1;
                ge++;
            }
            s = (const Uint32 *)(const void *)(r->shown + y * r->stride +
                                               g * 16);
            d = (Uint32 *)(void *)(back + y * r->stride + g * 16);
            for (n = (ge - g) * 4; n > 0; n--, s++, d++) {
                *d = *s;
                if (r->dbl) {
                    d[odd] = *s;
                }
            }
            g = ge;
        }
    }
}

/* the page about to be shown against the model converted, margins and
 * all */
static void check(SDL_Renderer *r, const Uint8 *back)
{
    const size_t bytes = (size_t)r->stride * SCREEN_H;
    const Uint8 *a = back - r->margin;
    size_t i;
    memset(r->scratch, 0, bytes);
    stdl_sdl2_c2p_c(r->model, SCREEN_W, r->scratch + r->margin, r->dp,
                    GROUPS, SCREEN_H, r->dbl, r->x2, r->x0);
    r->chk.flips++;
    for (i = 0; i < bytes; i++) {
        if (a[i] != r->scratch[i]) {
            if (!r->chk.bad) {
                r->chk.first_flip = r->chk.flips;
                r->chk.first_row = (int)(i / (size_t)r->stride);
            }
            r->chk.bad++;
            break;
        }
    }
}

void SDL_RenderPresent(SDL_Renderer *renderer)
{
    SDL_Renderer *r = renderer;
    Uint32 t0, t1, t2;
    Uint8 *back;
    int y, q;
    if (!r) {
        return;
    }
    if (!r->frame) {
        if (pal_changed(r)) {
            stage(r);
        } else if (r->flags & SDL_RENDERER_PRESENTVSYNC) {
            STDL_WaitVBL();
        }
        return;
    }
    t0 = STDL_GetHz38400();
    flush(r);
    back = back_page(r);
    catch_up(r, back);
    for (q = 0; q < r->npages; q++) {
        if (q == r->cur) {
            continue;
        }
        for (y = 0; y < SCREEN_H; y++) {
            r->owed[q][y] |= r->now[y];
        }
    }
    memset(r->owed[r->cur], 0, sizeof(r->owed[r->cur]));
    memset(r->now, 0, sizeof(r->now));
    if (r->model) {
        check(r, back);
    }
    if (pal_changed(r)) {
        stage(r);
    }
    t1 = STDL_GetHz38400();
    r->shown = back;
    STDL_Flip();
    t2 = STDL_GetHz38400();
    r->tconv += t1 - t0;
    r->tflip += t2 - t1;
    r->frame = 0;
}

int STDL_SDL2_ClearScreenNow(SDL_Renderer *renderer)
{
    SDL_Renderer *r = renderer;
    int p, shown = -1;
    if (!r) {
        return SDL_InvalidParamError("renderer");
    }
    drop_batch(r);
    if (r->shown) {
        shown = page_index(r, (Uint8 *)r->shown);
        memset((Uint8 *)r->shown - r->margin, 0, (size_t)r->stride * SCREEN_H);
    }
    for (p = 0; p < r->npages; p++) {
        r->clear[p] = p != shown;
    }
    memset(r->owed, 0, sizeof(r->owed));
    memset(r->now, 0, sizeof(r->now));
    if (r->model) {
        memset(r->model, (int)r->x, SCREEN_W * SCREEN_H);
    }
    r->frame = 0;
    r->drawn = 1;
    return 0;
}

int STDL_SDL2_GetShownPage(SDL_Renderer *renderer, const Uint8 **base,
                           long *bytes)
{
    if (!renderer) {
        return SDL_InvalidParamError("renderer");
    }
    if (base) {
        *base = renderer->shown ? renderer->shown - renderer->margin : NULL;
    }
    if (bytes) {
        *bytes = renderer->shown ? renderer->stride * SCREEN_H : 0;
    }
    return 0;
}

int STDL_SDL2_GetCheck(SDL_Renderer *renderer, STDL_SDL2_Check *check)
{
    if (!renderer || !check) {
        return SDL_InvalidParamError("renderer");
    }
    if (!renderer->model) {
        return SDL_SetError("The screen check is off (STDL_SDL2_HINT_CHECK)");
    }
    *check = renderer->chk;
    return 0;
}

void STDL_SDL2_GetPresentTimes(SDL_Renderer *renderer, Uint32 *convert,
                               Uint32 *flip, int reset)
{
    if (!renderer) {
        return;
    }
    if (convert) {
        *convert = renderer->tconv;
    }
    if (flip) {
        *flip = renderer->tflip;
    }
    if (reset) {
        renderer->tconv = renderer->tflip = 0;
    }
}

/* ------------------------------------------------------------------ */
/* textures                                                            */

static SDL_Texture *new_texture(SDL_Renderer *r, Uint8 *pixels, int w, int h,
                                int pitch, int access, int borrowed)
{
    SDL_Texture *t = (SDL_Texture *)calloc(1, sizeof(*t));
    if (!t) {
        SDL_OutOfMemory();
        return NULL;
    }
    t->r = r;
    t->pixels = pixels;
    t->w = w;
    t->h = h;
    t->pitch = pitch;
    t->access = access;
    t->borrowed = borrowed;
    t->next = r->textures;
    r->textures = t;
    return t;
}

SDL_Texture *SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format,
                               int access, int w, int h)
{
    Uint8 *pixels;
    SDL_Texture *t;
    if (!renderer) {
        SDL_InvalidParamError("renderer");
        return NULL;
    }
    if (format != SDL_PIXELFORMAT_INDEX8) {
        SDL_SetError("Textures are SDL_PIXELFORMAT_INDEX8 here");
        return NULL;
    }
    if (access != SDL_TEXTUREACCESS_STATIC &&
        access != SDL_TEXTUREACCESS_STREAMING) {
        SDL_SetError("Textures are static or streaming here");
        return NULL;
    }
    if (w <= 0 || h <= 0) {
        SDL_SetError("A texture of %dx%d", w, h);
        return NULL;
    }
    pixels = (Uint8 *)calloc((size_t)w * h, 1);
    if (!pixels) {
        SDL_OutOfMemory();
        return NULL;
    }
    t = new_texture(renderer, pixels, w, h, w, access, 0);
    if (!t) {
        free(pixels);
    }
    return t;
}

SDL_Texture *STDL_SDL2_CreateTextureFrom(SDL_Renderer *renderer, void *pixels,
                                         int w, int h, int pitch)
{
    if (!renderer || !pixels) {
        SDL_InvalidParamError("pixels");
        return NULL;
    }
    if (w <= 0 || h <= 0 || pitch < w) {
        SDL_SetError("A texture of %dx%d, %d bytes a row", w, h, pitch);
        return NULL;
    }
    return new_texture(renderer, (Uint8 *)pixels, w, h, pitch,
                       SDL_TEXTUREACCESS_STREAMING, 1);
}

void SDL_DestroyTexture(SDL_Texture *texture)
{
    SDL_Renderer *r;
    SDL_Texture **pp;
    if (!texture) {
        return;
    }
    r = texture->r;
    if (r->bt == texture) {
        flush(r);
    }
    for (pp = &r->textures; *pp; pp = &(*pp)->next) {
        if (*pp == texture) {
            *pp = texture->next;
            break;
        }
    }
    if (!texture->borrowed) {
        free(texture->pixels);
    }
    free(texture);
}

static int texture_rect(const SDL_Texture *t, const SDL_Rect *rect,
                        SDL_Rect *out)
{
    if (!rect) {
        out->x = out->y = 0;
        out->w = t->w;
        out->h = t->h;
        return 0;
    }
    if (rect->x < 0 || rect->y < 0 || rect->w < 0 || rect->h < 0 ||
        rect->x + rect->w > t->w || rect->y + rect->h > t->h) {
        return SDL_SetError("A rectangle outside the texture");
    }
    *out = *rect;
    return 0;
}

int SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect,
                      const void *pixels, int pitch)
{
    SDL_Rect r;
    const Uint8 *src = (const Uint8 *)pixels;
    int y;
    if (!texture || !pixels) {
        return SDL_InvalidParamError("texture");
    }
    if (texture_rect(texture, rect, &r) < 0) {
        return -1;
    }
    if (texture->r->bt == texture) {
        flush(texture->r);
    }
    for (y = 0; y < r.h; y++) {
        memcpy(texture->pixels + (r.y + y) * texture->pitch + r.x,
               src + y * pitch, (size_t)r.w);
    }
    return 0;
}

int SDL_LockTexture(SDL_Texture *texture, const SDL_Rect *rect,
                    void **pixels, int *pitch)
{
    SDL_Rect r;
    if (!texture || !pixels || !pitch) {
        return SDL_InvalidParamError("texture");
    }
    if (texture->access != SDL_TEXTUREACCESS_STREAMING) {
        return SDL_SetError("SDL_LockTexture takes a streaming texture");
    }
    if (texture_rect(texture, rect, &r) < 0) {
        return -1;
    }
    if (texture->r->bt == texture) {
        flush(texture->r);
    }
    *pixels = texture->pixels + r.y * texture->pitch + r.x;
    *pitch = texture->pitch;
    return 0;
}

void SDL_UnlockTexture(SDL_Texture *texture)
{
    (void)texture;
}

int SDL_QueryTexture(SDL_Texture *texture, Uint32 *format, int *access,
                     int *w, int *h)
{
    if (!texture) {
        return SDL_InvalidParamError("texture");
    }
    if (format) {
        *format = SDL_PIXELFORMAT_INDEX8;
    }
    if (access) {
        *access = texture->access;
    }
    if (w) {
        *w = texture->w;
    }
    if (h) {
        *h = texture->h;
    }
    return 0;
}
