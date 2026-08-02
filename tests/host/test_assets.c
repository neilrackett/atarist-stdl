/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
/* Asset loaders end-to-end: the tracked example assets load, the
 * colour key survives at index 15, and keyed blits respect it. */
#include <stdio.h>
#include <stdl/stdl.h>

static int failures;
#define CHECK(c, ...) do { if (!(c)) { failures++; \
    printf("FAIL %d: ", __LINE__); printf(__VA_ARGS__); \
    printf("\n"); } } while (0)

int main(void)
{
    STDL_Surface *sail, *icon, *dst;
    uint8_t key;
    STDL_Rect d;

    sail = STDL_LoadBMP("../../examples/assets/SAIL.BMP");
    CHECK(sail != NULL, "SAIL.BMP load: %s", STDL_GetError());
    if (sail == NULL) {
        return 1;
    }
    CHECK(sail->w == 147 && sail->h == 105, "SAIL dimensions");

    /* the magenta colour key must sit at index 15 exactly
     * (stdlconv bmp16 --keycolor pins it there) */
    key = STDL_MapRGB(sail->format, 0xFF, 0x00, 0xFF);
    CHECK(key == 15, "key index %d, want 15", key);
    CHECK(STDL_GetPixel(sail, 0, 0) == 15, "corner is key colour");
    STDL_SetColourKey(sail, 1, key);

    /* keyed blit: background shows through where the key was */
    dst = STDL_CreateSurface(160, 120);
    STDL_FillRect(dst, NULL, 6);
    d.x = 0; d.y = 0;
    STDL_BlitSurface(sail, NULL, dst, &d);
    CHECK(STDL_GetPixel(dst, 0, 0) == 6, "key pixel preserved dst");
    CHECK(STDL_SurfaceIsOpaque(sail) == 0, "sail has transparency");

    icon = STDL_LoadBMP("../../examples/assets/ICON.BMP");
    CHECK(icon != NULL, "ICON.BMP load: %s", STDL_GetError());
    if (icon != NULL) {
        CHECK(icon->w == 32 && icon->h == 32, "ICON dimensions");
        STDL_FreeSurface(icon);
    }

    STDL_FreeSurface(sail);
    STDL_FreeSurface(dst);
    if (failures == 0) {
        printf("all asset tests passed\n");
        return 0;
    }
    printf("%d failure(s)\n", failures);
    return 1;
}
