#!/usr/bin/env python3
# STDL - Planar Display Library for Atari ST
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: LGPL-2.1-or-later
"""
stdlconv - host-side asset converter for STDL.

Subcommands:

  bmp16 IN OUT [--colors N] [--st-palette] [--keycolor RRGGBB]
      Quantise any image Pillow can read to an indexed 4bpp BMP
      (loadable by STDL_LoadBMP on the ST). --st-palette snaps
      colours to the ST's 3-bit grid; --keycolor keeps that exact
      colour at index 15, excluded from quantisation.

  pi1 IN OUT
      Convert an image to a Degas PI1 (320x200, 16 colours) for
      STDL_LoadDegas / STDL_ShowDegas.

  embed IN OUT.c [--name SYMBOL]
      Embed any file as a C array (linked-in splashes etc.).

  wav IN OUT [--rate HZ]
      Decode a WAV (PCM or mono MS ADPCM) to unsigned 8-bit mono at
      an exact STE DMA rate for STDL_LoadWAV.

  midi IN OUT [--loop FRAME]
      Render a General MIDI file (SMF) to a YM2149 register stream
      (STM) for STDL_Music: one voice per instrument before chords,
      going by its program (bass, melody, then pads), envelopes and
      vibrato, and drums as noise or falling-tone bursts.

  bank OUT SPEC [SPEC...]
      Build an STDL asset bank. Each SPEC is one chunk:
        surface:ID:FILE[:key=N]
        sprite:ID:FILE:framew=N[:key=N]
        tileset:ID:FILE:tile=WxH[:key=N]
        palette:ID:FILE            (palette taken from image)
        font:ID:FILE:cell=WxH[:first=N][:last=N]
      Images must already be <= 16 colours (use bmp16 first).
      Pre-shifting happens on the ST at load (STDL_PRESHIFT).

Everything in a bank is big-endian, matching the 68000.
"""

import argparse
import math
import struct
import sys
from collections import Counter

try:
    # only the image subcommands (bmp16, pi1, bank) need Pillow;
    # wav, midi and embed run on the standard library alone
    from PIL import Image
except ImportError:
    Image = None

# ------------------------------------------------------------------
# audio

STE_RATES = (6258, 12517, 25033, 50066)

MSADPCM_COEFS = ((256, 0), (512, -256), (0, 0), (192, 64),
                 (240, 0), (460, -208), (392, -232))
MSADPCM_ADAPT = (230, 230, 230, 230, 307, 409, 512, 614,
                 768, 614, 512, 409, 307, 230, 230, 230)


def clamp16(v):
    return -32768 if v < -32768 else (32767 if v > 32767 else v)


def msadpcm_decode_mono(data, block_align):
    """Decode mono MS ADPCM blocks to a list of 16-bit samples."""
    out = []
    for boff in range(0, len(data) - 6, block_align):
        block = data[boff : boff + block_align]
        if len(block) < 7:
            break
        pred = block[0]
        if pred > 6:
            pred = 6
        c1, c2 = MSADPCM_COEFS[pred]
        delta, s1, s2 = struct.unpack("<hhh", block[1:7])
        out.append(s2)
        out.append(s1)
        for byte in block[7:]:
            for nib in (byte >> 4, byte & 0x0F):
                signed = nib - 16 if nib >= 8 else nib
                predicted = (s1 * c1 + s2 * c2) >> 8
                sample = clamp16(predicted + signed * delta)
                out.append(sample)
                s2, s1 = s1, sample
                delta = max(16, (MSADPCM_ADAPT[nib] * delta) >> 8)
    return out


