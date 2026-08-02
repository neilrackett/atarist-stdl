#!/usr/bin/env python3
# STDL - Planar Display Library for Atari ST
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: LGPL-2.1-or-later
"""
Regenerate the demo audio assets in examples/assets/:

  DEMO.MID - deterministic test tune (arpeggio melody, sustained
             bass, hats) used by PLAYMUS/SFXDEMO and designed for
             spectral verification of the YM output
  BEEP.WAV - 0.4s 880Hz square at 12517Hz u8 mono, the DMA chunk
             PLAYMUS fires over the music

DEMO.STM is then produced from DEMO.MID with:
  python3 tools/stdlconv/stdlconv.py midi examples/assets/DEMO.MID \
          examples/assets/DEMO.STM

Pure stdlib - no Pillow needed.
"""

import math
import os
import struct

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.join(HERE, "..", "examples", "assets")

PPQN = 96


def varint(v):
    out = bytearray([v & 0x7F])
    v >>= 7
    while v:
        out.insert(0, 0x80 | (v & 0x7F))
        v >>= 7
    return bytes(out)


def make_midi(path):
    events = []
    # tempo 120bpm -> 500000us/qn -> 2 qn = 1s
    events.append((0, b"\xFF\x51\x03" + struct.pack(">I", 500000)[1:]))

    # melody (channel 0): A3 C4 E4 A4 C5 E5 A5 A4, one second each
    melody = [57, 60, 64, 69, 72, 76, 81, 69]
    for i, n in enumerate(melody):
        t0 = i * 2 * PPQN
        t1 = t0 + 2 * PPQN - 8
        events.append((t0, bytes([0x90, n, 100])))
        events.append((t1, bytes([0x80, n, 0])))

    # bass (channel 1): sustained A2 the whole way
    events.append((0, bytes([0x91, 45, 90])))
    events.append((16 * PPQN, bytes([0x81, 45, 0])))

    # hats (channel 10 = drums): every half second
    for i in range(16):
        t = i * PPQN
        events.append((t, bytes([0x99, 42, 80])))
        events.append((t + 12, bytes([0x89, 42, 0])))

    events.sort(key=lambda e: e[0])
    track = bytearray()
    last = 0
    for t, data in events:
        track += varint(t - last) + data
        last = t
    track += varint(0) + b"\xFF\x2F\x00"

    with open(path, "wb") as f:
        f.write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, PPQN))
        f.write(b"MTrk" + struct.pack(">I", len(track)) + bytes(track))
    print("wrote %s (%d track bytes)" % (path, len(track)))


def make_beep(path):
    rate = 12517
    n = int(rate * 0.4)
    data = bytearray()
    for i in range(n):
        v = 60 if math.sin(2 * math.pi * 880 * i / rate) >= 0 else -60
        env = min(1.0, (n - i) / (rate * 0.05))
        data.append(128 + int(v * env))
    if len(data) & 1:
        data.append(128)
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE")
        f.write(b"fmt " + struct.pack("<IHHIIHH", 16, 1, 1, rate,
                                      rate, 1, 8))
        f.write(b"data" + struct.pack("<I", len(data)))
        f.write(data)
    print("wrote %s (%d samples)" % (path, len(data)))


if __name__ == "__main__":
    make_midi(os.path.join(ASSETS, "DEMO.MID"))
    make_beep(os.path.join(ASSETS, "BEEP.WAV"))
