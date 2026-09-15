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
/* TOS vertical-blank queue: nvbls entries at *_vblqueue, each either
 * NULL or a routine the VBL interrupt calls. Shared by the YM sound
 * tick (ym.c) and the public callback API (vbl.c). */
#define STDL_NVBLS      (*(volatile uint16_t *)0x454UL)
#define STDL_VBLQUEUE   (*(void (***)(void))0x456UL)
/* Video address counter, three bytes of a 24-bit address. During the
 * vertical blank it holds the base the next frame will fetch, which
 * is how overscan.c places its flicks and how hwscroll.c knows a
 * base has been latched. Read it inside a line's fetch or in the
 * blanking - AGENTS.md records that Hatari parks it at the line end
 * at 16MHz, so a mid-line read is not trustworthy there. */
/* Video base, the address the Shifter starts each frame from. The
 * high two bytes exist on every ST; the low byte is STE-only, so a
 * base that must work on both is 256-byte aligned and only the two
 * are written. The Shifter latches this about three lines before
 * the VBL, so a write has until then to land for the next frame. */
#define STDL_VB_HI      (*(volatile uint8_t *)0xFFFF8201UL)
#define STDL_VB_MID     (*(volatile uint8_t *)0xFFFF8203UL)
#define STDL_VC_HI      (*(volatile uint8_t *)0xFFFF8205UL)
#define STDL_VC_MID     (*(volatile uint8_t *)0xFFFF8207UL)
#define STDL_VC_LO      (*(volatile uint8_t *)0xFFFF8209UL)
/* YM2149: write a register number to SELECT, then its value to
 * DATA; reading SELECT returns the selected register's contents
 * (READBACK), which is how the mixer's port-direction bits are
 * preserved. CONTERM is TOS's console-attributes byte, bit 0 the
 * key click that would otherwise fight over the chip. */
#define STDL_YM_SELECT  (*(volatile uint8_t *)0xFFFF8800UL)
#define STDL_YM_DATA    (*(volatile uint8_t *)0xFFFF8802UL)
/* const: read-only by construction, because this names the select
 * port here and the data register on the host - an assignment
 * through it would pass the host tests and select a stray register
 * on the chip. */
#define STDL_YM_READBACK (*(const volatile uint8_t *)0xFFFF8800UL)
#define STDL_CONTERM    (*(volatile uint8_t *)0x484UL)
#else
/* host-test builds: the "registers" are plain memory provided by
 * tests/host/stubs.c, so every module compiles natively */
extern volatile uint32_t stdl_host_clock;
extern volatile uint16_t stdl_host_hwpal[16];
extern volatile uint16_t stdl_host_nvbls;
extern void (**stdl_host_vblqueue)(void);
extern volatile uint8_t stdl_host_vidcnt[3];
#define STDL_HZ200      stdl_host_clock
#define STDL_FRCLOCK    stdl_host_clock
#define STDL_HWPAL      stdl_host_hwpal
#define STDL_NVBLS      stdl_host_nvbls
#define STDL_VBLQUEUE   stdl_host_vblqueue
extern volatile uint8_t stdl_host_vidbase[2];
#define STDL_VB_HI      stdl_host_vidbase[0]
#define STDL_VB_MID     stdl_host_vidbase[1]
#define STDL_VC_HI      stdl_host_vidcnt[0]
#define STDL_VC_MID     stdl_host_vidcnt[1]
#define STDL_VC_LO      stdl_host_vidcnt[2]
/* the YM as a register file: SELECT is the index, DATA the selected
 * entry, so a test reads the chip state straight out of the array */
extern volatile uint8_t stdl_host_ym_sel;
extern volatile uint8_t stdl_host_ym[16];
extern volatile uint8_t stdl_host_conterm;
/* Writes go through stdl_host_ym_at, which fails the test if the
 * selected register is 14 or 15: those are the chip's port pins,
 * which on an ST drive floppy select, and the register numbers in
 * sfx.c and music.c are computed (2*v, 8+v) rather than literal.
 * A host test is the only place that can be caught. */
