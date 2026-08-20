/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/* Composition semantics: origin, transparent fill, dst-mask
 * maintenance, opacity cache, PutGroup, Degas loading. */
#include <stdio.h>
#include <string.h>
#include <stdl/stdl.h>

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static int mask_bit(STDL_Surface *s, int x, int y)
{
    uint16_t *m = (uint16_t *)(s->mask + (uint32_t)y * s->maskstride);
    return (m[x >> 4] >> (15 - (x & 15))) & 1;
}

int main(void)
{
    /* --- origin (ybias-style camera) --------------------------- */
    {
        /* 64x16 stripe standing in for a level; camera at y=100 */
        STDL_Surface *level = STDL_CreateSurface(64, 16);
        STDL_Surface *spr = STDL_CreateSurface(16, 4);
        STDL_Rect r, d;

        STDL_SetSurfaceOrigin(level, 0, 100);
        r.x = 4; r.y = 102; r.w = 20; r.h = 6;      /* level coords */
        STDL_FillRect(level, &r, 5);
        CHECK(STDL_GetPixel(level, 4, 2) == 5, "origin fill start");
        CHECK(STDL_GetPixel(level, 23, 7) == 5, "origin fill end");
        CHECK(STDL_GetPixel(level, 3, 2) == 0, "origin fill left edge");

        STDL_FillRect(spr, NULL, 9);
        d.x = 30; d.y = 105; d.w = 16; d.h = 4;     /* level coords */
        STDL_BlitSurface(spr, NULL, level, &d);
        CHECK(STDL_GetPixel(level, 30, 5) == 9, "origin blit");
        CHECK(d.y == 105, "writeback in logical coords, got %d", d.y);

        /* level as blit SOURCE with origin: copy level rect at
         * level coords onto a plain surface */
        {
            STDL_Surface *view = STDL_CreateSurface(64, 16);
            STDL_Rect sr = { 4, 102, 8, 4 }, dr = { 0, 0, 0, 0 };
            STDL_BlitSurface(level, &sr, view, &dr);
            CHECK(STDL_GetPixel(view, 0, 0) == 5, "origin as source");
            STDL_FreeSurface(view);
        }
        STDL_FreeSurface(level);
        STDL_FreeSurface(spr);
    }

    /* --- transparent fill + mask maintenance ------------------- */
    {
        STDL_Surface *s = STDL_CreateSurface(48, 16);
        STDL_Rect r;

        STDL_FillRect(s, NULL, 7);
        CHECK(STDL_CreateMask(s, 0) == 0, "create opaque mask");
        CHECK(STDL_SurfaceIsOpaque(s) == 1, "opaque after create");

        r.x = 8; r.y = 4; r.w = 20; r.h = 8;
        STDL_FillRect(s, &r, STDL_TRANSPARENT);
        CHECK(mask_bit(s, 8, 4) == 1, "hole transparent");
        CHECK(mask_bit(s, 27, 11) == 1, "hole end transparent");
        CHECK(mask_bit(s, 7, 4) == 0, "outside hole opaque");
        CHECK(STDL_SurfaceIsOpaque(s) == 0, "not opaque with hole");
        CHECK(STDL_GetPixel(s, 10, 5) == 0, "hole pixels cleared");

        /* refill part of the hole with a colour: opaque again */
        r.x = 8; r.y = 4; r.w = 4; r.h = 4;
        STDL_FillRect(s, &r, 3);
        CHECK(mask_bit(s, 9, 5) == 0, "refilled opaque");
        CHECK(mask_bit(s, 14, 5) == 1, "rest of hole still open");

        /* masked blit honours the hole */
        {
            STDL_Surface *dst = STDL_CreateSurface(48, 16);
            STDL_FillRect(dst, NULL, 1);
            STDL_BlitSurface(s, NULL, dst, NULL);
            CHECK(STDL_GetPixel(dst, 20, 6) == 1, "hole preserved dst");
            CHECK(STDL_GetPixel(dst, 0, 0) == 7, "solid part blitted");
            CHECK(STDL_GetPixel(dst, 9, 5) == 3, "refilled part blitted");
            STDL_FreeSurface(dst);
        }
        STDL_FreeSurface(s);
    }

    /* --- dst-mask maintenance by blits ------------------------- */
    {
        STDL_Surface *bg = STDL_CreateSurface(64, 8);   /* keyed src */
        STDL_Surface *dst = STDL_CreateSurface(64, 8);
        int x, y;

        /* src: colour 2 with a keyed stripe */
        STDL_FillRect(bg, NULL, 2);
        for (y = 0; y < 8; y++)
            for (x = 20; x < 30; x++)
                STDL_PutPixel(bg, x, y, 6);
        STDL_SetColourKey(bg, 1, 6);

        CHECK(STDL_CreateMask(dst, 1) == 0, "dst all-transparent");
        /* unaligned masked blit: dst becomes opaque exactly where
         * src pixels landed */
        {
            STDL_Rect d = { 3, 0, 0, 0 };
            STDL_BlitSurface(bg, NULL, dst, &d);
        }
        CHECK(mask_bit(dst, 3, 2) == 0, "blitted -> opaque");
        CHECK(mask_bit(dst, 25, 2) == 1, "keyed stripe -> still transparent");
        CHECK(mask_bit(dst, 0, 0) == 1, "left of blit untouched");
        CHECK(STDL_SurfaceIsOpaque(dst) == 0, "cache invalidated");

        /* aligned unmasked blit clears dst mask over the rect */
        {
            STDL_Surface *solid = STDL_CreateSurface(32, 8);
            STDL_Rect d = { 16, 0, 0, 0 };
            STDL_FillRect(solid, NULL, 4);
            STDL_BlitSurface(solid, NULL, dst, &d);
            CHECK(mask_bit(dst, 25, 2) == 0, "solid blit -> opaque");
            STDL_FreeSurface(solid);
        }
        STDL_FreeSurface(bg);
        STDL_FreeSurface(dst);
    }

    /* --- PutGroup decoder path --------------------------------- */
    {
        STDL_Surface *s = STDL_CreateSurface(32, 4);
        uint16_t planes[4] = { 0xF00F, 0x0FF0, 0x00FF, 0xFF00 };
        CHECK(STDL_CreateMask(s, 1) == 0, "mask for putgroup");
        STDL_PutGroup(s, 16, 1, planes, 0x000F);
        CHECK(STDL_GetPixel(s, 16, 1) == 9, "putgroup pixel 0");  /* p0+p3 */
        CHECK(STDL_GetPixel(s, 20, 1) == 2+8, "putgroup pixel 4");
        CHECK(mask_bit(s, 31, 1) == 1, "putgroup mask low bits");
        CHECK(mask_bit(s, 16, 1) == 0, "putgroup mask high bits");
        STDL_FreeSurface(s);
    }

    /* --- PutGroup8 half-group decoder path --------------------- */
    {
        STDL_Surface *s = STDL_CreateSurface(32, 4);
        uint8_t lo[4] = { 0xF0, 0x0F, 0x00, 0xFF };
        uint8_t hi[4] = { 0x0F, 0xF0, 0xFF, 0x00 };

        CHECK(STDL_CreateMask(s, 1) == 0, "mask for putgroup8");
        /* both halves of group 1: pixels 16-23, then 24-31 */
        STDL_PutGroup8(s, 16, 2, lo, 0x03);
        STDL_PutGroup8(s, 24, 2, hi, 0xC0);
        /* x is rounded down to the half-group it falls in */
        STDL_PutGroup8(s, 29, 3, hi, 0xC0);
        CHECK(STDL_GetPixel(s, 24, 3) == 2 + 4, "putgroup8 rounds x down");

        CHECK(STDL_GetPixel(s, 16, 2) == 1 + 8, "putgroup8 low pixel");
        CHECK(STDL_GetPixel(s, 20, 2) == 2 + 8, "putgroup8 low pixel 4");
        CHECK(STDL_GetPixel(s, 24, 2) == 2 + 4, "putgroup8 high pixel");
        CHECK(STDL_GetPixel(s, 28, 2) == 1 + 4, "putgroup8 high pixel 4");

        /* the mask byte follows the same even/odd split, and only
         * the written halves change */
        CHECK(mask_bit(s, 22, 2) == 1 && mask_bit(s, 21, 2) == 0,
              "putgroup8 low mask");
        CHECK(mask_bit(s, 24, 2) == 1 && mask_bit(s, 26, 2) == 0,
              "putgroup8 high mask");
        CHECK(mask_bit(s, 0, 2) == 1, "other groups untouched");
        CHECK(s->opaque_state == 0, "putgroup8 invalidates cache");

        /* out of range is a no-op, not a write */
        STDL_PutGroup8(s, 32, 2, lo, 0);
        STDL_PutGroup8(s, 0, 4, lo, 0);
        STDL_PutGroup8(s, -8, 0, lo, 0);
        CHECK(STDL_GetPixel(s, 0, 0) == 0, "putgroup8 clipped");
        STDL_FreeSurface(s);
    }

    /* --- colour key 16: keep a decode-time mask, do not scan --- */
    {
        STDL_Surface *s = STDL_CreateSurface(32, 4);
        uint8_t planes[4] = { 0xFF, 0x00, 0x00, 0x00 };
        STDL_Surface *dst = STDL_CreateSurface(32, 4);
        STDL_Rect d = { 0, 0, 0, 0 };

        /* a decoder builds pixels and mask together: colour 1
         * everywhere, right half of the first group transparent */
        CHECK(STDL_CreateMask(s, 0) == 0, "mask for key 16");
        STDL_PutGroup8(s, 0, 1, planes, 0x00);
        STDL_PutGroup8(s, 8, 1, planes, 0xFF);

        CHECK(STDL_SetColourKey(s, 1, STDL_TRANSPARENT) == 0,
              "set key 16");
        CHECK((s->flags & STDL_SRCKEY) != 0, "key 16 enables SRCKEY");
        CHECK(s->colourkey == STDL_TRANSPARENT, "key 16 stored");
        CHECK(mask_bit(s, 0, 1) == 0 && mask_bit(s, 8, 1) == 1,
              "key 16 preserved the hand-built mask");

        /* a scan for key 0 would have made everything but colour 1
         * transparent; check the blit honours the decoder's mask */
        STDL_FillRect(dst, NULL, 5);
        STDL_BlitSurface(s, NULL, dst, &d);
        CHECK(STDL_GetPixel(dst, 0, 1) == 1, "opaque half blitted");
        CHECK(STDL_GetPixel(dst, 8, 1) == 5, "transparent half kept");
        CHECK(STDL_GetPixel(dst, 0, 0) == 0, "opaque rows overwrite");

        /* enabling it on a maskless surface gives an all-opaque mask */
        {
            STDL_Surface *m = STDL_CreateSurface(32, 4);
            CHECK(STDL_SetColourKey(m, 1, 16) == 0, "key 16 no mask");
            CHECK(m->mask != NULL, "key 16 allocated a mask");
            CHECK(STDL_SurfaceIsOpaque(m) == 1, "allocated mask is opaque");
            STDL_FreeSurface(m);
        }
        STDL_FreeSurface(s);
        STDL_FreeSurface(dst);
    }

    /* --- Degas round trip -------------------------------------- */
    {
        STDL_Surface *pic =
            STDL_LoadDegas("../../examples/assets/SPLASH.PI1", NULL);
        CHECK(pic != NULL, "degas load: %s", STDL_GetError());
        if (pic) {
            CHECK(pic->w == 320 && pic->h == 200, "degas size");
            CHECK(pic->format->palette->colors[0].r % 17 == 0,
                  "degas palette expanded");
            STDL_FreeSurface(pic);
        }
    }

    /* --- case-insensitive open --------------------------------- */
    {
        STDL_Surface *pic = STDL_LoadDegas(
            "../../examples/assets/splash.pi1", NULL);
        /* macOS FS is case-insensitive so this passes trivially
         * there; the real test is on GEMDOS, but at least exercise
         * the code path */
        CHECK(pic != NULL, "ci open");
        STDL_FreeSurface(pic);
    }

    /* --- BlitSurfaceEx: UNDER and MARK -------------------------- */
    {
        /* dst carries a foreground plane in its mask; src is opaque */
        STDL_Surface *src = STDL_CreateSurface(32, 4);
        STDL_Surface *dst = STDL_CreateSurface(32, 4);
        CHECK(src != NULL && dst != NULL, "ex surfaces");
        if (src != NULL && dst != NULL) {
            STDL_Rect r;
            int x;
            CHECK(STDL_CreateMask(dst, 0) == 0, "ex dst mask");
            r.x = 0; r.y = 0; r.w = 32; r.h = 4;
            STDL_FillRect(src, &r, 5);
            STDL_FillRect(dst, &r, 2);

            /* mark the left half of row 0 as foreground */
            ((uint16_t *)dst->mask)[0] = 0xFFFFu;
            ((uint16_t *)dst->mask)[1] = 0x0000u;

            r.x = 0; r.y = 0;
            STDL_BlitSurfaceEx(src, NULL, dst, &r, STDL_BLIT_UNDER);
            for (x = 0; x < 16; x++) {
                CHECK(STDL_GetPixel(dst, x, 0) == 2,
                      "UNDER preserved x=%d", x);
            }
            for (x = 16; x < 32; x++) {
                CHECK(STDL_GetPixel(dst, x, 0) == 5,
                      "UNDER drew x=%d", x);
            }

            /* MARK sets the mask under what it draws; the default
             * clears it, which is what plain BlitSurface does */
            STDL_FillRect(dst, &r, 2);
            memset(dst->mask, 0, (size_t)dst->maskstride * 4);
            r.x = 0; r.y = 0;
            STDL_BlitSurfaceEx(src, NULL, dst, &r, STDL_BLIT_MARK);
            CHECK(((uint16_t *)dst->mask)[0] == 0xFFFFu, "MARK set");
            CHECK(STDL_GetPixel(dst, 0, 0) == 5, "MARK drew");

            memset(dst->mask, 0xFF, (size_t)dst->maskstride * 4);
            r.x = 0; r.y = 0;
            STDL_BlitSurfaceEx(src, NULL, dst, &r, 0);
            CHECK(((uint16_t *)dst->mask)[0] == 0x0000u,
                  "default clears");

            /* flags == 0 must be indistinguishable from the old call */
            {
                STDL_Surface *a = STDL_CreateSurface(48, 5);
                STDL_Surface *b = STDL_CreateSurface(48, 5);
                STDL_Rect ra;
                CHECK(a != NULL && b != NULL, "compat surfaces");
                if (a != NULL && b != NULL) {
                    ra.x = 0; ra.y = 0; ra.w = 48; ra.h = 5;
                    STDL_FillRect(a, &ra, 9);
                    STDL_FillRect(b, &ra, 9);
                    ra.x = 3; ra.y = 1;      /* unaligned: shift chain */
                    STDL_BlitSurface(src, NULL, a, &ra);
                    ra.x = 3; ra.y = 1;
                    STDL_BlitSurfaceEx(src, NULL, b, &ra, 0);
                    CHECK(memcmp(a->pixels, b->pixels,
                                 (size_t)a->stride * 5) == 0,
                          "flags==0 matches BlitSurface");
                }
                STDL_FreeSurface(a);
                STDL_FreeSurface(b);
            }
        }
        STDL_FreeSurface(src);
        STDL_FreeSurface(dst);
    }

    if (!failures) printf("all composition tests passed\n");
    return failures != 0;
}
