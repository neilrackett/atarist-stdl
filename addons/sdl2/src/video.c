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

/*
 * The ST build (st.h), experimental: the same screen of 320x240 and the
 * same compositor on an ST's page, which has four planes, so a group
 * is 8 bytes. A window of up to 200 rows gets ST Low, whose page shows
 * the screen's middle 200 rows and not the 20 above and below. A
 * taller one gets STDL's tall screen (stdl_responsive.h), 240 rows
 * with an ST's bottom border open, and two pages, the border's own;
 * if the border cannot open, ST Low. ROW0 and ROWS are what was got.
 * The pixels go through a reduction of the palette to 16 colours, made
 * from the colours on the page (st.c): two maps, a colour's first slot
 * and its second, which a checkerboard by the screen's x + y mixes. The
 * maps are judged again at a present after a palette change or a colour
 * on the page that was not there before, and made again when they no
 * longer fit. When they move the pages are stale: the next one drawn on
 * is converted again whole, from a chunky copy of the screen kept for
 * that (the screen check's model), and the others catch up from it as
 * from any frame.
 */
#ifdef STDL_SDL2_ST
#include "st.h"
/* One program for every machine: a TT or a Falcon gets the 256-colour
 * screen and the 68030's conversion, as the add-on built for them
 * gives it, and st_on is 0; an ST, an STE or a Mega STE gets the above.
 * So what is a constant in the other build is read here. */
static int st_on, st_rows = SCREEN_H;
#define PLANES    (st_on ? 4 : 8)
#define ROW0      ((SCREEN_H - st_rows) >> 1)
#define ROWS      st_rows
/* group g's bytes into a row, a shift so that no multiply is called */
#define GOFF(g)   ((g) << (st_on ? 3 : 4))
/* the conversion's tables for the screen's row y and the row after:
 * even rows show a colour's first slot at an even x, odd rows at an
 * odd x */
#define ST_TABS(r, y) (r)->tab[(y) & 1], (r)->tab[~(y) & 1]
/* and the pair tables, which are made from them (stpair.c) */
#define ST_PTABS(r, y) (r)->ptab + ((y) & 1) * 1024, \
                       (r)->ptab + (~(y) & 1) * 1024
#define GLONGS(n) ((n) << (st_on ? 1 : 2))  /* and n groups in longs */
/* a row or a texture's row times a stride: the 68000's own multiply,
 * where a 32-bit one is a library call (textures keep to 16 bits) */
#define MUL(a, b) STDL_MulS16((int)(a), (int)(b))
#else
#define PLANES    8
#define ROW0      0
#define ROWS      SCREEN_H
#define GOFF(g)   ((g) * 16)
#define GLONGS(n) ((n) * 4)
#define MUL(a, b) ((a) * (b))
#endif
#define GB        (2 * PLANES)  /* bytes a group */
/* the chunky copy of the screen is there for more than the check, so
 * the check's own page says whether the check is on */
#define CHECKED(r) ((r)->scratch != NULL)
/* The rows this frame drew on, a to b: a present reads and clears
 * those of now, and of each page's owed the rows it owes, and no
 * others. Going through all 240 rows of now and of each page's owed
 * was 56,000 of a 68000's cycles a present, 7ms at 8MHz, whatever was
 * drawn; a 68030 hardly feels it (0.1ms of one port's frame), and
 * keeps the ranges so that both builds have the one way. */
#define NOW_ROWS(r, a, b) do { \
        if ((a) < (r)->ny0) { \
            (r)->ny0 = (a); \
        } \
        if ((b) > (r)->ny1) { \
            (r)->ny1 = (b); \
        } \
    } while (0)

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
    int bparts;                 /* any partial groups in it */
    Uint32 bwhole[SCREEN_H];
    Uint32 bpart[SCREEN_H];
    Uint16 bpix[SCREEN_H][GROUPS];
    Uint32 bpair[SCREEN_H];     /* whole groups only pairs copies made */
    int bpaired;                /* any in this batch */
    int pairing;                /* the copy being made is of pairs */
    const struct stdl_sdl2_pairs *pairs;    /* c2ppair.c's side */

    Uint8 *model;               /* the chunky copy of the screen: what
                                 * copies are compared with, the check
                                 * converts and an ST converts again */
    int compare;                /* copies of whole groups are compared
                                 * with it (copy_changed) */
    int keep;                   /* it is kept as copies are made: the
                                 * compare, the check or an ST want it */
    int holes;                  /* groups of it may not be the screen's,
                                 * as copies went by while it was not
                                 * kept: those mvalid has, and none other.
                                 * Unkept, a copy takes its groups out of
                                 * mvalid and a clear all of them */
    Uint32 mvalid[SCREEN_H];
    Uint8 *scratch;             /* the check's page */
    STDL_SDL2_Check chk;

#ifdef STDL_SDL2_ST
    struct stdl_sdl2_st_maps maps;  /* a colour's two slots of the 16 */
    Uint32 tab[2][4 * 256];     /* as the conversion has them: even
                                 * rows' tables, and odd rows' */
    Uint32 *ptab;               /* the same for pixel pairs, 8K made at
                                 * the first pairs copy (stpair.c) */
    int bits;                   /* a channel as the machine shows it */
    int dither;                 /* a checkerboard of two slots (st.h) */
    int stale;                  /* the pages hold older maps' slots */
    Uint8 seen[256];            /* colours on the page since the count */
    int fresh;                  /* and one there that was not before */
    int given;                  /* the program's map and colours
                                 * (STDL_SDL2_SetSTColours), in regs */
    Uint8 gmap[256];            /* and its map's first slots */
    int gnew;                   /* a map not yet made into the maps */
    int regdirty;               /* the 16 colours are to be staged */
#endif
    int ny0, ny1;               /* the rows of now that are set */
    int oy0[3], oy1[3];         /* and of each page's owed */

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
#ifdef STDL_SDL2_ST
        st_on = STDL_SDL2_NO_256();
        if (st_on) {
            const int tall = h > 200 &&
                             SDL_GetHintBoolean(STDL_SDL2_HINT_ST_TALL,
                                                SDL_TRUE) &&
                             STDL_UseResponsive(STDL_RESPONSIVE_TALL) == 0;
            s = STDL_SetVideoMode(SCREEN_W, tall ? SCREEN_H : 200, 4, vflags);
            if (!s || s->format->BitsPerPixel != 4 || s->h < 200 ||
                s->h > SCREEN_H) {
                SDL_SetError("No ST screen: %s", STDL_GetError());
                return NULL;
            }
            st_rows = s->h;
        } else
