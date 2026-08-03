/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Internal shared state. The whole library runs in supervisor mode
 * (entered in STDL_Init) for direct access to the 200Hz counter,
 * palette registers and the blitter without trap overhead.
 */

#ifndef STDL_INTERNAL_H
#define STDL_INTERNAL_H

#include <stddef.h>
#include <stdl/stdl.h>

#ifdef __m68k__
#define STDL_HZ200      (*(volatile uint32_t *)0x4BAUL)
#define STDL_FRCLOCK    (*(volatile uint32_t *)0x466UL)
#define STDL_HWPAL      ((volatile uint16_t *)0xFFFF8240UL)
#else
/* host-test builds: the "registers" are plain memory provided by
 * tests/host/stubs.c, so every module compiles natively */
extern volatile uint32_t stdl_host_clock;
extern volatile uint16_t stdl_host_hwpal[16];
#define STDL_HZ200      stdl_host_clock
#define STDL_FRCLOCK    stdl_host_clock
#define STDL_HWPAL      stdl_host_hwpal
#endif

/*
 * Byte offset of a half-group inside a plane (or mask) word: on the
 * 68000 pixels 0-7 are the word's most significant byte, i.e. byte
 * 0. Host-test builds on a little-endian machine see the two bytes
 * the other way round; the pixel layout itself is unchanged.
 * half is 0 for pixels 0-7 of the group, 1 for pixels 8-15.
 */
#if defined(__m68k__) || (defined(__BYTE_ORDER__) \
                          && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define STDL_WORD_BYTE(half) (half)
#else
#define STDL_WORD_BYTE(half) (1 - (half))
#endif

/*
 * Pack two adjacent plane words into one long so that `first` lands
 * at the lower address - i.e. what a single long store writes on the
 * 68000. Host-test builds may be little-endian, where the halves
 * have to be swapped to produce the same bytes.
 */
#if defined(__m68k__) || (defined(__BYTE_ORDER__) \
                          && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__)
#define STDL_PACK2(first, second) \
    (((uint32_t)(first) << 16) | (uint32_t)(second))
#else
#define STDL_PACK2(first, second) \
    (((uint32_t)(second) << 16) | (uint32_t)(first))
#endif

#define STDL_SCREEN_W       320
#define STDL_SCREEN_H       200
#define STDL_SCREEN_PLANES  4
#define STDL_SCREEN_STRIDE  160
#define STDL_SCREEN_BYTES   32000

typedef struct {
    int              initialised;
    int              video_set;
    STDL_MachineInfo mach;

    /* screen pages: page[0] = TOS screen, page[1] = allocated */
    uint8_t         *page[2];
    void            *page1_alloc;
    int              backpage;    /* page currently drawn into */
    int              doublebuf;

    /* state saved for restore on quit */
    long             old_ssp;
    int              old_rez;
    uint16_t         old_palette[16];
    int              old_cpuspeed;

    /* logical palette for the screen */
    STDL_Colour      colours[16];
} stdl_state_t;

extern stdl_state_t stdl;
extern STDL_Surface stdl_screen;

void stdl_palette_apply_hw(void);       /* program all 16 registers  */
void stdl_events_install(void);
void stdl_events_remove(void);
void stdl_time_init(void);
STDL_Sprite *stdl_sprite_preshift(STDL_Sprite *spr);

/* cooperative service hooks, run from the event pump (and the
 * compat SDL_Delay): audio ring refill and software cursor motion.
 * Defined in event.c, installed by audio.c / cursor.c so programs
 * that use neither don't link them. */
extern void (*stdl_audio_hook)(void);
extern void (*stdl_cursor_hook)(int x, int y);

/* shutdown hooks run by restore_all before leaving supervisor
 * mode: stop DMA playback / remove the music VBL slot even when
 * the program exits without closing them. Defined in video.c. */
extern void (*stdl_shutdown_audio)(void);
extern void (*stdl_shutdown_music)(void);

/* brief interrupt masking (supervisor mode) for state shared with
 * interrupt context; no-ops when built for host-side unit tests */
#ifdef __m68k__
static __inline__ uint16_t stdl_int_off(void)
{
    uint16_t sr;
    __asm__ volatile("move.w %%sr,%0\n\tori.w #0x0700,%%sr"
                     : "=d"(sr) :: "memory");
    return sr;
}

