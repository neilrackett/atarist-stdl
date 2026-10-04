/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * STDL_BlitSprites: one sprite drawn at many places in one call.
 *
 * A pre-shifted 16x16 sprite draws its 32 groups in about 5000
 * cycles, and STDL_BlitSprite spends about 2000 more on being a call:
 * the checks, the frame's offset, the clip worked out group by group,
 * the BLiTTER's question, the plane dispatch. A scene of fourteen bobs
 * pays that fourteen times for the same sprite on the same surface.
 * Here what does not change between them is worked out once, and
 * each one wholly inside the clip goes straight to the row loop
 * STDL_BlitSprite uses (stdl_spr_rows4). Anything else - a sprite cut
 * by the clip, an unshifted one at an odd x, a plane budget of 2, a
 * machine whose BLiTTER the sprite path would use - is handed to
 * STDL_BlitSprite, so the pixels are the same whichever way it goes.
 *
 * Its own translation unit: a program that draws one sprite at a time
 * does not carry it.
 */

#include "stdl_internal.h"

void STDL_BlitSprites(STDL_Sprite *spr, const STDL_SpriteAt *at,
                      int count, STDL_Surface *dst)
{
    int i, fast, groups, h, stride, cx1, cx2, cy1, cy2;

    if (spr == NULL || at == NULL || dst == NULL || count <= 0) {
        return;
    }
    /* the direct route covers what STDL_BlitSprite's own aligned
     * four-plane loop covers, on the CPU */
    fast = stdl_planes > 2 && !stdl_blitter_active();
    groups = spr->groups;
    h = spr->h;
    stride = dst->stride;
    cx1 = dst->clip.x;
    cx2 = dst->clip.x + dst->clip.w;
    cy1 = dst->clip.y;
    cy2 = dst->clip.y + dst->clip.h;

    for (i = 0; i < count; i++) {
        const int x = at[i].x, y = at[i].y, frame = at[i].frame;
        const int phase = x & 15, xb = x - phase;
        uint32_t idx;

        /* wholly inside, and drawn by the aligned loop: a pre-shifted
         * variant, or an unshifted sprite on a group edge */
        if (!fast || frame >= spr->nframes
            || (spr->nvariants != 16 && phase != 0)
            || xb < cx1 || xb + (groups << 4) > cx2
            || y < cy1 || y + h > cy2) {
            STDL_BlitSprite(spr, frame, dst, x, y);
            continue;
        }
        idx = (uint32_t)frame;
        if (spr->nvariants == 16) {
            idx += stdl_row_off(phase, spr->nframes);
        }
        if ((spr->framesize | idx) > 0xFFFFu) {
            STDL_BlitSprite(spr, frame, dst, x, y);     /* a huge sheet */
            continue;
        }
        stdl_spr_rows4(spr->data
                           + stdl_row_off((int)idx, (uint16_t)spr->framesize),
                       dst->pixels + stdl_row_off(y, (uint16_t)stride)
                           + (xb >> 4) * 8,
                       groups, h, 0, stride - (groups << 3));
    }
}