#endif
        {
            /* a constant, so that only these two options' code links */
            if (STDL_UseResponsive(STDL_RESPONSIVE_COLOUR |
                                   STDL_RESPONSIVE_HEIGHT) < 0) {
                SDL_SetError("No responsive screen: %s", STDL_GetError());
                return NULL;
            }
            s = STDL_SetVideoMode(SCREEN_W, SCREEN_H, 8, vflags);
            if (!s || s->format->BitsPerPixel != 8 || s->h != SCREEN_H) {
                SDL_SetError("This program needs a TT or a Falcon with a"
                             " colour monitor: %s", STDL_GetError());
                return NULL;
            }
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
#ifdef STDL_SDL2_ST
    /* the page starts at the screen's row ROW0 */
    *y -= ROW0;
#endif
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

#ifdef STDL_SDL2_ST
/* an ST page's rows all colour c, as the maps have it now: a group of
 * it through the conversion's own tables, for a row of the page's
 * first row's parity and one of the other, so that the checkerboard is
 * the conversion's and not written out again here */
static void st_fill(SDL_Renderer *r, Uint8 *pix, unsigned c)
{
    Uint8 px[16];
    Uint32 pat[4];
    int y, g;
    memset(px, (int)c, sizeof(px));
    stdl_sdl2_st_c2p(px, 0, (Uint8 *)pat, 8, 1, 2, ST_TABS(r, ROW0));
    for (y = 0; y < ROWS; y++) {
        const Uint32 lo = pat[(y & 1) << 1], hi = pat[((y & 1) << 1) + 1];
        Uint32 *d = (Uint32 *)(void *)(pix + MUL(y, r->stride));
        for (g = GROUPS; g > 0; g--) {
            *d++ = lo;
            *d++ = hi;
        }
    }
}
#endif

/* the page being drawn on, cleared first if it is owed a clear; where
 * the screen's row 0 would be, as a page of the ST build's starts at
 * row ROW0 */
static Uint8 *back_page(SDL_Renderer *r)
{
    Uint8 *pix = r->screen->pixels;
    int p = page_index(r, pix);
    r->cur = p;
    if (r->clear[p]) {
#ifdef STDL_SDL2_ST
        if (st_on) {
            st_fill(r, pix, r->x);
        } else
#endif
        memset(pix - r->margin, 0, (size_t)r->stride * SCREEN_H);
        r->clear[p] = 0;
    }
    return pix - ROW0 * r->stride;
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

/* no group of now set, none owed by any page */
static void no_rows(SDL_Renderer *r)
{
    memset(r->owed, 0, sizeof(r->owed));
    memset(r->now, 0, sizeof(r->now));
    r->ny0 = r->oy0[0] = r->oy0[1] = r->oy0[2] = SCREEN_H;
    r->ny1 = r->oy1[0] = r->oy1[1] = r->oy1[2] = -1;
}

/* a frame's end: its rows owed by the other pages, and nothing owed
 * by this one */
static void frame_rows(SDL_Renderer *r)
{
    const int cur = r->cur;
    int q, y;
    for (q = 0; q < r->npages; q++) {
        Uint32 *o = r->owed[q] + r->ny0;
        const Uint32 *n = r->now + r->ny0;
        if (q == cur) {
            continue;
        }
        for (y = r->ny1 - r->ny0; y >= 0; y--) {
            *o++ |= *n++;
        }
        if (r->ny0 < r->oy0[q]) {
            r->oy0[q] = r->ny0;
        }
        if (r->ny1 > r->oy1[q]) {
            r->oy1[q] = r->ny1;
        }
    }
    for (y = r->oy0[cur]; y <= r->oy1[cur]; y++) {
        r->owed[cur][y] = 0;
    }
    for (y = r->ny0; y <= r->ny1; y++) {
        r->now[y] = 0;
    }
    r->oy0[cur] = r->ny0 = SCREEN_H;
    r->oy1[cur] = r->ny1 = -1;
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
#ifdef STDL_SDL2_ST
    if (st_on) {
        /* an ST's page: a line a row, no margin */
        r->dp = r->stride;
        r->bits = STDL_GetMachineInfo()->is_ste ? 4 : 3;
        r->dither = SDL_GetHintBoolean(STDL_SDL2_HINT_ST_DITHER, SDL_TRUE);
        r->npages = (the_screen->flags & STDL_TRIPLEBUF) ? 3 : 2;
    } else
#endif
    {
        /* a TT's 320x240 is TT Low's 480 lines, a row on two of them */
        r->dbl = r->stride == 2 * SCREEN_W;
        r->dp = r->dbl ? r->stride / 2 : r->stride;
        /* a Falcon's RGB line is 384 pixels, the picture in its middle */
        r->margin = (r->dp - SCREEN_W) / 2;
        r->npages = (the_screen->flags & STDL_TRIPLEBUF) ? 3 : 2;
        r->v = &stdl_sdl2_c2p_plain;
    }
    r->by0 = SCREEN_H;
    r->by1 = -1;
    no_rows(r);
    r->drawrgb.a = 255;
    place(r, window->w, window->h);
    /* a screen an earlier renderer drew on: every page is cleared when
     * next drawn on, as the one on show is not known yet */
    if (screen_used) {
        r->clear[0] = r->clear[1] = r->clear[2] = 1;
    }
    screen_used = 1;
    {
        /* The chunky copy of the screen: an ST's always, to convert
         * again after new maps and to compare copies with; a TT's or a
         * Falcon's for the compare or the check. It starts at 0 as the
         * pages do, the colour X is converted to. The check has a page
         * of its own besides. The compare is a hint: without the
         * memory for the copy there is none. */
        const int chk = SDL_GetHintBoolean(STDL_SDL2_HINT_CHECK, SDL_FALSE);
        int cmp = SDL_GetHintBoolean(STDL_SDL2_HINT_COMPARE, SDL_FALSE);
        int must = chk;
#ifdef STDL_SDL2_ST
        must |= st_on;
        cmp |= st_on;
#endif
        if (must || cmp) {
            r->model = (Uint8 *)calloc(SCREEN_W * SCREEN_H, 1);
        }
        if (chk) {
            r->scratch = (Uint8 *)malloc((size_t)r->stride * ROWS);
            r->chk.first_row = -1;
        }
        if (must && (!r->model || (chk && !r->scratch))) {
            free(r->model);
            free(r->scratch);
            free(r);
            SDL_OutOfMemory();
            return NULL;
        }
        r->compare = cmp && r->model;
        r->keep = r->model != NULL;
    }
    window->renderer = r;
    return r;
}

static void drop_batch(SDL_Renderer *r)
{
    int y;
    for (y = r->by0; y <= r->by1; y++) {
        r->bwhole[y] = 0;
        r->bpair[y] = 0;
        if (r->bpart[y]) {
            memset(r->bpix[y], 0, sizeof(r->bpix[y]));
            r->bpart[y] = 0;
        }
    }
    r->by0 = SCREEN_H;
    r->by1 = -1;
    r->bparts = 0;
    r->bpaired = 0;
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
#ifdef STDL_SDL2_ST
    free(renderer->ptab);
#endif
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

#ifdef STDL_SDL2_ST
/*
 * The colours on the page. The reduction is made from them (st.c says
 * why), so the renderer keeps which it has seen: counted from the
 * chunky copy of the screen, then each colour a copy or a clear puts
 * on the page noted as it goes there, which says when one comes that
 * was not (fresh). A colour that goes stays noted until the next
 * count, which is made only when a palette change has left the maps
 * not fitting, as a room's change does: the count is a pass over the
 * page, and a fade's steps keep their maps and so never pay it.
 */
/* 1 if a group's 16 pixels are all colours seen: the check for every
 * group a copy changes, so on the machine 16 lookups and no branch,
 * about 22 cycles a pixel, where gcc's loop took 60 */
static __inline__ int st_seen16(const Uint8 *seen, const Uint8 *p)
{
#ifdef __m68k__
    unsigned long v;
    Uint8 all = 1;
    __asm__("moveq	#0,%1\n"
            ".rept	16\n"
            "	move.b	(%2)+,%1\n"
            "	and.b	0(%3,%1.w),%0\n"
            ".endr"
            : "+d"(all), "=&d"(v), "+a"(p)
            : "a"(seen)
            : "cc");
    return all;
#else
    Uint8 all = 1;
    int k;
    for (k = 0; k < 16; k++) {
        all &= seen[p[k]];
    }
    return all;
#endif
}

/* colours marked seen, fresh if one was not */
static void st_mark(SDL_Renderer *r, const Uint8 *p, int n)
{
    Uint8 *seen = r->seen;
    int k;
    for (k = 0; k < n; k++) {
        if (!seen[p[k]]) {
            seen[p[k]] = 1;
            r->fresh = 1;
        }
    }
}

/* n pixels put on the page: 16 at a time through st_seen16, marked only
 * where one is a colour not seen */
static void st_note(SDL_Renderer *r, const Uint8 *p, int n)
{
    for (; n >= 16; n -= 16, p += 16) {
        if (!st_seen16(r->seen, p)) {
            st_mark(r, p, 16);
        }
    }
    if (n > 0) {
        st_mark(r, p, n);
    }
}


/* the colours on the page, from the chunky copy: on the machine 16
 * pixels a pass with the index's top bits cleared once, about 22
 * cycles a pixel, where gcc's loop took 57 (a count 228ms on a Mega
 * STE, 11 of them in one cutscene). A colour seen is 1, as st_mark
 * makes it: an st, 0xFF, broke the test on the machine alone. */
static void st_count(SDL_Renderer *r)
{
    const Uint8 *p = r->model + MUL(ROW0, SCREEN_W);
    Uint8 *seen = r->seen;
    long n = MUL(ROWS, SCREEN_W) >> 4;
    memset(seen, 0, sizeof(r->seen));
#ifdef __m68k__
    {
        unsigned long v;
        __asm__ volatile("moveq	#0,%1\n"
                         "1:\n"
                         ".rept	16\n"
                         "	move.b	(%2)+,%1\n"
                         "	move.b	%4,0(%3,%1.w)\n"
                         ".endr\n"
                         "	subq.l	#1,%0\n"
                         "	bne.s	1b"
                         : "+d"(n), "=&d"(v), "+a"(p)
                         : "a"(seen), "d"(1)
                         : "cc", "memory");
    }
#else
    for (; n > 0; n--, p += 16) {
        int k;
        for (k = 0; k < 16; k++) {
            seen[p[k]] = 1;
        }
    }
#endif
}

/* The palette as 16 colours and two maps onto them, at a palette change
 * or a colour seen that was not: the maps kept if they fit, else made
 * again, from a fresh count after a palette change. Only then: after a
 * colour comes, what has been seen is what the page shows and the
 * colours gone since the count, which the next palette change drops,
 * so a program that never changes its palette keeps every colour it
 * has shown and its maps drift toward the palette's as a whole. A count
 * at every remake would cost a pass over the page each time (90-105ms
 * on a Mega STE) and was not measured as better for a game that
 * changes its palette with its rooms. A program's own map and colours
 * (STDL_SDL2_SetSTColours) take the place of all that, the second
 * slots made when the map is new: made again at each change of the
 * palette or the 16, they would move at a fade's last step, all its
 * colours black, and a move is a whole screen converted again, where
 * an ST without the add-on writes 16 registers. Maps that
 * moved leave the pages stale and are a frame to show, drawn on or
 * not; the colours wait for st_registers, just before the flip that
 * shows a page converted for them. Says whether the maps moved. */
static int st_reduce(SDL_Renderer *r)
{
    const SDL_Color *col = r->pal ? r->pal->colors : NULL;
    const int ncol = r->pal ? r->pal->ncolors : 0;
    int moved = 0;
    if (r->given) {
        /* the program's map, its second slots from the colours it gave
         * over the palette as they are now; under the same map new
         * colours or a new palette are the registers alone */
        if (r->gnew) {
            Uint8 b[256];
            stdl_sdl2_st_seconds(col, ncol, r->x, r->regs, r->gmap, b,
                                 r->dither);
            moved = stdl_sdl2_st_use(&r->maps, r->gmap, b);
            r->gnew = 0;
        }
    } else if (!stdl_sdl2_st_keep(col, ncol, r->x, r->bits, r->seen,
                                  &r->maps, r->regs)) {
        if (pal_changed(r)) {
            st_count(r);
        }
        moved = stdl_sdl2_st_remake(col, ncol, r->x, r->bits, r->dither,
                                    r->seen, &r->maps, r->regs);
    }
    r->fresh = 0;
    if (moved) {
        stdl_sdl2_st_spread(r->maps.a, r->maps.b, r->tab[0]);
        if (r->ptab) {
            r->pairs->st_tabs(r->tab[0], r->ptab);
        }
        r->stale = r->frame = 1;
    }
    r->palver = r->pal ? r->pal->version : 0;
    r->paldirty = 0;
    r->regdirty = 1;
    return moved;
}

const struct stdl_sdl2_st_maps *stdl_sdl2_st_view(SDL_Renderer *renderer,
                                                  int *bits, int *dither,
                                                  const Uint8 **seen)
{
    *bits = renderer->bits;
    *dither = renderer->dither;
    *seen = renderer->seen;
    return &renderer->maps;
}

int STDL_SDL2_SetSTColours(SDL_Renderer *renderer, const Uint8 *map,
                           const SDL_Color *colours)
{
    SDL_Renderer *r = renderer;
    int i;
    if (!r) {
        return SDL_InvalidParamError("renderer");
    }
    if (!st_on) {
        return SDL_SetError("The 16 colours are an ST's screen's");
    }
    if (!map) {
        /* the add-on's own again: new maps from a count */
        if (r->given) {
            r->given = r->gnew = 0;
            r->maps.made_valid = 0;
            r->paldirty = 1;
        }
        return 0;
    }
    if (!colours) {
        return SDL_InvalidParamError("colours");
    }
    for (i = 0; i < 256; i++) {
        r->gmap[i] = (Uint8)(map[i] & 15);
    }
    for (i = 0; i < 16; i++) {
        r->regs[i] = colours[i];
        r->regs[i].a = 255;
    }
    /* a new map made into the maps and the colours to the registers,
     * at the next present or staging */
    r->gnew |= !r->given || memcmp(r->gmap, r->maps.a, sizeof(r->gmap));
    r->given = r->paldirty = 1;
    return 0;
}

static void st_registers(SDL_Renderer *r)
{
    STDL_Colour c[16];
    int i;
    for (i = 0; i < 16; i++) {
        c[i].r = r->regs[i].r;
        c[i].g = r->regs[i].g;
        c[i].b = r->regs[i].b;
        c[i].unused = 0;
    }
    STDL_SetColours(r->screen, c, 0, 16);
    r->regdirty = 0;
}

/* a staging's colours on an ST: with new maps, a present */
static void st_stage(SDL_Renderer *r)
{
    if (st_reduce(r)) {
        SDL_RenderPresent(r);
    } else {
        st_registers(r);
    }
}
#endif

/* register i takes colour i ^ X, register 0 black when X is set */
static void stage(SDL_Renderer *r)
{
    STDL_Colour c[256];
    int i;
#ifdef STDL_SDL2_ST
    if (st_on) {
        st_stage(r);
        return;
    }
#endif
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

/* a box's partial groups, row by row */
static void batch_parts(SDL_Renderer *r, int sy, int h, int lg, Uint16 lm,
                        int rg, Uint16 rm)
{
    int y;
    for (y = sy; y < sy + h; y++) {
        if (lg >= 0) {
            part(r, y, lg, lm);
        }
        if (rg >= 0) {
            part(r, y, rg, rm);
        }
    }
    r->bparts = 1;
}

/* a box that starts or ends inside a group: its whole groups and its
 * partial ones. Kept out of batch_add, as a program that copies whole
 * groups never has any. */
static __attribute__((noinline)) void batch_any(SDL_Renderer *r, int sx,
                                                int sy, int w, int h)
{
    const int x1 = sx + w;
    const int g0 = sx >> 4, g1 = (x1 - 1) >> 4;
    Uint32 whole = 0;
    int lg = -1, rg = -1;
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
    if (whole) {
        Uint32 *wr = &r->bwhole[sy];
        int n;
        for (n = h; n > 0; n--) {
            *wr++ |= whole;
        }
    }
    /* the box's whole groups are never its partial ones, so the order
     * of the two makes no difference */
    if (lg >= 0 || rg >= 0) {
        batch_parts(r, sy, h, lg, lm, rg, rm);
    }
}

/* screen pixels sx..sx+w-1 of rows sy..sy+h-1: of whole groups, an OR
 * a row. A program pushes many small boxes (one port's play, 14 a
 * frame), and a Falcon fetches every instruction of them from ST-RAM. */
static void batch_add(SDL_Renderer *r, int sx, int sy, int w, int h)
{
#ifdef STDL_SDL2_ST
    /* the rows the page has */
    if (sy < ROW0) {
        h -= ROW0 - sy;
        sy = ROW0;
    }
    if (sy + h > ROW0 + ROWS) {
        h = ROW0 + ROWS - sy;
    }
    if (h <= 0) {
        return;
    }
#endif
    /* pixel pairs, once the batch has a pairs copy (c2ppair.c) */
    if (r->pairing | r->bpaired) {
        r->pairs->mark(r->bpair + sy, r->bwhole + sy, r->bpart + sy, h, sx,
                       w, r->pairing);
        r->bpaired |= r->pairing;
    }
    if (((sx | w) & 15) == 0) {
        const Uint32 whole = (Uint32)(((1UL << ((sx + w) >> 4)) - 1) &
                                      ~((1UL << (sx >> 4)) - 1));
        Uint32 *wr = &r->bwhole[sy];
        int n;
        for (n = h; n > 0; n--) {
            *wr++ |= whole;
        }
    } else {
        batch_any(r, sx, sy, w, h);
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
    s = r->shown + MUL(y, r->stride) + GOFF(g);
    memcpy(d, s, GB);
    if (r->dbl) {
        memcpy(d + r->dp, s, GB);
    }
}

static void merge_part(SDL_Renderer *r, Uint8 *back, int y, int g, Uint16 pm)
{
    const SDL_Texture *t = r->bt;
    const Uint8 *srow = t->pixels + MUL(y - r->bdy, t->pitch) - r->bdx + g * 16;
    Uint8 *d = back + MUL(y, r->stride) + GOFF(g);
    Uint8 in[16], out[16];
    int i, p;
    catch_group(r, d, y, g);
    for (i = 0; i < 16; i++) {
        in[i] = (pm & (0x8000u >> i)) ? srow[i] : 0;
    }
#ifdef STDL_SDL2_ST
    if (st_on) {
        stdl_sdl2_st_c2p(in, 16, out, GB, 1, 1, ST_TABS(r, y));
    } else
#endif
    stdl_sdl2_c2p_c(in, 16, out, 16, 1, 1, 0, r->x2, r->x0);
    for (p = 0; p < PLANES; p++) {
        const Uint16 w = (Uint16)((RD16(d + 2 * p) & ~pm) |
                                  (RD16(out + 2 * p) & pm));
        WR16(d + 2 * p, w);
        if (r->dbl) {
            WR16(d + r->dp + 2 * p, w);
        }
    }
}

/* n rows from y whose whole groups are m, a run of groups a call;
 * those of p, within m, as pixel pairs */
static void convert_rows(SDL_Renderer *r, Uint8 *back, int y, int n, Uint32 m,
                         Uint32 p)
{
    const SDL_Texture *t = r->bt;
    int pair;
    for (pair = 0; pair < 2; pair++) {
        Uint32 k = pair ? p : m & ~p;
        int g = 0;
        while (k) {
            int ge;
            while (!(k & 1)) {
                k >>= 1;
                g++;
            }
            ge = g;
            while (k & 1) {
                k >>= 1;
                ge++;
            }
#ifdef STDL_SDL2_ST
            if (st_on) {
                const Uint8 *src = t->pixels + MUL(y - r->bdy, t->pitch) +
                                   (g * 16 - r->bdx);
                Uint8 *dst = back + MUL(y, r->stride) + GOFF(g);
                /* pairs are read a word each, from even addresses; at an
                 * odd one, converted whole */
                if (pair && !(((unsigned long)src | (unsigned long)t->pitch) &
                              1)) {
                    r->pairs->st_conv(src, t->pitch, dst, r->dp, ge - g, n,
                                      ST_PTABS(r, y));
                } else {
                    stdl_sdl2_st_c2p(src, t->pitch, dst, r->dp, ge - g, n,
                                     ST_TABS(r, y));
                }
            } else
#endif
            {
                const Uint8 *src = t->pixels + MUL(y - r->bdy, t->pitch) +
                                   (g * 16 - r->bdx);
                Uint8 *dst = back + MUL(y, r->stride) + g * 16;
                if (pair) {
                    r->pairs->conv(src, t->pitch, dst, r->dp, ge - g, n,
                                   r->dbl, r->x2, r->x0);
                } else {
                    stdl_sdl2_c2p(src, t->pitch, dst, r->dp, ge - g, n,
                                  r->dbl, r->v, r->x2, r->x0);
                }
            }
            g = ge;
        }
        if (!p) {
            break;
        }
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
#ifdef STDL_SDL2_ST
    /* a stale page is converted whole at the present, this batch's
     * pixels with it, from the chunky copy every copy went into */
    if (r->stale) {
        drop_batch(r);
        return;
    }
#endif
    /* the partial groups first, the whole ones' bits not among them */
    for (y = r->by0; r->bparts && y <= r->by1; y++) {
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
    r->bparts = 0;
    /* then runs of rows with the same whole groups, one call a run;
     * with pairs in the batch, the same pairs too, their groups through
     * their own loops */
    for (y = r->by0; y <= r->by1; ) {
        const Uint32 m = r->bwhole[y];
        const Uint32 p = r->bpaired ? r->bpair[y] & m : 0;
        int y2 = y + 1, i;
        if (!m) {
            y++;
            continue;
        }
        while (y2 <= r->by1 && r->bwhole[y2] == m &&
               (!r->bpaired || (r->bpair[y2] & m) == p)) {
            y2++;
        }
        convert_rows(r, back, y, y2 - y, m, p);
        for (i = y; i < y2; i++) {
            r->now[i] |= m;
            r->bwhole[i] = 0;
            r->bpair[i] = 0;
        }
        y = y2;
    }
    /* the batch's rows are the rows of now it set */
    NOW_ROWS(r, r->by0, r->by1);
    r->by0 = SCREEN_H;
    r->by1 = -1;
    r->bpaired = 0;
    r->bt = NULL;
}

/* ------------------------------------------------------------------ */
/* drawing                                                             */

/* a clear, in the chunky copy of the screen: all of it colour c while
 * it is kept, and while it is not, no group of it the screen's */
static __attribute__((noinline)) void model_fill(SDL_Renderer *r, int c)
{
    if (r->keep) {
        memset(r->model, c, SCREEN_W * SCREEN_H);
        r->holes = 0;
    } else if (r->model) {
        memset(r->mvalid, 0, sizeof(r->mvalid));
    }
}

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
#ifdef STDL_SDL2_ST
    if (st_on) {
        back = back_page(r);
        st_fill(r, back + ROW0 * r->stride, c);
        if (!r->given) {
            st_mark(r, &c, 1);
        }
        for (y = ROW0; y < ROW0 + ROWS; y++) {
            r->now[y] = ALLGROUPS;
        }
    } else
#endif
    {
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
    }
    NOW_ROWS(r, 0, SCREEN_H - 1);
    model_fill(r, c);
    r->frame = r->drawn = 1;
    return 0;
}

/* a 68000's longs are at even addresses; the host's test of that build
 * refuses as it does */
#ifdef STDL_SDL2_ST
#define LONGS_AT 1
#else
#define LONGS_AT 0
#endif

/*
 * A copy of whole groups, compared: each group of the box is compared
 * with the chunky copy of the screen, and only those that differ go
 * into the copy and into the batch. A group the screen already has
 * needs no conversion on any page: the page drawn on has it or owes
 * it, and what it owes it takes from the page last shown (catch_up).
 * A program that pushes the blocks it drew on, changed or not, pushes
 * many it only drew again as they were (three in four of one game's),
 * and a compare is four longs, some 70 cycles a group on a 68030,
 * against a conversion; on an ST, against a conversion's 750. 0 for a
 * box this does not take (a part of a group, pixel pairs, a 68000's
 * pixels at an odd address), which goes the usual way.
 */
static int copy_changed(SDL_Renderer *r, const SDL_Texture *t, int tx, int ty,
                        int sx, int sy, int w, int h)
{
    const Uint8 *s;
    Uint8 *m;
    long sskip;
    Uint32 rowbits;
    int g0, ng, y;
    if (((sx | w) & 15) ||
#ifdef STDL_SDL2_ST
        /* an ST's screen compares pairs copies too (below) */
        (!st_on && (r->pairing || r->bpaired)) ||
#else
        r->pairing || r->bpaired ||
#endif
        (((unsigned long)t->pixels + (unsigned long)tx) & LONGS_AT) ||
        ((unsigned long)t->pitch & LONGS_AT)) {
        return 0;
    }
#ifdef STDL_SDL2_ST
    /* the rows the page has; the others are never shown or read */
    if (sy < ROW0) {
        h -= ROW0 - sy;
        ty += ROW0 - sy;
        sy = ROW0;
    }
    if (sy + h > ROW0 + ROWS) {
        h = ROW0 + ROWS - sy;
    }
    if (h <= 0) {
        return 1;
    }
    r->bpaired |= r->pairing;
#endif
    g0 = sx >> 4;
    ng = w >> 4;
    rowbits = ((1UL << ng) - 1) << g0;
    s = t->pixels + MUL(ty, t->pitch) + tx;
    m = r->model + MUL(sy, SCREEN_W) + sx;
    sskip = t->pitch - w;
    for (y = sy; y < sy + h; y++) {
        Uint32 bit = 1UL << g0, mask = 0;
        int g = ng;
        if (r->holes && (r->mvalid[y] & rowbits) != rowbits) {
            /* a row with groups the model has not got, as the compare
             * was off when they were copied: all of it converted, and
             * the model has it from here on */
            memcpy(m, s, (size_t)w);
            s += w;
            m += w;
            mask = rowbits;
            r->mvalid[y] |= rowbits;
        } else {
#ifdef __m68k__
            /* a group that differs is copied from the long that did, the
             * ones before it being the same already */
            __asm__ volatile (
                "1:  cmpm.l (%[s])+,(%[m])+\n"
                "    bne.s 2f\n"
                "    cmpm.l (%[s])+,(%[m])+\n"
                "    bne.s 3f\n"
                "    cmpm.l (%[s])+,(%[m])+\n"
                "    bne.s 4f\n"
                "    cmpm.l (%[s])+,(%[m])+\n"
                "    bne.s 5f\n"
                "6:  add.l %[bit],%[bit]\n"
                "    subq.l #1,%[g]\n"
                "    bne.s 1b\n"
                "    bra.s 7f\n"
                "2:  move.l -4(%[s]),-4(%[m])\n"
                "    move.l (%[s])+,(%[m])+\n"
                "    move.l (%[s])+,(%[m])+\n"
                "    move.l (%[s])+,(%[m])+\n"
                "    or.l %[bit],%[mask]\n"
                "    bra.s 6b\n"
                "3:  move.l -4(%[s]),-4(%[m])\n"
                "    move.l (%[s])+,(%[m])+\n"
                "    move.l (%[s])+,(%[m])+\n"
                "    or.l %[bit],%[mask]\n"
                "    bra.s 6b\n"
                "4:  move.l -4(%[s]),-4(%[m])\n"
                "    move.l (%[s])+,(%[m])+\n"
                "    or.l %[bit],%[mask]\n"
                "    bra.s 6b\n"
                "5:  move.l -4(%[s]),-4(%[m])\n"
                "    or.l %[bit],%[mask]\n"
                "    bra.s 6b\n"
                "7:\n"
                : [s] "+a" (s), [m] "+a" (m), [bit] "+d" (bit),
                  [mask] "+d" (mask), [g] "+d" (g)
                : : "cc", "memory");
#else
            do {
                if (memcmp(s, m, 16)) {
                    memcpy(m, s, 16);
                    mask |= bit;
                }
                s += 16;
                m += 16;
                bit += bit;
            } while (--g);
#endif
        }
#ifdef STDL_SDL2_ST
        /* Pixel pairs on an ST's screen, by the batch's rule (c2ppair.c's
         * mark): a pairs copy's changed groups are pairs where no other
         * copy has reached them, and any other copy takes back every
         * group of its box, changed or not, as the rule is about the
         * copies and not the pixels. A group a pairs copy leaves as it
         * was is not converted, and stays as it was marked. */
        if (r->pairing) {
            r->bpair[y] |= mask & ~((r->bwhole[y] & ~r->bpair[y]) |
                                    r->bpart[y]);
        } else if (r->bpaired) {
            r->bpair[y] &= ~rowbits;
        }
#endif
        if (mask) {
#ifdef STDL_SDL2_ST
            /* an ST's colours seen: the changed groups', now the copy's */
            if (st_on && !r->given) {
                const Uint8 *p = m - w;
                Uint32 k = mask >> g0;
                for (; k; k >>= 1, p += 16) {
                    if ((k & 1) && !st_seen16(r->seen, p)) {
                        st_mark(r, p, 16);
                    }
                }
            }
#endif
            r->bwhole[y] |= mask;
            if (y < r->by0) {
                r->by0 = y;
            }
            if (y > r->by1) {
                r->by1 = y;
            }
        }
        s += sskip;
        m += SCREEN_W - w;
    }
    return 1;
}

/* the chunky copy of the screen: the box as the screen should have it */
static __attribute__((noinline)) void model_copy(SDL_Renderer *r,
                                                 const SDL_Texture *t,
                                                 int tx, int ty, int sx,
                                                 int sy, int w, int h)
{
    int row;
    for (row = 0; row < h; row++) {
        memcpy(r->model + (sy + row) * SCREEN_W + sx,
               t->pixels + (ty + row) * t->pitch + tx, (size_t)w);
#ifdef STDL_SDL2_ST
        /* what the page shows of it */
        if (st_on && !r->given && sy + row >= ROW0 &&
            sy + row < ROW0 + ROWS) {
            st_note(r, r->model + MUL(sy + row, SCREEN_W) + sx, w);
        }
#endif
    }
}

/* A copy not given both rectangles, or not inside the texture and the
 * logical size: clipped, then copied as one that is. */
static __attribute__((noinline)) int copy_clipped(SDL_Renderer *r,
                                                  SDL_Texture *texture,
                                                  const SDL_Rect *srcrect,
                                                  const SDL_Rect *dstrect)
{
    SDL_Rect s, d;
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
    return SDL_RenderCopy(r, texture, &s, &d);
}

/* A box already inside the texture and the logical size, as a program
 * that tracks its own changes sends them, costs a few compares and the
 * batch's OR a row; anything else is clipped first (copy_clipped). The
 * compares are unsigned: (unsigned)(w - 1) < W is 1 <= w <= W, and then
 * (unsigned)x <= W - w is 0 <= x <= W - w. */
int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture,
                   const SDL_Rect *srcrect, const SDL_Rect *dstrect)
{
    SDL_Renderer *r = renderer;
    const SDL_Rect *s = srcrect, *d = dstrect;
    long dx, dy;
    int sx, sy, w, h;
    if (!r || !texture || texture->r != r) {
        return SDL_SetError("RenderCopy: no such texture for this renderer");
    }
    if (!s || !d) {
        return copy_clipped(r, texture, srcrect, dstrect);
    }
    w = s->w;
    h = s->h;
    if (d->w != w || d->h != h ||
        (unsigned)(w - 1) >= (unsigned)texture->w ||
        (unsigned)s->x > (unsigned)(texture->w - w) ||
        (unsigned)(h - 1) >= (unsigned)texture->h ||
        (unsigned)s->y > (unsigned)(texture->h - h) ||
        (unsigned)(w - 1) >= (unsigned)r->lw ||
        (unsigned)d->x > (unsigned)(r->lw - w) ||
        (unsigned)(h - 1) >= (unsigned)r->lh ||
        (unsigned)d->y > (unsigned)(r->lh - h)) {
        return copy_clipped(r, texture, srcrect, dstrect);
    }
    sx = d->x + r->xoff;
    sy = d->y + r->yoff;
    dx = sx - s->x;
    dy = sy - s->y;
    if (r->bt && (r->bt != texture || r->bdx != dx || r->bdy != dy)) {
        flush(r);
    }
    r->bt = texture;
    r->bdx = dx;
    r->bdy = dy;
    if (!r->compare || !copy_changed(r, texture, s->x, s->y, sx, sy, w, h)) {
        batch_add(r, sx, sy, w, h);
        if (r->keep) {
            model_copy(r, texture, s->x, s->y, sx, sy, w, h);
        } else if (r->model) {
            /* the chunky copy is not kept (STDL_SDL2_SetCompare): the
             * groups this copy touches are the screen's and no longer
             * its own, and all its others still are. An AND a row, so
             * that a program can leave one copy of a frame uncompared
             * and have the rest of the screen compared as before. */
            const Uint32 left = ~(Uint32)(((2UL << ((sx + w - 1) >> 4)) - 1) &
                                          ~((1UL << (sx >> 4)) - 1));
            Uint32 *v = &r->mvalid[sy];
            do {
                *v++ &= left;
            } while (--h > 0);
        }
    }
    r->frame = r->drawn = 1;
    return 0;
}

int stdl_sdl2_copy_pairs(SDL_Renderer *r, SDL_Texture *texture,
                         const SDL_Rect *srcrect, const SDL_Rect *dstrect,
                         const struct stdl_sdl2_pairs *pairs)
{
    int ret;
#ifdef STDL_SDL2_ST
    /* an ST's screen: pair tables of its own, made from the conversion's
     * at the first pairs copy and again with them (st_reduce); without
     * the memory for them a pairs copy is converted as any */
    if (r && st_on && !r->ptab) {
        r->ptab = (Uint32 *)malloc(sizeof(r->tab));
        if (!r->ptab) {
            return SDL_RenderCopy(r, texture, srcrect, dstrect);
        }
        pairs->st_tabs(r->tab[0], r->ptab);
    }
#endif
    /* no renderer: SDL_RenderCopy's error */
    if (!r) {
        return SDL_RenderCopy(r, texture, srcrect, dstrect);
    }
    r->pairs = pairs;
    r->pairing = 1;
    ret = SDL_RenderCopy(r, texture, srcrect, dstrect);
    r->pairing = 0;
    return ret;
}

/* the groups the page owes and this frame did not draw, from the page
 * last shown, which holds the screen as it stands; a TT's row read
 * from its first line and written to both. The state is in locals and
 * each layout has a loop of its own: the copy is a Falcon's biggest
 * after the conversion, and its stores through d would otherwise have
 * the renderer's fields read again for every long. */
static void catch_up(SDL_Renderer *r, Uint8 *back)
{
    const Uint8 *shown = r->shown;
    const Uint32 *owed = r->owed[r->cur], *now = r->now;
    const long stride = r->stride, odd = r->dp >> 2;
    const int dbl = r->dbl, y1 = r->oy1[r->cur];
    int y = r->oy0[r->cur];
    long off = MUL(y, stride);
    if (!shown || shown == back) {
        return;
    }
    for (; y <= y1; y++, off += stride) {
        Uint32 m = owed[y] & ~now[y];
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
            s = (const Uint32 *)(const void *)(shown + off + GOFF(g));
            d = (Uint32 *)(void *)(back + off + GOFF(g));
            n = GLONGS(ge - g);
            if (dbl) {
                do {
                    d[odd] = *s;
                    *d++ = *s++;
                } while (--n > 0);
            } else {
                do {
                    *d++ = *s++;
                } while (--n > 0);
            }
            g = ge;
        }
    }
}

/* the page about to be shown against the model converted, margins and
 * all */
static void check(SDL_Renderer *r, const Uint8 *back)
{
    const size_t bytes = (size_t)r->stride * ROWS;
    const Uint8 *a = back + ROW0 * r->stride - r->margin;
    size_t i;
    memset(r->scratch, 0, bytes);
#ifdef STDL_SDL2_ST
    if (st_on) {
        stdl_sdl2_st_c2p(r->model + ROW0 * SCREEN_W, SCREEN_W, r->scratch,
                         r->dp, GROUPS, ROWS, ST_TABS(r, ROW0));
    } else
#endif
    stdl_sdl2_c2p_c(r->model, SCREEN_W, r->scratch + r->margin, r->dp,
                    GROUPS, SCREEN_H, r->dbl, r->x2, r->x0);
    r->chk.flips++;
    for (i = 0; i < bytes; i++) {
        if (a[i] != r->scratch[i]) {
            if (!r->chk.bad) {
                r->chk.first_flip = r->chk.flips;
                r->chk.first_row = (int)(i / (size_t)r->stride) + ROW0;
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
    if (!r) {
        return;
    }
#ifdef STDL_SDL2_ST
    if (st_on) {
        /* the maps first: what this frame converts goes through them */
        if (pal_changed(r) || r->fresh) {
            st_reduce(r);
        }
        if (!r->frame) {
            if (r->regdirty) {
                st_registers(r);
            } else if (r->flags & SDL_RENDERER_PRESENTVSYNC) {
                STDL_WaitVBL();
            }
            return;
        }
    } else
#endif
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
#ifdef STDL_SDL2_ST
    if (r->stale) {
        /* the whole page through the new maps; the other pages then owe
         * all of it, and catch up from this one as from any frame */
        int y;
        stdl_sdl2_st_c2p(r->model + ROW0 * SCREEN_W, SCREEN_W,
                         back + ROW0 * r->stride, r->dp, GROUPS, ROWS,
                         ST_TABS(r, ROW0));
        for (y = ROW0; y < ROW0 + ROWS; y++) {
            r->now[y] = ALLGROUPS;
        }
        NOW_ROWS(r, ROW0, ROW0 + ROWS - 1);
        r->stale = 0;
    } else
#endif
    catch_up(r, back);
    frame_rows(r);
    if (CHECKED(r)) {
        check(r, back);
    }
#ifdef STDL_SDL2_ST
    if (st_on) {
        if (r->regdirty) {
            st_registers(r);
        }
    } else
#endif
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
#ifdef STDL_SDL2_ST
    /* colour X as the maps have it now, which is what the chunky copy
     * will say is there; a page cleared so is not stale */
    if (st_on) {
        const Uint8 x = (Uint8)r->x;
        if (r->shown) {
            Uint8 *pix = (Uint8 *)r->shown + ROW0 * r->stride;
            shown = page_index(r, pix);
            st_fill(r, pix, r->x);
        }
        if (!r->given) {
            st_mark(r, &x, 1);
        }
        r->stale = 0;
    } else
#endif
    if (r->shown) {
        shown = page_index(r, (Uint8 *)r->shown);
        memset((Uint8 *)r->shown - r->margin, 0, (size_t)r->stride * SCREEN_H);
    }
    for (p = 0; p < r->npages; p++) {
        r->clear[p] = p != shown;
    }
    no_rows(r);
    model_fill(r, (int)r->x);
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
        *base = renderer->shown ? renderer->shown +
                                  ROW0 * renderer->stride - renderer->margin
                                : NULL;
    }
    if (bytes) {
        *bytes = renderer->shown ? renderer->stride * ROWS : 0;
    }
    return 0;
}

int STDL_SDL2_GetCheck(SDL_Renderer *renderer, STDL_SDL2_Check *check)
{
    if (!renderer || !check) {
        return SDL_InvalidParamError("renderer");
    }
    if (!CHECKED(renderer)) {
        return SDL_SetError("The screen check is off (STDL_SDL2_HINT_CHECK)");
    }
    *check = renderer->chk;
    return 0;
}

/*
 * The compare on or off from here on (STDL_SDL2_HINT_COMPARE). Off, the
 * chunky copy of the screen is no longer kept, unless the check keeps
 * it, and copies cost what they do without one but for an AND a row:
 * each takes the groups it touches out of those the copy has (mvalid),
 * and a clear takes them all. On again, the copy has every group no
 * copy or clear reached meanwhile, so those are compared as if it had
 * never been off: a program turns it off for the one copy of a frame
 * that is all new pixels and on for the rest. A copy reaching a group
 * it lacks is converted whole on that row and the copy has it from
 * then on; a clear of the screen while kept fills it again. On for a
 * renderer made without the hint, the copy is allocated now, all of it
 * groups the copy has not got.
 */
int STDL_SDL2_SetCompare(SDL_Renderer *renderer, int on)
{
    SDL_Renderer *r = renderer;
    if (!r) {
        return SDL_InvalidParamError("renderer");
    }
#ifdef STDL_SDL2_ST
    if (st_on) {
        return 0;               /* an ST's screen always compares */
    }
#endif
    if (!on) {
        r->compare = 0;
        if (r->keep && !CHECKED(r)) {
            /* mvalid is read from here on: every group, unless some
             * were missing already */
            if (!r->holes) {
                memset(r->mvalid, 0xFF, sizeof(r->mvalid));
                r->holes = 1;
            }
            r->keep = 0;
        }
        return 0;
    }
    if (!r->model) {
        r->model = (Uint8 *)malloc(SCREEN_W * SCREEN_H);
        if (!r->model) {
            return SDL_OutOfMemory();
        }
        memset(r->mvalid, 0, sizeof(r->mvalid));
        r->holes = 1;
    }
    r->keep = 1;
    r->compare = 1;
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
    SDL_Texture *t;
#ifdef STDL_SDL2_ST
    /* MUL's 16 bits */
    if (pitch > 0x7FFF || h > 0x7FFF) {
        SDL_SetError("A texture of %d rows, %d bytes a row: 32,767 at most"
                     " in this build", h, pitch);
        return NULL;
    }
#endif
    t = (SDL_Texture *)calloc(1, sizeof(*t));
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