unsigned stdl_host_ym_at(unsigned sel);
#define STDL_YM_SELECT  stdl_host_ym_sel
#define STDL_YM_DATA    stdl_host_ym[stdl_host_ym_at(stdl_host_ym_sel)]
#define STDL_YM_READBACK \
    ((const volatile uint8_t *)stdl_host_ym)[stdl_host_ym_sel & 15]
#define STDL_CONTERM    stdl_host_conterm
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

/*
 * Plane budget (planes.c): the number of low bitplanes the drawing
 * paths maintain. 4 is the default and means "all of them" - the
 * behaviour of every build before the budget existed. A lower value
 * is the program's promise that no colour index >= 2^stdl_planes is
 * drawn, so planes stdl_planes..3 are zero everywhere and writing
 * them is a no-op that can be skipped.
 *
 * gcc 4.6 does not unswitch loops, so a runtime plane count in an
 * inner loop would cost more than it saves. Hot paths put the loop
 * body in an STDL_PLANE_INLINE helper taking a `const int np` and
 * call it through STDL_PLANE_DISPATCH, placed outside every loop;
 * each instantiation then unrolls with np as a constant.
 *
 * A budget only ever licenses skipping a write, never forces one,
 * so a path may still write a high plane when the value it would
 * write is provably zero (whole-block clears use memset for that).
 * That is also why there are two instantiations and not four: a
 * budget is rounded UP to the nearest one (1 -> 2, 3 -> 4), which
 * maintains a plane the caller promised never to use - and that
 * plane is zero everywhere, so every word written to it is a zero
 * on top of a zero. Four instantiations cost 12K of text in every
 * linked program, unreachable in the default-budget case; on a
 * 512K-1M machine that is the difference between a port fitting
 * and not (it broke FreeNukum at 1M). Two cost half as much and
 * leave the two budgets ports actually use - 4 and 2 - unrolled.
 */
extern int stdl_planes;
void stdl_planes_clear_screens(void);
void stdl_planes_normalise(uint8_t *base, int stride, int h);

/* colour indices are effectively masked to the budget: the XOR ops
 * use this directly (a plane whose colour bit is clear is never
 * touched), the rest get it for free by not writing high planes */
#define STDL_COL_MASK ((uint8_t)((1u << stdl_planes) - 1u))

#define STDL_PLANE_INLINE \
    static __inline__ __attribute__((always_inline))

/*
 * The two composition flags as a compile-time class, the same trick
 * the plane count already gets: inside the loops they stop being
 * values tested per group per row and become constants the compiler
 * folds away, freeing the registers that held them. Four classes
 * times two plane classes is eight instances of a row loop, which
 * is most of why PIXEL_MAX is where it is - measured at 6548 bytes
 * for 13-19% on masked blits.
 */
#define STDL_FLAG_DISPATCH(fl, BODY) \
    do {                                                        \
        if (((fl) & (STDL_BLIT_UNDER | STDL_BLIT_MARK)) == 0) { \
            BODY(0u);                                           \
        } else if (((fl) & STDL_BLIT_MARK) == 0) {              \
            BODY(STDL_BLIT_UNDER);                              \
        } else if (((fl) & STDL_BLIT_UNDER) == 0) {             \
            BODY(STDL_BLIT_MARK);                               \
        } else {                                                \
            BODY(STDL_BLIT_UNDER | STDL_BLIT_MARK);             \
        }                                                       \
    } while (0)

#define STDL_PLANE_DISPATCH(np, BODY) \
    do {                              \
        if ((np) <= 2) { BODY(2); }   \
        else { BODY(4); }             \
    } while (0)

/*
 * Store / merge the low np plane words of one group. The words are
 * passed as scalars, not an array: an array parameter escapes and
 * gcc 4.6 then reloads it after every store to the destination.
 */
STDL_PLANE_INLINE void stdl_put_planes(uint16_t *grp,
    uint16_t w0, uint16_t w1, uint16_t w2, uint16_t w3, const int np)
{
    grp[0] = w0;
    if (np > 1) grp[1] = w1;
    if (np > 2) grp[2] = w2;
    if (np > 3) grp[3] = w3;
}

