#!/usr/bin/env python3
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: LGPL-2.1-or-later
"""
The art test_bank8conv.c loads: source images made here with Pillow,
a bank of them made by stdlconv with every 8-plane option, and what
each loaded pixel must be, worked out here and not by stdlconv (its
ST mapping is the one thing both compute: done again plainly below).

Writes test_bank8conv.dir/: BANK8.BNK, and raw files of
"u16 w, u16 h, w*h bytes" (big-endian): B.IDX the 256-colour art,
BST.IDX its ST mapping, A.IDX the art over the ST's; PAL8.RGB and
PAL4.RGB the palettes, 3 bytes an entry. Exits 2 without Pillow.
"""

import os
import random
import struct
import subprocess
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit(2)

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "test_bank8conv.dir")
CONV = os.path.join(HERE, "..", "..", "tools", "stdlconv", "stdlconv.py")
W, H = 48, 7                    # three sprite frames of 16

rnd = random.Random(808)


def indexed(path, px, pal, size=(W, H)):
    img = Image.new("P", size)
    flat = []
    for c in pal:
        flat += list(c)
    img.putpalette(flat + [0] * (768 - len(flat)))
    img.putdata(px)
    img.save(path)


def raw(path, px):
    with open(path, "wb") as f:
        f.write(struct.pack(">HH", W, H) + bytes(px))


def rgb(path, pal):
    with open(path, "wb") as f:
        for c in pal:
            f.write(bytes(c))


os.makedirs(OUT, exist_ok=True)

# the ST's 16, and 256 distinct colours for the TT's own art
st16 = [(i * 17 % 256, i * 53 % 256, i * 97 % 256) for i in range(16)]
pal8 = [(i, (i * 7) % 256, 255 - i) for i in range(256)]
b = [200 if rnd.randrange(6) == 0 else rnd.randrange(256)
     for _ in range(W * H)]
# over the ST's: index = shade << 4 | ST colour, ST colour 5 the key
a = [(rnd.randrange(16) << 4) | (5 if rnd.randrange(6) == 0
                                 else rnd.randrange(16))
     for _ in range(W * H)]

# B's ST mapping, plainly: the key's nearest ST colour kept for it
def near(c, skip=None):
    best = min((sum((x - y) ** 2 for x, y in zip(c, t)), i)
               for i, t in enumerate(st16) if i != skip)
    return best[1]

tkey = near(pal8[200])
bst = [tkey if v == 200 else near(pal8[v], tkey) for v in b]

# the ST's palette as a 16x1 image, the map= stdlconv is given
indexed(os.path.join(OUT, "st16.png"), list(range(16)), st16, (16, 1))
indexed(os.path.join(OUT, "b.png"), b, pal8)
indexed(os.path.join(OUT, "a.png"), a, pal8)

d = lambda n: os.path.join(OUT, n)
specs = [
    "palette:0:" + d("st16.png"),
    "palette:0:" + d("b.png") + ":depth=8",
    "surface:1:" + d("b.png") + ":map=" + d("st16.png") + ":key=200",
    "surface:1:" + d("b.png") + ":depth=8:key=200",
    "surface:2:" + d("a.png") + ":low:key=5",
    "surface:2:" + d("a.png") + ":depth=hi",
    "sprite:3:" + d("b.png") + ":map=" + d("st16.png") + ":key=200"
    ":framew=16",
    "sprite:3:" + d("b.png") + ":depth=8:key=200:framew=16",
    "sprite:4:" + d("a.png") + ":low:key=5:framew=16",
    "sprite:4:" + d("a.png") + ":depth=hi:framew=16",
]
subprocess.run([sys.executable, CONV, "bank", d("BANK8.BNK")] + specs,
               check=True, stdout=subprocess.DEVNULL)
raw(d("B.IDX"), b)
raw(d("BST.IDX"), bst)
raw(d("A.IDX"), a)
rgb(d("PAL8.RGB"), pal8)
rgb(d("PAL4.RGB"), st16)
