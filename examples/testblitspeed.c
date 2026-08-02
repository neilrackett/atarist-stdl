/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: CC0-1.0
 *
 * STDL port of a public-domain SDL 1.2 test program; the port is
 * dedicated to the public domain like the original.
 */
/*
 * Benchmarks surface-to-surface blits in various formats.
 *
 *  Written by Ryan C. Gordon.
 *
 * SDL 1.2 test program (public domain), ported to STDL.
 * Port notes:
 *  - default surfaces sized for the ST (dest 320x200, src 160x100);
 *    bpp/mask options are accepted and ignored (everything is 4bpp
 *    planar)
 *  - SAMPLE.BMP is the stock sample.bmp pre-quantised by stdlconv
 *  - default test time cut to 5 seconds
 * This is the number that answers "how much of the blit path can
 * stay in C" - run it on a plain ST and a Mega STE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "SDL.h"

static SDL_Surface *dest = NULL;
static SDL_Surface *src = NULL;
static int testSeconds = 5;

static int percent(int val, int total)
{
    return((int) ((((float) val) / ((float) total)) * 100.0f));
}

static int randRange(int lo, int hi)
{
    return(lo + (int) (((double) hi)*rand()/(RAND_MAX+1.0)));
}

static void copy_trunc_str(char *str, size_t strsize, const char *flagstr)
{
    if ( (strlen(str) + strlen(flagstr)) >= (strsize - 1) )
        strcpy(str + (strsize - 5), " ...");
    else
        strcat(str, flagstr);
}

static void __append_sdl_surface_flag(SDL_Surface *_surface, char *str,
                                      size_t strsize, Uint32 flag,
                                      const char *flagstr)
{
    if (_surface->flags & flag)
        copy_trunc_str(str, strsize, flagstr);
}


#define append_sdl_surface_flag(a, b, c, fl) __append_sdl_surface_flag(a, b, c, fl, " " #fl)
#define print_tf_state(str, val) printf("%s: {%s}\n", str, (val) ? "true" : "false" )

static void output_videoinfo_details(void)
{
    const SDL_VideoInfo *info = SDL_GetVideoInfo();
    printf("SDL_GetVideoInfo():\n");
    if (info == NULL)
        printf("  (null.)\n");
    else
    {
        print_tf_state("  hardware surface available", info->hw_available);
        print_tf_state("  accelerated hardware->hardware blits", info->blit_hw);
        print_tf_state("  accelerated hardware->hardware colorkey blits", info->blit_hw_CC);
        print_tf_state("  accelerated color fills", info->blit_fill);
        printf("  video memory: (%d)\n", (int)info->video_mem);
    }

    printf("\n");
}

static void output_surface_details(const char *name, SDL_Surface *surface)
{
    printf("Details for %s:\n", name);

    if (surface == NULL)
    {
        printf("-WARNING- You've got a NULL surface!");
    }
    else
    {
        char f[256];
        printf("  width      : %d\n", surface->w);
        printf("  height     : %d\n", surface->h);
        printf("  depth      : %d bits per pixel\n", surface->format->BitsPerPixel);
        printf("  pitch      : %d\n", (int) surface->pitch);

        f[0] = '\0';

        if ((surface->flags & SDL_HWSURFACE) == 0)
            copy_trunc_str(f, sizeof (f), " SDL_SWSURFACE");

        append_sdl_surface_flag(surface, f, sizeof (f), SDL_HWSURFACE);
        append_sdl_surface_flag(surface, f, sizeof (f), SDL_HWPALETTE);
        append_sdl_surface_flag(surface, f, sizeof (f), SDL_DOUBLEBUF);
        append_sdl_surface_flag(surface, f, sizeof (f), SDL_FULLSCREEN);
        append_sdl_surface_flag(surface, f, sizeof (f), SDL_HWACCEL);
        append_sdl_surface_flag(surface, f, sizeof (f), SDL_SRCCOLORKEY);
        append_sdl_surface_flag(surface, f, sizeof (f), SDL_RLEACCEL);

        if (f[0] == '\0')
            strcpy(f, " (none)");

        printf("  flags      :%s\n", f);
    }

    printf("\n");
}

static void output_details(void)
{
    output_videoinfo_details();
    output_surface_details("Source Surface", src);
    output_surface_details("Destination Surface", dest);
}

static Uint32 blit(SDL_Surface *dst, SDL_Surface *srcs, int x, int y)
{
    Uint32 start = 0;
    SDL_Rect srcRect;
    SDL_Rect dstRect;

    srcRect.x = 0;
    srcRect.y = 0;
    dstRect.x = x;
    dstRect.y = y;
    dstRect.w = srcRect.w = srcs->w;  /* SDL will clip as appropriate. */
    dstRect.h = srcRect.h = srcs->h;

    start = SDL_GetTicks();
    SDL_BlitSurface(srcs, &srcRect, dst, &dstRect);
    return(SDL_GetTicks() - start);
}