def load_wav_samples(path):
    """Read a WAV (PCM 8/16-bit or mono MS ADPCM) as mono 16-bit."""
    d = open(path, "rb").read()
    if d[:4] != b"RIFF" or d[8:12] != b"WAVE":
        die(path + " is not a WAV file")
    fmt = None
    data = None
    i = 12
    while i < len(d) - 8:
        cid = d[i : i + 4]
        sz = struct.unpack("<I", d[i + 4 : i + 8])[0]
        body = d[i + 8 : i + 8 + sz]
        if cid == b"fmt ":
            fmt = struct.unpack("<HHIIHH", body[:16])
        elif cid == b"data":
            data = body
        i += 8 + sz + (sz & 1)
    if fmt is None or data is None:
        die(path + ": missing fmt/data chunk")
    wformat, channels, rate, _bps, align, bits = fmt

    if wformat == 2:
        if channels != 1:
            die("MS ADPCM: only mono supported")
        samples = msadpcm_decode_mono(data, align)
    elif wformat == 1 and bits == 8:
        samples = [(b - 128) << 8 for b in data]
    elif wformat == 1 and bits == 16:
        samples = list(struct.unpack("<%dh" % (len(data) // 2), data))
    else:
        die("unsupported WAV format %d/%d-bit" % (wformat, bits))

    if channels == 2:
        samples = [(samples[j] + samples[j + 1]) // 2
                   for j in range(0, len(samples) - 1, 2)]
    return rate, samples


def cmd_wav(args):
    rate, samples = load_wav_samples(args.input)
    out_rate = args.rate
    if out_rate not in STE_RATES:
        die("rate must be one of %s (STE DMA rates)"
            % ", ".join(map(str, STE_RATES)))
    n_out = int(len(samples) * out_rate / rate)
    out = bytearray()
    for j in range(n_out):
        s = samples[(j * rate) // out_rate]
        out.append(((s >> 8) + 128) & 0xFF)      # unsigned 8-bit
    if len(out) & 1:                             # DMA wants even counts
        out.append(128)
    with open(args.output, "wb") as f:
        datalen = len(out)
        f.write(b"RIFF" + struct.pack("<I", 36 + datalen) + b"WAVE")
        f.write(b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, out_rate,
                                      out_rate, 1, 8))
        f.write(b"data" + struct.pack("<I", datalen))
        f.write(out)
    print("wrote %s (%d Hz u8 mono, %d samples, %.1fs)"
          % (args.output, out_rate, datalen, datalen / out_rate))

CHUNK_PALETTE = 1
CHUNK_SURFACE = 2
CHUNK_SPRITE = 3
CHUNK_TILESET = 4
CHUNK_FONT = 5


def die(msg):
    sys.exit("stdlconv: " + msg)


def need_pillow():
    if Image is None:
        die("this subcommand needs Pillow (pip install pillow)")


def load_indexed(path, max_colors=16):
    """Load an image as (width, height, index-array, palette[(r,g,b)])."""
    need_pillow()
    img = Image.open(path)
    if img.mode != "P":
        img = img.convert("RGB").quantize(colors=max_colors)
    pal = img.getpalette()[: 256 * 3]
    px = list(img.getdata())
    used = max(px) + 1
    if used > max_colors:
        die("%s uses %d colours (max %d); run bmp16 first"
            % (path, used, max_colors))
    palette = [tuple(pal[i * 3 : i * 3 + 3]) for i in range(used)]
    return img.width, img.height, px, palette


def to_planar(w, h, px):
    """Convert an index array to ST interleaved planar rows."""
    groups = (w + 15) // 16
    out = bytearray(groups * 8 * h)
    for y in range(h):
        base = y * groups * 8
        for g in range(groups):
            words = [0, 0, 0, 0]
            for b in range(16):
                x = g * 16 + b
                if x >= w:
                    continue
                v = px[y * w + x]
                bit = 0x8000 >> b
                for p in range(4):
                    if v & (1 << p):
                        words[p] |= bit
            for p in range(4):
                struct.pack_into(">H", out, base + g * 8 + p * 2,
                                 words[p])
    return bytes(out)


def snap_st(c):
    """Snap an 8-bit channel to the ST's 3-bit grid."""
    return (c >> 5) * 255 // 7


def cmd_bmp16(args):
    need_pillow()
    img = Image.open(args.input).convert("RGB")
    keyrgb = None
    if args.keycolor:
        keyrgb = tuple(int(args.keycolor[i : i + 2], 16)
                       for i in (0, 2, 4))

    if keyrgb is None:
        q = img.quantize(colors=args.colors)
        if args.st_palette:
            pal = q.getpalette()[: args.colors * 3]
            pal = [snap_st(c) for c in pal]
            q.putpalette(pal + [0] * (768 - len(pal)))
        write_bmp4(args.output, q)
    else:
        # reserve index 15 for the exact key colour so colour-keyed
        # blits survive quantisation; key pixels are excluded from
        # the quantiser so no palette slot is wasted on (or left
        # near) the key colour
        ncol = min(args.colors, 15)
        data = list(img.getdata())
        keymask = [1 if p == keyrgb else 0 for p in data]
        opaque = [p for p, m in zip(data, keymask) if not m]
        filler = (Counter(opaque).most_common(1)[0][0] if opaque
                  else (0, 0, 0))
        cleaned = Image.new("RGB", img.size)
        cleaned.putdata([filler if m else p
                         for p, m in zip(data, keymask)])
        q = cleaned.quantize(colors=ncol)
        pal = q.getpalette()[: ncol * 3]
        if args.st_palette:
            pal = [snap_st(c) for c in pal]
        px = list(q.getdata())
        for i, m in enumerate(keymask):
            if m:
                px[i] = 15
        out = Image.new("P", img.size)
        out.putdata(px)
        # pad the palette so the key colour sits exactly at index 15
        pal = pal + [0] * (45 - len(pal))
        out.putpalette(pal + list(keyrgb) + [0] * 720)
        write_bmp4(args.output, out)
    print("wrote %s (%dx%d)" % (args.output, img.width, img.height))


def write_bmp4(path, img):
    """Write a P-mode image (<=16 colours) as an uncompressed 4bpp BMP."""
    w, h = img.width, img.height
    px = list(img.getdata())
    if max(px) > 15:
        die("more than 16 colours after quantise?")
    pal = img.getpalette()[: 16 * 3]
    pal += [0] * (48 - len(pal))
    rowbytes = (w * 4 + 31) // 32 * 4
    data = bytearray()
    for y in range(h - 1, -1, -1):
        row = bytearray(rowbytes)
        for x in range(w):
            v = px[y * w + x] & 15
            if x % 2 == 0:
                row[x // 2] |= v << 4
            else:
                row[x // 2] |= v
        data += row
    off = 14 + 40 + 16 * 4
    size = off + len(data)
    with open(path, "wb") as f:
        f.write(b"BM")
        f.write(struct.pack("<IHHI", size, 0, 0, off))
        f.write(struct.pack("<IiiHHIIiiII", 40, w, h, 1, 4, 0,
                            len(data), 2835, 2835, 16, 16))
        for i in range(16):
            r, g, b = pal[i * 3 : i * 3 + 3]
            f.write(struct.pack("<BBBB", b, g, r, 0))
        f.write(data)


def pack_group(px, w, y, x0, key):
    """One 16px group as (mask, planes[4]): mask bit set =
    transparent (key pixel), plane bits cleared under the mask."""
    mask = 0
    planes = [0, 0, 0, 0]
    for b in range(16):
        x = x0 + b
        v = px[y * w + x]
        bit = 0x8000 >> b
        if key is not None and v == key:
            mask |= bit
        else:
            for p in range(4):
                if v & (1 << p):
                    planes[p] |= bit
    return mask, planes


def pack_words(words):
    return struct.pack(">%dH" % len(words), *words)


def chunk_surface(spec):
    w, h, px, _pal = load_indexed(spec["file"])
    key = spec.get("key")
    payload = struct.pack(">HHBB", w, h, 1 if key is not None else 0,
                          key or 0)
    return payload + to_planar(w, h, px)


def chunk_palette(spec):
    _w, _h, _px, pal = load_indexed(spec["file"])
    payload = struct.pack(">H", len(pal))
    for r, g, b in pal:
        payload += struct.pack(">BBBB", r, g, b, 0)
    return payload


def chunk_sprite(spec):
    w, h, px, _pal = load_indexed(spec["file"])
    fw = spec["framew"]
    if fw % 16:
        die("sprite frame width must be a multiple of 16")
    if w % fw:
        die("image width %d is not a multiple of frame width %d"
            % (w, fw))
    nframes = w // fw
    groups = fw // 16
    key = spec.get("key")
    words = []
    for f in range(nframes):
        for y in range(h):
            for g in range(groups):
                mask, planes = pack_group(px, w, y, f * fw + g * 16,
                                          key)
                words.append(mask)
                words.extend(planes)
    framesize = groups * 5 * h
    payload = struct.pack(">HHHBBHI", fw, h, nframes, 1, 4, groups,
                          framesize)
    return payload + pack_words(words)


def chunk_tileset(spec):
    w, h, px, _pal = load_indexed(spec["file"])
    tw, th = spec["tile"]
    if tw % 16:
        die("tile width must be a multiple of 16")
    cols, rows = w // tw, h // th
    ntiles = cols * rows
    groups = tw // 16
    key = spec.get("key")
    masked = 1 if key is not None else 0
    words = []
    for t in range(ntiles):
        tx, ty = (t % cols) * tw, (t // cols) * th
        for y in range(th):
            for g in range(groups):
                mask, planes = pack_group(px, w, ty + y,
                                          tx + g * 16, key)
                if masked:
                    words.append(mask)
                words.extend(planes)
    tilesize = groups * (5 if masked else 4) * th
    payload = struct.pack(">HHHHBBI", tw, th, ntiles, groups, masked,
                          4, tilesize)
    return payload + pack_words(words)


def chunk_font(spec):
    """Glyphs in a horizontal strip, non-background = ink."""
    w, h, px, _pal = load_indexed(spec["file"])
    cw, ch = spec["cell"]
    first = spec.get("first", 32)
    nglyphs = w // cw
    last = spec.get("last", first + nglyphs - 1)
    if h < ch:
        die("font image shorter than cell height")
    bpr = (cw + 7) // 8
    bits = bytearray()
    for gl in range(last - first + 1):
        for y in range(ch):
            row = bytearray(bpr)
            for x in range(cw):
                if px[y * w + gl * cw + x] != 0:
                    row[x // 8] |= 0x80 >> (x % 8)
            bits += row
    return struct.pack(">HHBBH", cw, ch, first, last, bpr) + bytes(bits)


# ------------------------------------------------------------------
# Degas PI1

def rot_nibble(v):
    """4-bit channel value -> ST/STE rotated hardware nibble."""
    return ((v >> 1) | ((v & 1) << 3)) & 0x0F


def cmd_pi1(args):
    need_pillow()
    img = Image.open(args.input).convert("RGB")
    if img.size != (320, 200):
        img = img.resize((320, 200))
    q = img.quantize(colors=16)
    pal = q.getpalette()[: 48] + [0] * 48
    px = list(q.getdata())
    planar = to_planar(320, 200, px)
    with open(args.output, "wb") as f:
        f.write(struct.pack(">H", 0))            # low resolution
        for i in range(16):
            r, g, b = pal[i * 3 : i * 3 + 3]
            f.write(struct.pack(">H",
                    (rot_nibble(r >> 4) << 8)
                    | (rot_nibble(g >> 4) << 4)
                    | rot_nibble(b >> 4)))
        f.write(planar)
    print("wrote %s (Degas PI1, 32034 bytes)" % args.output)


def cmd_embed(args):
    data = open(args.input, "rb").read()
    name = args.name
    with open(args.output, "w") as f:
        f.write("/* generated by stdlconv embed from %s */\n"
                % args.input.replace("\\", "/").split("/")[-1])
        f.write("const unsigned char %s[%d] = {\n" % (name, len(data)))
        for i in range(0, len(data), 16):
            f.write(",".join(str(b) for b in data[i : i + 16]) + ",\n")
        f.write("};\n")
    print("wrote %s (%d bytes embedded as %s[])"
          % (args.output, len(data), name))


# ------------------------------------------------------------------
# MIDI -> YM register stream (STM)
#
# A General MIDI score reduced to the YM2149's 3 square voices and
# noise, tuned by ear against a GeneralUser GS rendering of a game
# score. Each instrument (channel) gets one voice before any gets
# two, going by its General MIDI program: the bass first, then the
# melodic instruments, then the pads, and only then a second note
# of a chord; a part plays its lowest note if a bass, else its
# highest, and a note keeps the voice it has. Notes below C2 play an
# octave up (a lower square wave is a buzz more than a note), with a
# short accent, a settle on long melodic notes, a swell on pads, a
# fade on plucked and struck notes and a vibrato on held ones. Drums
# are short noise or falling-tone bursts over a free voice, else
# over the bass (a kick or tom) or the least important note, so the
# parts play on. Levels follow the perceived loudness of each
# program and octave. The synth runs on a 120Hz clock, sampled at
# the stream's frame rate.

YM_CLOCK = 125000            # 2MHz / 16: tone period unit
TICK_HZ = 50
SYNTH_HZ = 120               # the clock the times below count in

NOTE_FLOOR = 36              # C2: lower notes play an octave up
ACCENT = 6                   # ticks of the accent starting a note
SETTLE = 30                  # ticks after which a melodic note settles
SWELL = 6                    # ticks per level of a pad's swell, 3 levels
VIBRATO_DELAY = 40           # ticks before a held note gets a vibrato
VIBRATO_TOP = 80             # notes from G#5 up get none
CRASH_OVER = 8               # ticks a cymbal may replace a melody/bass
HEADROOM = 2                 # levels under full scale, so the loudest
                             # program still fits in 15
VIBRATO = (0, 1, 2, 1, 0, -1, -2, -1)   # in 1/16 semitone

# levels of a note velocity, and taken off by a channel's volume or
# expression: about 3dB a level
VEL_LEVEL = [0] + [max(0, 15 - round(-40 * math.log10(v / 127) / 3))
                   for v in range(1, 128)]
ATTENUATION = [15] + [min(15, round(-40 * math.log10(v / 127) / 3))
                      for v in range(1, 128)]

# what a General MIDI program is, for the voice allocation
PAD, LEAD, BASS = 1, 2, 3
PRIORITY = 0x07
DECAY = 0x08                 # plucked or struck: the note fades
DRUM = 0x10                  # percussion: played as a drum
_L, _LD, _B, _P, _D = LEAD, LEAD | DECAY, BASS, PAD, DRUM
PROGRAMS = ([_LD] * 8                             # pianos
            + [_LD] * 8                           # chromatic percussion
            + [_L] * 8                            # organs
            + [_LD] * 8                           # guitars
            + [_B] * 8                            # basses
            + [_L, _L, _L, _B, _L, _LD, _LD, _D]  # strings, harp, timpani
            + [_P] * 7 + [_LD]                    # ensembles, orchestra hit
            + [_L] * 32                           # brass, reeds, pipes, leads
            + [_P] * 16                           # synth pads and effects
            + [_LD] * 5 + [_L] * 3                # ethnic
            + [_D] * 8                            # percussive
            + [0] * 8)                            # sound effects: left out

KICK, SNARE, CLAP, TOM, HIHAT, OPEN_HIHAT, CYMBAL, CLICK = range(8)
# noise period (0 none), ticks, level lost per tick in 1/4, level
# added to the velocity's, tone period (0 none), period added per
# tick (1 = a 16th of the period): kick and tom are falling tones
DRUM_KINDS = ((0, 8, 4, 0, 400, 160), (10, 10, 5, 0, 0, 0),
              (6, 5, 8, -1, 0, 0), (0, 12, 4, 0, 700, 1),
              (1, 3, 10, -3, 0, 0), (1, 14, 3, -3, 0, 0),
              (2, 36, 1, -1, 0, 0), (4, 4, 8, -2, 0, 0))
# the drum of General MIDI percussion notes 35 to 81
DRUM_NOTES = (
    KICK, KICK, CLAP, SNARE, CLAP, SNARE,                          # 35
    TOM, HIHAT, TOM, HIHAT, TOM, OPEN_HIHAT,                       # 41
    TOM, TOM, CYMBAL, TOM, OPEN_HIHAT, CYMBAL,                     # 47
    OPEN_HIHAT, HIHAT, CYMBAL, CLICK, CYMBAL, CLICK,               # 53
    OPEN_HIHAT, CLICK, CLICK, CLICK, CLICK, CLICK,                 # 59
    CLICK, CLICK, CLICK, CLICK, HIHAT, HIHAT,                      # 65
    CLICK, CLICK, HIHAT, HIHAT, CLICK, CLICK,                      # 71
    CLICK, CLICK, CLICK, HIHAT, HIHAT)                             # 77

# level of each program at C1 C2 .. C7 next to a lead instrument: its
# perceived (A-weighted) loudness in the GeneralUser GS SoundFont,
# minus that of the square wave playing the note, over 3dB a level.
# Deep strings and pads sit in the background, high notes, where
# square waves are harshest, lower.
LOUDNESS = (
    (0, 1, 1, -1, -2, -4, -8), (0, 1, 0, -1, -2, -4, -7),
    (-2, -1, -1, -1, -3, -2, -3), (0, 1, 0, -1, -2, -4, -7),
    (-4, -1, -2, 0, -1, -1, -2), (-3, -2, -2, -1, -2, -2, -2),
    (-1, 0, 0, -2, -3, -2, -4), (1, 1, 1, 0, 0, -1, -3),
    (-8, -4, -2, -1, -2, -2, -3), (-2, 0, 0, -1, -2, -5, -6),
    (-7, -4, -3, -3, -3, -4, -5), (0, 1, 1, 0, 0, 0, 0),
    (-1, -2, -2, -1, -2, -4, -7), (-1, -2, -4, -5, -8, -8, -8),
    (0, 2, 2, 1, -1, -2, -4), (-1, 0, 0, -2, -2, -5, -4),
    (-3, 0, 0, -1, -1, -1, -1), (-3, -1, -1, 0, -1, -1, -1),
    (-3, -1, 0, -1, -1, -1, 0), (-2, -1, 0, 0, -1, -1, -2),
    (-1, 0, 0, 1, -1, 0, -4), (-2, 0, 0, -1, -1, -1, -2),
    (-8, -3, -1, 0, -1, 0, 0), (-2, 0, 0, 0, -1, 0, 0),
    (0, 0, 0, -2, -3, -4, -4), (0, 0, -2, -1, -2, -4, -6),
    (-2, 0, -1, -2, -1, -2, -2), (-2, 0, 0, 0, -1, -2, -3),
    (-3, -1, -6, -7, -8, -8, -8), (1, 2, 1, 0, 0, 0, 1),
    (2, 2, 2, 1, 1, 0, -1), (-2, 2, 2, 2, 2, 1, -4),
    (-3, -2, -2, -3, -3, -4, -7), (-2, 0, 0, 0, -1, -2, -5),
    (-2, 0, -1, -2, -3, -3, -5), (-1, -1, 1, 1, 0, -1, -8),
    (-1, -1, 0, 0, -1, -2, -8), (-1, -1, -2, -2, -5, -7, -8),
    (-1, 0, 0, 0, -1, -2, -6), (0, 1, -1, -2, -3, -8, -8),
    (-4, -1, 1, 2, 1, 0, -1), (0, 2, 1, -1, -1, -1, -4),
    (-2, 1, 0, 0, 0, -1, 0), (-2, -1, -1, 0, 0, -1, -1),
    (-2, 0, -1, -1, -2, -1, -2), (-7, -4, -4, -5, -7, -8, -8),
    (-4, -2, 0, -1, -2, -2, -6), (-1, 0, -1, -1, -3, -4, -8),
    (-2, 0, 0, 0, -1, -1, -2), (-2, 0, -1, -1, -2, -1, -2),
    (-2, -1, 0, -1, -1, -2, -3), (-6, -6, -4, -3, -4, -4, -5),
    (-3, -1, -1, -1, -1, -1, -1), (-4, -2, -1, -1, 0, -1, 0),
    (-4, -3, -2, -2, -2, -2, -1), (-4, -2, 0, -1, -2, -5, -8),
    (-3, 1, 1, 1, 1, -2, -3), (-1, 1, 1, 1, 0, 0, 0),
    (-1, 0, 1, 1, 1, -3, -5), (-2, 1, -1, -2, 0, -2, -2),
    (-2, 1, 2, 2, 1, 1, 1), (-1, 2, 2, 1, 0, 0, -1),
    (-1, 0, 0, 0, -1, -1, -1), (-1, 0, 0, -1, 0, -1, -1),
    (-7, -1, 0, -1, 0, 1, 0), (0, 1, 1, 0, 0, 1, 1),
    (-1, 1, 1, 1, 1, -1, -1), (0, 1, 0, 0, 0, 0, -1),
    (-3, 1, 1, 0, 0, 1, 1), (-6, -1, 0, 0, 0, 1, 1),
    (1, 2, 2, 2, 1, 0, 0), (-6, 1, 2, 1, 1, 1, 1),
    (-8, -6, -3, 0, 1, 2, 1), (-8, -2, 0, 0, 1, 2, 0),
    (-8, -5, -2, -1, 0, 1, 1), (-8, -5, -2, -1, 0, 0, 0),
    (-1, -1, -2, -1, 0, 0, -2), (-1, 2, 2, 0, -2, -1, -2),
    (-6, -2, 0, 2, 2, 2, 2), (-8, -2, 0, 1, 1, 2, 2),
    (-1, 0, 0, 0, 0, -1, -1), (-1, 0, 0, 0, 0, 0, -1),
    (0, 0, 0, 1, 1, 1, -3), (-3, -2, -1, 0, -1, -1, -1),
    (0, 0, 0, 0, -1, -1, -4), (-2, 0, 1, 1, 2, 0, 1),
    (-2, -1, -1, -1, -1, -2, -1), (-1, 0, 0, 1, 0, 1, 1),
    (-1, 0, 0, 0, 0, -1, -2), (-4, -2, -3, -4, -6, -5, -6),
    (-2, -1, -1, -1, -1, -2, -2), (-2, 0, 0, 0, -1, -1, -1),
    (-2, -2, -2, 0, -2, -3, -4), (0, 1, -2, 0, -1, -1, -2),
    (-2, -1, -1, -1, -2, -3, -3), (-3, -2, -2, -3, -3, -4, -4),
    (-1, -2, -2, -2, -2, -2, -3), (-2, -2, -1, -2, -1, -6, -8),
    (-2, -1, -1, -1, 0, 0, 0), (-1, 0, 0, 0, -1, -3, -4),
    (-1, 0, 1, 0, 0, 0, -1), (-4, -7, -8, -6, -5, -3, -5),
    (-1, 0, 1, 1, 0, -1, -3), (-3, -1, 0, 1, 0, 1, 0),
    (1, 2, 2, 1, -2, -5, -8), (0, 0, -1, -2, -5, -8, -8),
    (-3, -3, -3, -4, -5, -8, -8), (2, 2, 1, 0, -2, -6, -8),
    (-4, -5, -4, -3, -3, -2, -2), (-1, 1, 1, 2, 1, 0, -2),
    (-4, 0, 1, 2, 0, 0, -1), (1, 2, 2, 2, 0, -1, -2),
    (-3, 0, 0, 0, -1, -2, -4), (-2, -1, -2, -5, -8, -8, -8),
    (-3, 1, 2, 0, -3, -8, -8), (-7, -8, -8, -8, -8, -8, -8),
    (-3, -3, -3, -4, -5, -7, -4), (-5, -5, -6, -6, -8, -8, -8),
    (-2, -3, -5, -6, -8, -8, -8), (-4, -3, -4, -5, -5, -5, -5),
    (1, 1, 0, -3, -6, -8, -8), (-8, -8, -8, -8, -8, -8, -8),
    (-2, 0, -1, -2, -4, -5, -5), (2, 2, 1, 0, -2, -3, -3),
    (2, 2, 1, 0, -2, -2, -2), (-7, -8, -8, -8, -8, -8, -8),
    (-4, -1, -2, -3, -4, -5, -5), (2, 1, -1, -3, -5, -7, -8),
)


def read_varint(d, i):
    v = 0
    while True:
        b = d[i]
        i += 1
        v = (v << 7) | (b & 0x7F)
        if not (b & 0x80):
            return v, i


def parse_smf(path):
    """Parse an SMF file into (division, [(tick, kind, ...)])."""
    d = open(path, "rb").read()
    if d[:4] != b"MThd":
        die(path + " is not a MIDI file")
    fmt, ntrk, division = struct.unpack(">HHH", d[8:14])
    if division & 0x8000:
        die("SMPTE-timed MIDI not supported")
    events = []
    i = 14
    for _ in range(ntrk):
        if d[i : i + 4] != b"MTrk":
            die("bad MIDI track header")
        length = struct.unpack(">I", d[i + 4 : i + 8])[0]
        j = i + 8
        end = j + length
        tick = 0
        status = 0
        while j < end:
            dt, j = read_varint(d, j)
            tick += dt
            b = d[j]
            if b & 0x80:
                status = b
                j += 1
            if status == 0xFF:                    # meta
                mtype = d[j]
                mlen, j2 = read_varint(d, j + 1)
                if mtype == 0x51:                 # set tempo
                    us = (d[j2] << 16) | (d[j2 + 1] << 8) | d[j2 + 2]
                    events.append((tick, "tempo", us))
                j = j2 + mlen
            elif status in (0xF0, 0xF7):          # sysex
                slen, j2 = read_varint(d, j)
                j = j2 + slen
            else:
                kind = status & 0xF0
                ch = status & 0x0F
                if kind in (0x80, 0x90, 0xA0, 0xB0, 0xE0):
                    a, b2 = d[j], d[j + 1]
                    j += 2
                    if kind == 0x90 and b2 > 0:
                        events.append((tick, "on", ch, a, b2))
                    elif kind == 0x80 or (kind == 0x90 and b2 == 0):
                        events.append((tick, "off", ch, a))
                    elif kind == 0xB0:
                        events.append((tick, "cc", ch, a, b2))
                    elif kind == 0xE0:
                        bend = ((b2 << 7) | a) - 8192
                        events.append((tick, "bend", ch, bend))
                elif kind in (0xC0, 0xD0):
                    if kind == 0xC0:
                        events.append((tick, "prog", ch, d[j]))
                    j += 1
        i = end
    # stable sort; note-offs first and note-ons last at the same tick
    order = {"tempo": 0, "off": 1, "cc": 2, "prog": 3, "bend": 4, "on": 5}
    events.sort(key=lambda e: (e[0], order[e[1]]))
    return division, events


class _Note:
    """A keyed note."""
    __slots__ = ("count", "ch", "note", "vel", "level", "kind", "stamp",
                 "tick")


def _period(pitch):
    """YM period of a pitch in (fractional) MIDI notes."""
    p = round(YM_CLOCK / (440.0 * 2.0 ** ((pitch - 69) / 12.0)))
    return max(1, min(0xFFF, p))


def _note_first(kind, n1, n2):
    """Whether a part plays note n1 before n2: a bass its lowest,
    the others their highest."""
    return n1 < n2 if (kind & PRIORITY) == BASS else n1 > n2


def midi_to_frames(path):
    """Render a MIDI file to a list of 14-byte YM register frames."""
    division, events = parse_smf(path)

    # event times in synth ticks, through the tempo map
    us_per_qn = 500000
    timed = []
    last_tick = 0
    t_us = 0.0
    for ev in events:
        t_us += (ev[0] - last_tick) * us_per_qn / division
        last_tick = ev[0]
        if ev[1] == "tempo":
            us_per_qn = ev[2]
            continue
        timed.append((int(t_us * SYNTH_HZ / 1e6 + 0.5),) + ev[1:])
    if not timed:
        die("no notes in MIDI file")
    total = timed[-1][0] * TICK_HZ // SYNTH_HZ + TICK_HZ // 2 + 1

    program = [0] * 16
    volume = [100] * 16          # General MIDI's defaults
    expression = [127] * 16
    bend = [0.0] * 16            # in 1/16 semitone
    notes = []                   # keyed notes
    voices = [None, None, None]  # the note each voice plays
    periods = [0, 0, 0]
    noise = 0
    drum = None
    stamp = 0

    def level_of(ch, vel):
        return (VEL_LEVEL[vel] - ATTENUATION[volume[ch]]
                - ATTENUATION[expression[ch]] - HEADROOM)

    def rank(n):
        # higher priority, then louder when keyed, then the part's own
        # order (see _note_first), then newer
        return (n.kind & PRIORITY, n.level,
                -n.note if (n.kind & PRIORITY) == BASS else n.note,
                n.stamp)

    def drum_level(now):
        """The drum's level now, 0 once over or faded out."""
        if drum is None:
            return 0
        age = now - drum["tick"]
        fade = DRUM_KINDS[drum["kind"]][2] * age
        if age >= drum["ticks"] or drum["level"] <= fade:
            return 0
        return (drum["level"] - fade) >> 2

    def drum_hit(t, ch, note, vel, kind):
        nonlocal drum
        # a hi-hat does not cut a kick or a snare short
        if drum_level(t) and drum["kind"] <= TOM < kind:
            return
        k = DRUM_KINDS[kind]
        level = level_of(ch, vel) + k[3]
        if level <= 0:
            return
        period = k[4]
        if kind == TOM and ch != 9:
            # a timpani is tuned
            while note < NOTE_FLOOR:
                note += 12
            period = _period(note)
        # timed from the next frame, so a drum shorter than a frame
        # still sounds its attack
        t = -(-t * TICK_HZ // SYNTH_HZ) * SYNTH_HZ // TICK_HZ
        drum = {"kind": kind, "ch": ch, "level": level << 2,
                "period": period, "ticks": k[1], "tick": t,
                "sweep": (period >> 4) if k[5] == 1 else k[5]}

    def note_on(t, ch, note, vel):
        nonlocal stamp
        if ch == 9:
            drum_hit(t, ch, note, vel, DRUM_NOTES[note - 35]
                     if 35 <= note < 35 + len(DRUM_NOTES) else HIHAT)
            return
        prog = program[ch]
        kind = PROGRAMS[prog]
        if kind & DRUM:
            # timpani, taiko / melodic tom / synth drum, reverse
            # cymbal, or bells and blocks
            drum_hit(t, ch, note, vel,
                     TOM if prog == 47 else
                     KICK if 116 <= prog <= 118 else
                     CYMBAL if prog == 119 else HIHAT)
            return
        if kind == 0:
            return
        for n in notes:
            if n.ch == ch and n.note == note:
                n.count += 1
                break
        else:
            n = _Note()
            n.count, n.ch, n.note = 1, ch, note
            notes.append(n)
        stamp += 1
        n.vel, n.level = vel, level_of(ch, vel)
        n.kind, n.stamp, n.tick = kind, stamp, t

    def note_off(ch, note):
        for n in notes:
            if n.ch == ch and n.note == note:
                n.count -= 1
                if n.count == 0:
                    notes.remove(n)
                return

    def channel_off(ch):
        nonlocal drum
        notes[:] = [n for n in notes if n.ch != ch]
        if drum is not None and drum["ch"] == ch:
            drum = None

    def allocate():
        # one voice per part first, then the other notes
        parts = {}
        for n in notes:
            p = parts.get(n.ch)
            if p is None or _note_first(n.kind, n.note, p.note):
                parts[n.ch] = n
        chosen = []
        while len(chosen) < 3:
            pool = [n for n in parts.values() if n not in chosen]
            if not pool:
                pool = [n for n in notes if n not in chosen]
            if not pool:
                break
            chosen.append(max(pool, key=rank))
        # a note keeps its voice; the others take a free one from A up
        for v in range(3):
            if voices[v] not in chosen:
                voices[v] = None
        for n in chosen:
            if n not in voices:
                voices[voices.index(None)] = n

    def drum_voice():
        # a free voice, else for a kick or tom the bass, else the one
        # playing the least important note
        host, host_prio = 0, 0xFF
        for v in (2, 1, 0):
            n = voices[v]
            if n is None:
                return v
            p = n.kind & PRIORITY
            if drum["period"] and p == BASS:
                p = 0
            if p < host_prio:
                host, host_prio = v, p
        return host

    # the synth runs tick by tick, sharing the voices again after each
    # tick's messages; the frames sample its registers
    ei = 0
    out = []
    for now in range((total - 1) * SYNTH_HZ // TICK_HZ + 1):
        while ei < len(timed) and timed[ei][0] <= now:
            ev = timed[ei]
            ei += 1
            kind, ch = ev[1], ev[2]
            if kind == "on":
                note_on(ev[0], ch, ev[3], ev[4])
            elif kind == "off":
                note_off(ch, ev[3])
            elif kind == "cc":
                if ev[3] == 7:
                    volume[ch] = ev[4]
                elif ev[3] == 11:
                    expression[ch] = ev[4]
                elif ev[3] in (120, 123):       # sound / notes off
                    channel_off(ch)
                elif ev[3] == 121:              # reset controllers
                    expression[ch] = 127
                    bend[ch] = 0.0
            elif kind == "prog":
                program[ch] = ev[3]
            elif kind == "bend":                # +/- 2 semitones
                bend[ch] = ev[3] / 256.0
        allocate()
        if now != len(out) * SYNTH_HZ // TICK_HZ:
            continue

        levels = [0, 0, 0]
        mix = 0x3F                       # all off (1 = disabled)
        for v in range(3):
            mix &= ~(1 << v)             # tone on: silent at level 0
            n = voices[v]
            if n is None:
                continue
            age = now - n.tick
            prio = n.kind & PRIORITY

            # a held melodic note gets a vibrato after a while
            vib = 0
            if (not n.kind & DECAY and prio in (LEAD, PAD)
                    and n.note < VIBRATO_TOP and age >= VIBRATO_DELAY):
                vib = VIBRATO[((age - VIBRATO_DELAY) * 3 >> 3) & 7]
            note = n.note
            while note < NOTE_FLOOR:
                note += 12
            periods[v] = _period(note + (bend[n.ch] + vib) / 16.0)

            octave = 0
            nn = n.note
            while nn >= 30 and octave < 6:
                nn -= 12
                octave += 1
            level = level_of(n.ch, n.vel) + LOUDNESS[program[n.ch]][octave]

            # plucked and struck notes fade, the others have an
            # accent, melodic notes settle and pads swell
            if n.kind & DECAY:
                level -= min(age >> 4, 6)
            elif prio == PAD:
                level -= 1 + sum(age < SWELL * i for i in (1, 2, 3))
            elif prio == LEAD:
                level -= (age >= ACCENT) + (age >= SETTLE)
            elif prio == BASS:
                level -= age >= ACCENT
            levels[v] = max(0, min(15, level))

        # the drum sounds over a voice
        dl = drum_level(now)
        if drum is not None and dl == 0:
            drum = None
        if drum is not None:
            k = DRUM_KINDS[drum["kind"]]
            age = now - drum["tick"]
            v = drum_voice()
            if drum["period"]:
                # a kick or tom: its own falling tone
                periods[v] = min(drum["period"] + drum["sweep"] * age,
                                 0xFFF)
                levels[v] = min(15, dl)
                if k[0]:
                    mix &= ~(8 << v)
            else:
                # noise: on a free voice, or instead of the least
                # important note (noise over a tone sounds its pitch),
                # a cymbal only briefly instead of a melody or the bass
                n = voices[v]
                if (n is None or age < CRASH_OVER
                        or (n.kind & PRIORITY) < LEAD
                        or drum["kind"] < OPEN_HIHAT):
                    mix |= 1 << v
                    mix &= ~(8 << v)
                    levels[v] = min(15, dl)
            if mix & 0x38 != 0x38:
                noise = k[0]

        regs = [0] * 14
        for v in range(3):
            regs[2 * v] = periods[v] & 0xFF
            regs[2 * v + 1] = (periods[v] >> 8) & 0x0F
            regs[8 + v] = levels[v]
        regs[6] = noise
        regs[7] = mix
        out.append(regs)
    return out


def delta_encode(frames):
    """Pack register frames as (mask, changed bytes) deltas."""
    shadow = [None] * 14
    blob = bytearray()
    for regs in frames:
        mask = 0
        payload = bytearray()
        for r in range(14):
            if regs[r] != shadow[r]:
                mask |= 1 << r
                payload.append(regs[r])
                shadow[r] = regs[r]
        blob += struct.pack(">H", mask) + payload
    return blob


def cmd_midi(args):
    frames = midi_to_frames(args.input)
    loop = min(args.loop, len(frames) - 1)
    blob = delta_encode(frames)
    with open(args.output, "wb") as f:
        f.write(b"STM1")
        f.write(struct.pack(">HHHH", TICK_HZ, len(frames), loop, 0))
        f.write(blob)
    print("wrote %s (%d frames = %.1fs at %dHz, %d bytes)"
          % (args.output, len(frames), len(frames) / TICK_HZ,
             TICK_HZ, 12 + len(blob)))


def parse_spec(text):
    parts = text.split(":")
    if len(parts) < 3:
        die("bad chunk spec: " + text)
    spec = {"type": parts[0], "id": int(parts[1]), "file": parts[2]}
    for opt in parts[3:]:
        if "=" in opt:
            k, v = opt.split("=", 1)
            if k in ("key", "framew", "first", "last"):
                spec[k] = int(v)
            elif k in ("tile", "cell"):
                a, b = v.split("x")
                spec[k] = (int(a), int(b))
            else:
                die("unknown option " + opt)
        else:
            die("unknown option " + opt)
    return spec


BUILDERS = {
    "surface": (CHUNK_SURFACE, chunk_surface),
    "palette": (CHUNK_PALETTE, chunk_palette),
    "sprite": (CHUNK_SPRITE, chunk_sprite),
    "tileset": (CHUNK_TILESET, chunk_tileset),
    "font": (CHUNK_FONT, chunk_font),
}


def cmd_bank(args):
    chunks = []
    for text in args.spec:
        spec = parse_spec(text)
        if spec["type"] not in BUILDERS:
            die("unknown chunk type " + spec["type"])
        ctype, builder = BUILDERS[spec["type"]]
        chunks.append((ctype, spec["id"], builder(spec)))
    dirsize = 8 + 12 * len(chunks)
    off = dirsize
    with open(args.output, "wb") as f:
        f.write(b"STDL" + struct.pack(">HH", 1, len(chunks)))
        for ctype, cid, payload in chunks:
            f.write(struct.pack(">HHII", ctype, cid, off, len(payload)))
            off += len(payload)
        for _ctype, _cid, payload in chunks:
            f.write(payload)
    print("wrote %s (%d chunks, %d bytes)"
          % (args.output, len(chunks), off))


def main():
    ap = argparse.ArgumentParser(prog="stdlconv")
    sub = ap.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("bmp16", help="quantise image to 4bpp BMP")
    p.add_argument("input")
    p.add_argument("output")
    p.add_argument("--colors", type=int, default=16)
    p.add_argument("--st-palette", action="store_true")
    p.add_argument("--keycolor", metavar="RRGGBB",
                   help="preserve this exact colour as index 15")
    p.set_defaults(func=cmd_bmp16)

    p = sub.add_parser("bank", help="build an STDL asset bank")
    p.add_argument("output")
    p.add_argument("spec", nargs="+")
    p.set_defaults(func=cmd_bank)

    p = sub.add_parser("pi1", help="convert image to a Degas PI1 "
                                   "(320x200, 16 colours)")
    p.add_argument("input")
    p.add_argument("output")
    p.set_defaults(func=cmd_pi1)

    p = sub.add_parser("embed", help="embed any file as a C array")
    p.add_argument("input")
    p.add_argument("output")
    p.add_argument("--name", default="embedded_data")
    p.set_defaults(func=cmd_embed)

    p = sub.add_parser("midi",
                       help="render a MIDI file to a YM2149 register "
                            "stream (STM) for STDL_Music")
    p.add_argument("input")
    p.add_argument("output")
    p.add_argument("--loop", type=int, default=0,
                   help="frame to loop back to (default 0)")
    p.set_defaults(func=cmd_midi)

    p = sub.add_parser("wav",
                       help="convert WAV (incl. MS ADPCM) to u8 mono "
                            "PCM at an exact STE DMA rate")
    p.add_argument("input")
    p.add_argument("output")
    p.add_argument("--rate", type=int, default=12517,
                   help="6258, 12517, 25033 or 50066 (default 12517)")
    p.set_defaults(func=cmd_wav)

    args = ap.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
