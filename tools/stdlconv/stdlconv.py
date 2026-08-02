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
      Render an SMF MIDI file to a YM2149 register stream (STM) for
      STDL_Music: 3 voices, last-note priority, drums to noise.

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

YM_CLOCK = 125000            # 2MHz / 16: tone period unit
TICK_HZ = 50


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
                    elif kind == 0xB0 and a == 7:
                        events.append((tick, "vol", ch, b2))
                    elif kind == 0xE0:
                        bend = ((b2 << 7) | a) - 8192
                        events.append((tick, "bend", ch, bend))
                elif kind in (0xC0, 0xD0):
                    j += 1
        i = end
    # stable sort; note-offs before note-ons at the same tick
    order = {"tempo": 0, "off": 1, "vol": 2, "bend": 3, "on": 4}
    events.sort(key=lambda e: (e[0], order[e[1]]))
    return division, events


def midi_to_frames(path):
    """Render a MIDI file to a list of 14-byte YM register frames."""
    division, events = parse_smf(path)

    # convert ticks to 50Hz frames through the tempo map
    us_per_qn = 500000
    frames_ev = []
    last_tick = 0
    t_us = 0.0
    for ev in events:
        t_us += (ev[0] - last_tick) * us_per_qn / division
        last_tick = ev[0]
        if ev[1] == "tempo":
            us_per_qn = ev[2]
            continue
        frames_ev.append((int(t_us * TICK_HZ / 1e6),) + ev[1:])
    if not frames_ev:
        die("no notes in MIDI file")
    total = max(f[0] for f in frames_ev) + TICK_HZ // 2

    chvol = [100] * 16
    chbend = [0] * 16
    notes = []          # active: [serial (age), ch, note, vel]
    voices = [None, None, None]   # index into notes-list entries
    drum = None         # (frames_left, noise_period, volume)
    serial = 0
    ei = 0
    out = []

    def period_of(ch, note):
        freq = 440.0 * 2.0 ** ((note - 69 + chbend[ch] / 4096.0) / 12.0)
        p = int(round(YM_CLOCK / freq))
        return max(1, min(0xFFF, p))

    def volume_of(ch, vel):
        v = (vel / 127.0) * (chvol[ch] / 127.0)
        return max(1, min(15, int(round(15 * v ** 0.5))))

    for frame in range(total):
        # apply this frame's MIDI events
        while ei < len(frames_ev) and frames_ev[ei][0] <= frame:
            ev = frames_ev[ei]
            ei += 1
            kind = ev[1]
            if kind == "vol":
                chvol[ev[2]] = ev[3]
            elif kind == "bend":
                chbend[ev[2]] = ev[3]
            elif kind == "on":
                ch, note, vel = ev[2], ev[3], ev[4]
                if ch == 9:
                    # percussion -> noise burst: low drums rumble,
                    # cymbals hiss
                    np = 25 if note in (35, 36) else \
                         (12 if note in (38, 40) else 3)
                    drum = [4, np, volume_of(ch, vel)]
                else:
                    ent = [serial, ch, note, vel]
                    serial += 1
                    notes.append(ent)
                    # newest-note priority: free voice, else steal
                    # the voice holding the oldest note
                    if None in voices:
                        voices[voices.index(None)] = ent
                    else:
                        old = min(voices, key=lambda e: e[0])
                        voices[voices.index(old)] = ent
            elif kind == "off":
                ch, note = ev[2], ev[3]
                for ent in notes:
                    if ent[1] == ch and ent[2] == note:
                        notes.remove(ent)
                        if ent in voices:
                            v = voices.index(ent)
                            voices[v] = None
                            # revive the newest unassigned note
                            spare = [e for e in notes
                                     if e not in voices]
                            if spare:
                                voices[v] = max(spare,
                                                key=lambda e: e[0])
                        break

        # compose the register frame
        regs = [0] * 14
        mix = 0x3F                       # all off (1 = disabled)
        for v in range(3):
            ent = voices[v]
            if ent is not None:
                p = period_of(ent[1], ent[2])
                regs[2 * v] = p & 0xFF
                regs[2 * v + 1] = (p >> 8) & 0x0F
                regs[8 + v] = volume_of(ent[1], ent[3])
                mix &= ~(1 << v)         # tone on
        if drum is not None:
            # drum overlays channel C: noise replaces its tone
            regs[6] = drum[1]            # noise period
            regs[8 + 2] = max(1, drum[2] - (4 - drum[0]) * 3)
            mix |= (1 << 2)              # C tone off
            mix &= ~(1 << 5)             # C noise on
            drum[0] -= 1
            if drum[0] <= 0:
                drum = None
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