static __inline__ void stdl_int_restore(uint16_t sr)
{
    __asm__ volatile("move.w %0,%%sr" :: "d"(sr) : "memory");
}
#else
static __inline__ uint16_t stdl_int_off(void) { return 0; }
static __inline__ void stdl_int_restore(uint16_t sr) { (void)sr; }
#endif

/*
 * Shared YM2149 service (ym.c): one VBL tick drives the music tick
 * and then the effects tick, so effect voices deterministically
 * override music. stdl_ym_owned is a bitmask of voices (bit 0..2 =
 * A..C, bit 3 = noise generator) currently owned by effects; the
 * music stream skips their registers and hands them back through
 * stdl_ym_release_voice (which calls the restore hook music.c
 * installs). The hooks keep music and effects independently
 * linkable.
 */
int  stdl_ym_install(void);             /* claim the VBL slot        */
void stdl_ym_write(int reg, int val);   /* VBL/effects context only  */
void stdl_ym_mix_update(uint8_t clr, uint8_t set); /* r7, shadowed   */
void stdl_ym_release_voice(int voice);
extern volatile uint8_t stdl_ym_owned;
extern void (*stdl_music_tick)(void);   /* stream player per VBL     */
extern void (*stdl_sfx_tick)(void);     /* effects, after music      */
extern void (*stdl_ym_restore_voice)(int voice); /* stream re-write  */

/* mixer-register bits (tone + noise enable) of one voice */
#define STDL_YM_VOICE_BITS(v) ((uint8_t)((1u << (v)) | (1u << ((v) + 3))))

/* cooperative timer service (compat SDL timers register here so
 * they fire from the pump and the native delays too) */
extern void (*stdl_timer_hook)(void);

/*
 * BLiTTER (blitter.c). stdl_blitter_go runs one plane-rectangle
 * operation; callers decide per-op whether the area amortises the
 * register setup (the *_MIN_CELLS thresholds, in words-per-plane).
 * On host builds the driver is stubbed out by the 0 macro.
 */
#ifdef __m68k__
int  stdl_blitter_active(void);
void stdl_blitter_go(uintptr_t src, int16_t sxinc, int16_t syinc,
                     uintptr_t dst, int16_t dxinc, int16_t dyinc,
                     uint16_t em1, uint16_t em3,
                     uint16_t nwords, uint16_t nlines,
                     uint8_t hop, uint8_t op);
#else
#define stdl_blitter_active() 0
static __inline__ void stdl_blitter_go(uintptr_t src, int16_t sxinc,
    int16_t syinc, uintptr_t dst, int16_t dxinc, int16_t dyinc,
    uint16_t em1, uint16_t em3, uint16_t nwords, uint16_t nlines,
    uint8_t hop, uint8_t op)
{
    (void)src; (void)sxinc; (void)syinc; (void)dst; (void)dxinc;
    (void)dyinc; (void)em1; (void)em3; (void)nwords; (void)nlines;
    (void)hop; (void)op;
}
#endif

#define STDL_BLIT_HOP_ONES 0
#define STDL_BLIT_HOP_SRC  2
#define STDL_BLIT_OP_ZERO  0
#define STDL_BLIT_OP_AND   1    /* src AND dst  */
#define STDL_BLIT_OP_SRC   3
#define STDL_BLIT_OP_XOR   6    /* src XOR dst  */

#define STDL_BLIT_FILL_MIN_CELLS   32   /* fill: ng * rows          */
#define STDL_BLIT_COPY_MIN_CELLS   32   /* unmasked blit            */
#define STDL_BLIT_MASKED_MIN_CELLS 64   /* masked: 3 passes/plane   */

/* case-normalising fopen for GEMDOS: retries with an uppercased
 * basename so lowercase asset names in ported code just work */
void *stdl_fopen_ci(const char *path, const char *mode);

/* byte-order readers shared by the loaders */
static __inline__ uint16_t stdl_rd16(const uint8_t *p)
{
    return (uint16_t)((p[0] << 8) | p[1]);
}

static __inline__ uint32_t stdl_rd32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16)
         | ((uint32_t)p[2] << 8) | p[3];
}

static __inline__ uint16_t stdl_le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static __inline__ uint32_t stdl_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* bulk sample conversion to signed 8-bit mono/stereo frames at a
 * new rate (audio.c); shared by the mixer's chunk loader */
void stdl_audio_convert(int8_t *dst, uint32_t dst_frames,
                        const uint8_t *src, uint32_t src_frames,
                        uint16_t format, int channels, int mono_mix);

#endif /* STDL_INTERNAL_H */