/* grp = (grp & ~m) | (w & m), low np planes only */
STDL_PLANE_INLINE void stdl_merge_planes(uint16_t *grp, uint16_t m,
    uint16_t w0, uint16_t w1, uint16_t w2, uint16_t w3, const int np)
{
    uint16_t keep = (uint16_t)~m;

    grp[0] = (uint16_t)((grp[0] & keep) | (w0 & m));
    if (np > 1) grp[1] = (uint16_t)((grp[1] & keep) | (w1 & m));
    if (np > 2) grp[2] = (uint16_t)((grp[2] & keep) | (w2 & m));
    if (np > 3) grp[3] = (uint16_t)((grp[3] & keep) | (w3 & m));
}

#define STDL_SCREEN_W       320
#define STDL_SCREEN_H       200
#define STDL_SCREEN_PLANES  4
#define STDL_SCREEN_STRIDE  160
#define STDL_SCREEN_BYTES   32000

typedef struct {
    int              initialised;
    int              video_set;
    STDL_MachineInfo mach;

    /* who owns the sound DMA (see STDL_DMA_*): the ring device, a
     * one-shot/looping sample, and the voice mixer are mutually
     * exclusive users of the one channel */
    uint8_t          dma_owner;

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

/* shutdown hooks run before leaving supervisor mode: stop DMA
 * playback, remove the music VBL slot and drop public VBL callbacks
 * even when the program exits without closing them. They run from
 * the GEMDOS terminate vector too, so they may only touch hardware
 * and vectors - no GEMDOS calls, no heap. Defined in video.c. */
extern void (*stdl_shutdown_audio)(void);
extern void (*stdl_shutdown_music)(void);
extern void (*stdl_shutdown_vbl)(void);
extern void (*stdl_shutdown_overscan)(void);
extern void (*stdl_shutdown_hwscroll)(void);

/* Mega STE speed/cache control: the register, the requested mode
 * (see STDL_UseMegaSteSpeedup) and the read-modify-write that
 * applies it. video.c owns them because STDL_Init sets the speed;
 * src/cpuspeed.c holds the public call. */
#ifdef __m68k__
#define STDL_MSTE_CTL (*(volatile uint8_t *)0xFFFF8E21UL)
#else
extern volatile uint8_t stdl_host_mste_ctl;
#define STDL_MSTE_CTL stdl_host_mste_ctl
#endif
/* Set by overscan.c while a border is open: STDL_Flip routes the
 * page flip through the module that owns the video base, instead of
 * Setscreen, which would fight it. Returns non-zero if it handled
 * the flip. */
extern int (*stdl_ovsc_flip)(void);

extern int stdl_megaste_mode;
void stdl_megaste_apply(int mode);

/* while set, blitter.c asks this before starting an operation of
 * nlines lines costing cpl bus cycles each: the answer is how many
 * of them may run now in hog mode - all of them, or the ones that
 * end before the next border ISR window, or none, in which case the
 * driver runs the operation in shared mode. The policy may wait
 * for a window to pass before answering; the driver splits the
 * operation and asks again for the rest. The overscan border
 * tricks need Timer A/B interrupts taken inside a scanline window,
 * and a hog blit stalls the CPU past it. Set and cleared by
 * overscan.c; defined in video.c (always linked). */
extern uint16_t (*stdl_blit_policy)(uint16_t nlines, uint32_t cpl);

/* while set, stdl_palette_apply_hw routes through this instead of
 * writing the registers - overscan.c stages writes into the
 * vertical blanking with it. Defined in video.c (always linked). */
extern void (*stdl_pal_apply_hook)(void);

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
 * Shared YM2149 service (ym.c): one VBL tick drives the music tick,
 * then the tone tick, then the effects tick, so live tones override
 * music and effects override both. stdl_ym_owned is a bitmask of
 * voices (bit 0..2 = A..C, bit 3 = noise generator) currently owned
 * above the music stream; the stream skips their registers and gets
 * them back through stdl_ym_release_voice (which offers the voice to
 * the tone hook first, then the restore hook music.c installs). The
 * hooks keep music, tones and effects independently linkable.
 */
int  stdl_ym_install(void);             /* claim the VBL slot        */
void stdl_ym_write(int reg, int val);   /* VBL/effects context only  */
void stdl_ym_mix_update(uint8_t clr, uint8_t set); /* r7, shadowed   */
void stdl_ym_release_voice(int voice);
extern volatile uint8_t stdl_ym_owned;
extern void (*stdl_music_tick)(void);   /* stream player per VBL     */
extern void (*stdl_tone_tick)(void);    /* live notes, after music   */
extern void (*stdl_sfx_tick)(void);     /* effects, after tones      */
extern void (*stdl_ym_restore_voice)(int voice); /* stream re-write  */
/* tone.c's notice of an effect taking a voice (lost) or handing it
 * back (not lost); returns non-zero when a tone re-took the voice,
 * so the stream restore is skipped. Effects claim voices through
 * stdl_ym_claim_voice so the notice is sent. */
extern int (*stdl_ym_tone_hook)(int voice, int lost);
void stdl_ym_claim_voice(int voice);    /* effects context only      */

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
/* the invariant registers once, then one call per plane: a
 * four-plane copy was writing eleven identical registers four times
 * over, half the fitted setup cost */
void stdl_blitter_setup(int16_t sxinc, int16_t syinc,
                        int16_t dxinc, int16_t dyinc,
                        uint16_t em1, uint16_t em3, uint16_t nwords,
                        uint8_t hop, uint8_t op);
void stdl_blitter_run(uintptr_t src, uintptr_t dst, uint16_t nwords,
                      uint16_t nlines, uint8_t hop);
void stdl_blitter_go(uintptr_t src, int16_t sxinc, int16_t syinc,
                     uintptr_t dst, int16_t dxinc, int16_t dyinc,
                     uint16_t em1, uint16_t em3,
                     uint16_t nwords, uint16_t nlines,
                     uint8_t hop, uint8_t op);
#else
#define stdl_blitter_active() 0
static __inline__ void stdl_blitter_setup(int16_t sxinc, int16_t syinc,
    int16_t dxinc, int16_t dyinc, uint16_t em1, uint16_t em3,
    uint16_t nwords, uint8_t hop, uint8_t op)
{
    (void)sxinc; (void)syinc; (void)dxinc; (void)dyinc;
    (void)em1; (void)em3; (void)nwords; (void)hop; (void)op;
}
static __inline__ void stdl_blitter_run(uintptr_t src, uintptr_t dst,
    uint16_t nwords, uint16_t nlines, uint8_t hop)
{
    (void)src; (void)dst; (void)nwords; (void)nlines; (void)hop;
}
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
/*
 * Is the BLiTTER worth it for an unmasked copy? Both costs are
 * linear and the comparison is one multiply, which is nothing
 * beside a blit: the BLiTTER is a fixed setup plus a rate per cell,
 * the CPU path a smaller per-blit cost plus a per-row term and a
 * per-cell term. So the BLiTTER wins once
 *
 *     h * (ROW + CELL * ng)  >  SETUP
 *
 * A cell count alone cannot express this - 64x8 and 16x32 are both
 * 32 cells, and on an STE the CPU path is 40% faster for the first
 * and 3% slower for the second, because what the CPU pays for is
 * rows.
 *
 * The constants are fitted to measurements from
 * tests/hatari/blitcost.c on an emulated STE, in units where only
 * their ratios matter. They are the 8MHz numbers: a Mega STE at
 * 16MHz runs the CPU path about 1.8x faster, so these hand it a few
 * blits between roughly 128x16 and 192x16 that it would have done
 * slightly quicker itself. That is a few percent on a machine where
 * blits are least likely to be the bottleneck, and it keeps one set
 * of numbers rather than a set per clock - which the speed control
 * can change at run time anyway.
 *
 * Both multiplies stay 16-bit: written as plain ints this test
 * costs two __mulsi3 calls on every blit the BLiTTER is allowed to
 * consider, which measured 8% of a tile blit - on blits it then
 * declines to accelerate.
 *
 * Re-fitted again after the BLiTTER's per-plane register writes
 * were hoisted out of the plane loop, which took 11-13% off every
 * accelerated blit and moved the crossover down: 128x8 and 32x16
 * belong to the BLiTTER now and did not before.
 *
 * They were fitted after the short-row copy went inline; that made
 * the CPU path up to twice as fast for tile-sized blits, which is
 * what made the previous flat 32-cell threshold wrong. Re-measure
 * if either path changes.
 */
#define STDL_BLIT_CPU_ROW     26
#define STDL_BLIT_CPU_CELL     7
#define STDL_BLIT_SETUP      630
#define STDL_BLIT_MASKED_MIN_CELLS 64   /* masked: 3 passes/plane   */

/*
 * Row offset y * stride. gcc 4.6 compiles a plain 32-bit multiply
 * into a __mulsi3 library call - measured at ~270 cycles on an
 * 8MHz 68000, which is more than a short span's worth of pixel
 * writes. Both operands are 16-bit by construction (y is clipped
 * to the surface, stride is a uint16_t), and a 16x16->32 mulu.w is
 * a single instruction. Only valid for a clipped, non-negative y.
 */
#ifdef __m68k__
static __inline__ uint32_t stdl_row_off(int y, uint16_t stride)
{
    /* spelled out: the cast form `(uint32_t)(uint16_t)y * stride`
     * usually compiles to mulu.w, but gcc 4.6 still reached for
     * __mulsi3 when the stride was a selected constant or came from
     * an inlined argument (blitter.c's cost per line, the tile
     * blit's row advance) - measured, not assumed */
    uint32_t r = (uint16_t)y;

    __asm__("mulu.w %1,%0" : "+d"(r) : "d"(stride));
    return r;
}

/*
 * 32/16 divide on the 68000's own instruction. gcc 4.6 calls
 * __udivsi3 for a 32-bit division even when the quotient provably
 * fits a word - about 350 cycles against 140. The caller must know
 * the quotient fits, or the CPU traps.
 */
static __inline__ uint16_t stdl_divu(uint32_t a, uint16_t b)
{
    __asm__("divu.w %1,%0" : "+d"(a) : "d"(b));
    return (uint16_t)a;
}

/* The signed twin, for products whose operands are known to fit
 * sixteen bits (a clipped count times a pitch, a sample times a
 * gain). */
static __inline__ int32_t stdl_mul16(int a, int b)
{
    int32_t r = (int16_t)a;

    __asm__("muls.w %1,%0" : "+d"(r) : "d"((int16_t)b));
    return r;
}
#else
static __inline__ uint32_t stdl_row_off(int y, uint16_t stride)
{
    return (uint32_t)(uint16_t)y * stride;
}

static __inline__ uint16_t stdl_divu(uint32_t a, uint16_t b)
{
    return (uint16_t)(a / b);
}

static __inline__ int32_t stdl_mul16(int a, int b)
{
    return (int32_t)(int16_t)a * (int16_t)b;
}
#endif

/* A 32-bit value times a 16-bit one in two mulu.w, for the frame
 * and variant offsets into sprite data, whose frame size can pass
 * sixteen bits: about 150 cycles against __mulsi3's 270-plus. */
static __inline__ uint32_t stdl_mul32x16(uint32_t a, uint16_t b)
{
    return (stdl_row_off((int)(a >> 16), b) << 16)
         + stdl_row_off((int)(a & 0xFFFFu), b);
}

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

/* sound DMA ownership states (stdl.dma_owner) */
#define STDL_DMA_FREE   0
#define STDL_DMA_RING   1   /* STDL_OpenAudio                        */
#define STDL_DMA_SAMPLE 2   /* STDL_PlaySample / STDL_PlaySampleLoop */
#define STDL_DMA_VOICES 3   /* STDL_OpenVoices                       */

/* shared DMA plumbing (audio.c): rate table and nearest-rate pick,
 * program+start (stops first; registers latch at start), stop, and
 * the frame address counter. Host builds get stubs. */
extern const int stdl_dma_rates[4];
int      stdl_dma_nearest(int freq);
void     stdl_dma_start(const void *data, uint32_t bytes,
                        uint8_t mode, int repeat);
void     stdl_dma_stop(void);
uint32_t stdl_dma_counter(void);

/* bulk sample conversion to signed 8-bit mono/stereo frames at a
 * new rate (audio.c); shared by the mixer's chunk loader */
void stdl_audio_convert(int8_t *dst, uint32_t dst_frames,
                        const uint8_t *src, uint32_t src_frames,
                        uint16_t format, int channels, int mono_mix);

#endif /* STDL_INTERNAL_H */
