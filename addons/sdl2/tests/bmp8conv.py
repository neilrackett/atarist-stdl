#!/usr/bin/env python3
# STDL - Planar Display Library for Atari ST
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: LGPL-2.1-or-later
"""
The files test_bmp.c loads with SDL_LoadBMP: images made here with
Pillow and converted by `stdlconv bmp8` with each of its options, and
the 4-bit BMPs in examples/assets. What each must load as is read back
by Pillow's own BMP reader, not by stdlconv or the loader. Then what
bmp8 promises is checked here: one palette in every file of a set,
the key's index holding exactly the key's pixels and nothing else,
index 0's colour, a TT's grid, the border index unused and black, art
that fits kept exact, indexed art keeping its indices, and the palette
file.

Writes test_bmp.dir/: the BMPs in a directory a set, for each a .EXP
of "u16 w, u16 h, u16 ncolors (little-endian), ncolors * (r, g, b),
w * h indices", and LIST, a "BMP EXP" pair a line. Exits 2 without
Pillow, 1 when a promise is broken.
"""

import math
import os
import random
import struct
import subprocess
import sys

try:
    from PIL import Image, ImageDraw
except ImportError:
    sys.exit(2)

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..", "..")
OUT = os.path.join(HERE, "test_bmp.dir")
CONV = os.path.join(ROOT, "tools", "stdlconv", "stdlconv.py")
rnd = random.Random(808)
failures = []
pairs = []


def check(ok, what):
    if not ok:
        failures.append(what)


def bmp8(outdir, inputs, *opts):
    os.makedirs(outdir, exist_ok=True)
    subprocess.run([sys.executable, CONV, "bmp8", "-o", outdir]
                   + list(inputs) + list(opts),
                   check=True, stdout=subprocess.DEVNULL)


