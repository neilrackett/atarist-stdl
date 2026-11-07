#!/usr/bin/env python3
# STDL - Planar Display Library for Atari ST
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: LGPL-2.1-or-later
"""
SDL2BMP's art (tests/hatari/sdl2bmp.c): a 320x240 picture of a few
thousand colours and a sheet of three shaded balls on a transparent
background, drawn here with Pillow and converted together by
`stdlconv bmp8` as a port would convert its own, sharing one palette:

    stdlconv bmp8 S2BPIC.png S2BSPR.png -o dist --tt --zero 000000 \\
        --key FF00FF

so the palette is what a TT shows, index 0 is black (the border, with
no border index), and the balls' background is the key, index 255.
Then prints what SDL2BMP must print for them and for the core's 4-bit
SAIL.BMP (in dist/ after `make`), each file's pixels and palette as
Pillow reads them, hashed as SDL2BMP hashes them: compare the two.
"""

import math
import os
import subprocess
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..", "..")
DIST = os.path.join(ROOT, "dist")
SRC = os.path.join(ROOT, "obj", "sdl2bmp")
os.makedirs(SRC, exist_ok=True)

pic = Image.new("RGB", (320, 240))
d = ImageDraw.Draw(pic)
for y in range(240):
    t = min(y / 149, 1.0)
    d.line([(0, y), (319, y)],
           fill=(int(20 + 150 * t), int(40 + 140 * t), int(120 + 100 * t)))
for r in range(40, 0, -1):
    t = r / 40
    d.ellipse([240 - r, 60 - r, 240 + r, 60 + r],
              fill=(255, int(250 - 90 * t), int(190 - 170 * t)))
for i in range(4):
    top = [(x, 150 + 20 * math.sin(x / 37 + i * 1.7) + i * 18)
           for x in range(0, 321, 4)]
    d.polygon(top + [(320, 240), (0, 240)],
              fill=(30 + i * 15, 110 - i * 15, 40 + i * 5))
for y in range(150, 240):
    for x in range(320):
        r, g, b = pic.getpixel((x, y))
        k = 1.0 - (y - 150) / 180
        pic.putpixel((x, y), (int(r * k), int(g * k), int(b * k)))
d.text((12, 12), "SDL_LoadBMP: stdlconv bmp8, one palette", fill=(255, 255, 255))
pic.save(os.path.join(SRC, "S2BPIC.png"))

spr = Image.new("RGBA", (96, 32), (0, 0, 0, 0))
for f, hue in enumerate(((230, 60, 50), (60, 120, 240), (240, 200, 40))):
    for r in range(14, 0, -1):
        k = 0.45 + 0.55 * (1 - r / 14)
        c = tuple(min(255, int(v * k + 120 * (1 - r / 14))) for v in hue)
        ImageDraw.Draw(spr).ellipse([f * 32 + 16 - r - 2, 16 - r - 2,
                                     f * 32 + 16 + r - 2, 16 + r - 2],
                                    fill=c + (255,))
spr.save(os.path.join(SRC, "S2BSPR.png"))

subprocess.run([sys.executable,
                os.path.join(ROOT, "tools", "stdlconv", "stdlconv.py"),
                "bmp8", os.path.join(SRC, "S2BPIC.png"),
                os.path.join(SRC, "S2BSPR.png"), "-o", DIST, "--tt",
                "--zero", "000000", "--key", "FF00FF"],
               check=True, stdout=sys.stderr)


def fnv(data, h=0x811C9DC5):
    for b in data:
        h = ((h ^ b) * 0x01000193) & 0xFFFFFFFF
    return h


for name in ("S2BPIC.BMP", "S2BSPR.BMP", "SAIL.BMP"):
    path = os.path.join(DIST, name)
    raw = open(path, "rb").read()
    bits = raw[28]
    n = int.from_bytes(raw[46:50], "little") or 1 << bits
    im = Image.open(path)
    pal = (im.getpalette() + [0] * 768)[: n * 3]
    h = fnv(bytes(im.getdata()))
    h = fnv(bytes(pal), h)
    print("S2B:%s %dx%d n=%d hash=%08lx" % (name, im.width, im.height, n, h))