static void blitCentered(SDL_Surface *dst, SDL_Surface *srcs)
{
    int x = (dst->w - srcs->w) / 2;
    int y = (dst->h - srcs->h) / 2;
    blit(dst, srcs, x, y);
}

static int setup_test(int argc, char **argv)
{
    Uint32 dstflags = 0;
    int dstw = 320;
    int dsth = 200;
    Uint32 srcflags = 0;
    int srcw = 160;
    int srch = 100;
    int screenSurface = 0;
    SDL_Surface *bmp = NULL;
    int i = 0;

    for (i = 1; i < argc; i++)
    {
        const char *arg = argv[i];

        if (strcmp(arg, "--dstwidth") == 0)
            dstw = atoi(argv[++i]);
        else if (strcmp(arg, "--dstheight") == 0)
            dsth = atoi(argv[++i]);
        else if (strcmp(arg, "--srcwidth") == 0)
            srcw = atoi(argv[++i]);
        else if (strcmp(arg, "--srcheight") == 0)
            srch = atoi(argv[++i]);
        else if (strcmp(arg, "--seconds") == 0)
            testSeconds = atoi(argv[++i]);
        else if (strcmp(arg, "--screen") == 0)
            screenSurface = 1;
    }

    if (SDL_Init(SDL_INIT_VIDEO) == -1)
    {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return(0);
    }

    bmp = SDL_LoadBMP("SAMPLE.BMP");
    if (bmp == NULL)
    {
        fprintf(stderr, "SDL_LoadBMP failed: %s\n", SDL_GetError());
        SDL_Quit();
        return(0);
    }

    /* the screen is always available on the ST; use it as the
     * destination so the benchmark is visible */
    if (screenSurface)
        dest = SDL_SetVideoMode(dstw, dsth, 4, dstflags);
    else
    {
        (void)SDL_SetVideoMode(320, 200, 4, 0);
        dest = SDL_CreateRGBSurface(dstflags, dstw, dsth, 4, 0, 0, 0, 0);
    }

    if (dest == NULL)
    {
        fprintf(stderr, "dest surface creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return(0);
    }

    src = SDL_CreateRGBSurface(srcflags, srcw, srch, 4, 0, 0, 0, 0);
    if (src == NULL)
    {
        fprintf(stderr, "src surface creation failed: %s\n", SDL_GetError());
        SDL_Quit();
        return(0);
    }

    /* set some sane defaults so we can see if the blit code is broken... */
    SDL_FillRect(dest, NULL, SDL_MapRGB(dest->format, 0, 0, 0));
    SDL_FillRect(src, NULL, SDL_MapRGB(src->format, 0, 0, 0));

    blitCentered(src, bmp);
    SDL_FreeSurface(bmp);

    output_details();

    return(1);
}


static void test_blit_speed(void)
{
    Uint32 clearColor = SDL_MapRGB(dest->format, 0, 0, 0);
    Uint32 iterations = 0;
    Uint32 elasped = 0;
    Uint32 end = 0;
    Uint32 now = 0;
    Uint32 last = 0;
    int testms = testSeconds * 1000;
    int wmax = (dest->w - src->w);
    int hmax = (dest->h - src->h);
    int isScreen = (SDL_GetVideoSurface() == dest);
    SDL_Event event;

    printf("Testing blit speed for %d seconds...\n", testSeconds);

    now = SDL_GetTicks();
    end = now + testms;

    do
    {
        /* pump the event queue occasionally to keep OS happy... */
        if (now - last > 1000)
        {
            last = now;
            while (SDL_PollEvent(&event)) { /* no-op. */ }
        }

        iterations++;
        elasped += blit(dest, src, randRange(0, wmax), randRange(0, hmax));
        if (isScreen)
        {
            SDL_Flip(dest);  /* show it! */
            SDL_FillRect(dest, NULL, clearColor); /* blank it for next time! */
        }

        now = SDL_GetTicks();
    } while (now < end);

    printf("Non-blitting crap accounted for %d percent of this run.\n",
            percent(testms - elasped, testms));

    if (elasped == 0)
        elasped = 1;
    printf("%d blits took %d ms (%d fps).\n",
            (int) iterations,
            (int) elasped,
            (int) (((float)iterations) / (((float)elasped) / 1000.0f)));
}

int main(int argc, char **argv)
{
    int initialized = setup_test(argc, argv);
    if (initialized)
    {
        test_blit_speed();
        SDL_Quit();
    }
    return(!initialized);
}

/* end of testblitspeed.c ... */