def expect(bmp, exp=None):
    """The .EXP for bmp (beside it, or exp), from Pillow's reading and
    the header's own colour count; returns (w, h, indices, palette)."""
    exp = exp or os.path.splitext(bmp)[0] + ".EXP"
    raw = open(bmp, "rb").read()
    bits = struct.unpack_from("<H", raw, 28)[0]
    n = struct.unpack_from("<I", raw, 46)[0]
    if n == 0 or n > 256:
        n = 1 << bits
    im = Image.open(bmp)
    flat = im.getpalette()
    pal = [tuple(flat[i * 3 : i * 3 + 3]) for i in range(len(flat) // 3)]
    pal = (pal + [(0, 0, 0)] * 256)[:n]
    px = list(im.getdata())
    with open(exp, "wb") as f:
        f.write(struct.pack("<HHH", im.width, im.height, n))
        for c in pal:
            f.write(bytes(c))
        f.write(bytes(px))
    pairs.append((os.path.relpath(bmp, HERE), os.path.relpath(exp, HERE)))
    return im.width, im.height, px, pal


src = os.path.join(OUT, "src")
os.makedirs(src, exist_ok=True)

# --- set A: art that fits, a key and transparency, colour 0, --pal ----
# a 101x61 picture of 180 colours, odd width so rows are padded
pic = Image.new("RGB", (101, 61))
pic.putdata([((x * 9) % 256, (y * 4) % 256, ((x // 10) * 40 + y) % 256)
             for y in range(61) for x in range(101)])
d = ImageDraw.Draw(pic)
d.rectangle([5, 5, 20, 20], fill=(0, 0, 0))
pic = pic.quantize(colors=180, dither=Image.Dither.NONE).convert("RGB")
pic.save(os.path.join(src, "apic.png"))
# sprites with alpha, and pixels of the key's own colour
spr = Image.new("RGBA", (33, 17), (0, 0, 0, 0))
d = ImageDraw.Draw(spr)
d.ellipse([1, 1, 15, 15], fill=(220, 40, 40, 255), outline=(255, 255, 255, 255))
d.rectangle([18, 2, 31, 14], fill=(255, 0, 255, 255))
d.rectangle([20, 4, 29, 12], fill=(40, 200, 90, 255))
spr.save(os.path.join(src, "aspr.png"))
adir = os.path.join(OUT, "a")
bmp8(adir, [os.path.join(src, "apic.png"), os.path.join(src, "aspr.png")],
     "--key", "FF00FF=200", "--zero", "000000",
     "--pal", os.path.join(adir, "A.PAL"))
aw, ah, apx, apal = expect(os.path.join(adir, "APIC.BMP"))
sw, sh, spx, spal = expect(os.path.join(adir, "ASPR.BMP"))
check(apal == spal, "A: the two files' palettes differ")
check(apal[0] == (0, 0, 0), "A: index 0 is not --zero's colour")
check(apal[200] == (255, 0, 255), "A: the key's entry is not its colour")
for x, (s, i) in enumerate(zip(pic.getdata(), apx)):
    check(i != 200 and apal[i] == s, "A: picture pixel %d not exact" % x)
for x, (s, i) in enumerate(zip(spr.getdata(), spx)):
    key = s[3] < 128 or s[:3] == (255, 0, 255)
    check((i == 200) == key, "A: sprite pixel %d keyed wrongly" % x)
    check(key or apal[i] == s[:3], "A: sprite pixel %d not exact" % x)
pal = open(os.path.join(adir, "A.PAL"), "rb").read()
check(pal == b"".join(bytes(c + (255,)) for c in apal),
      "A: A.PAL is not the files' palette as r, g, b, 255")

# --- set B: more colours than fit, a TT's grid, a border index, and a
# key among colours near its own, none of which may land on it -------
plasma = Image.new("RGB", (37, 23))
plasma.putdata([(int(128 + 127 * math.sin(x / 3.0)),
                 int(128 + 127 * math.sin(y / 2.0 + x / 5.0)),
                 int(128 + 127 * math.cos(math.hypot(x - 18, y - 11) / 2.5)))
                for y in range(23) for x in range(37)])
d = ImageDraw.Draw(plasma)
d.line([(0, 3), (36, 3)], fill=(250, 4, 250))
d.line([(0, 4), (36, 4)], fill=(255, 0, 255))
d.line([(0, 5), (36, 5)], fill=(255, 0, 250))
plasma.save(os.path.join(src, "bplasma.png"))
bdir = os.path.join(OUT, "b")
bmp8(bdir, [os.path.join(src, "bplasma.png")], "--tt", "--border", "0xF0",
     "--key", "FF00FF=0x80")
bw, bh, bpx, bpal = expect(os.path.join(bdir, "BPLASMA.BMP"))
check(len(set(plasma.getdata())) > 256, "B: the picture fits; it should not")
check(all(v % 17 == 0 for c in bpal for v in c), "B: not on a TT's grid")
check(0xF0 not in bpx and bpal[0xF0] == (0, 0, 0),
      "B: the border index is used, or not black")
check([i == 0x80 for i in bpx] == [s == (255, 0, 255)
                                    for s in plasma.getdata()],
      "B: the key's index is not exactly the key's pixels")
err = max(max(abs(a - b) for a, b in zip(s, bpal[i]))
          for s, i in zip(plasma.getdata(), bpx) if i != 0x80)
check(err < 64, "B: a pixel %d off its colour" % err)   # 42 when written

# --- set C: indexed art through one palette keeps its indices -------
cpal = [rnd.randrange(256) for _ in range(256 * 3)]
cpal[3:6] = cpal[0:3]               # two entries alike: both kept
names = []
for n, (w, h) in enumerate(((19, 7), (8, 3))):
    im = Image.new("P", (w, h))
    im.putpalette(cpal)
    im.putdata([rnd.randrange(256) for _ in range(w * h)])
    im.info["transparency"] = 9
    name = os.path.join(src, "cidx%d.png" % n)
    im.save(name, transparency=9)
    names.append((name, list(im.getdata())))
cdir = os.path.join(OUT, "c")
bmp8(cdir, [p for p, _px in names])
for n, (p, px) in enumerate(names):
    _w, _h, got, gpal = expect(os.path.join(cdir, "CIDX%d.BMP" % n))
    check(got == px, "C: indices not kept")
    check([v for c in gpal for v in c] == cpal, "C: palette not kept")

# --- set D: 32 colours, dithered, which must leave the key's index to
# the key's pixels and the border index to none, though dithering
# reaches for the nearest entries of all ---------------------------
ddir = os.path.join(OUT, "d")
bmp8(ddir, [os.path.join(src, "bplasma.png")], "--colors", "32", "--dither",
     "--zero", "102030", "--key", "FF00FF", "--border", "0x10")
dw, dh, dpx, dpal = expect(os.path.join(ddir, "BPLASMA.BMP"))
check(len(dpal) <= 32 and max(dpx) < 32, "D: more than 32 colours")
check(dpal[0] == (0x10, 0x20, 0x30), "D: index 0 is not --zero's colour")
check([i == 31 for i in dpx] == [s == (255, 0, 255)
                                  for s in plasma.getdata()],
      "D: the key's index (31, the last) is not exactly the key's pixels")
check(0x10 not in dpx and dpal[0x10] == (0, 0, 0),
      "D: the border index is used, or not black")

# --- the 4-bit BMPs the core's examples load, as other tools wrote them
for name in ("ICON", "SAIL", "SAMPLE"):
    expect(os.path.join(ROOT, "examples", "assets", name + ".BMP"),
           os.path.join(OUT, name + ".EXP"))

with open(os.path.join(OUT, "LIST"), "w") as f:
    for b, e in pairs:
        f.write("%s %s\n" % (b, e))
for what in failures[:12]:
    print("bmp8conv: " + what)
sys.exit(1 if failures else 0)
