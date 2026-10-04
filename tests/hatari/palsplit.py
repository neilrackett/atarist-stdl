#!/usr/bin/env python3
# STDL - Planar Display Library for Atari ST
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Count the frames of a PALSTG capture (tests/hatari/palstg.c) that
# show colour 1 in two colours - a palette change that landed in the
# picture - per phase, the phase read from the corner marker's colour.
#
#   tests/hatari/palsplit.py CAPTURE.avi
#
# Needs ffmpeg/ffprobe and numpy.
import subprocess
import sys

import numpy as np

MARKS = {(255, 255, 255): 0, (255, 255, 0): 1, (0, 255, 255): 2,
         (255, 0, 255): 3}
NAMES = ["SetColours unstaged", "SetColours staged",
         "SDL PHYSPAL unstaged", "SDL PHYSPAL staged"]


def frames(path):
    w, h = map(int, subprocess.check_output(
        ['ffprobe', '-v', 'error', '-select_streams', 'v:0',
         '-show_entries', 'stream=width,height', '-of', 'csv=p=0',
         path]).decode().split(','))
    p = subprocess.Popen(['ffmpeg', '-v', 'error', '-i', path, '-f',
                          'rawvideo', '-pix_fmt', 'rgb24', '-'],
                         stdout=subprocess.PIPE)
    while True:
        b = p.stdout.read(w * h * 3)
        if len(b) < w * h * 3:
            break
        yield np.frombuffer(b, np.uint8).reshape(h, w, 3)


def main():
    split = [0] * 4
    whole = [0] * 4
    for f in frames(sys.argv[1]):
        col = f[:, f.shape[1] // 2].astype(int)
        red = ((col[:, 0] > 150) & (col[:, 2] < 100)).sum()
        blue = ((col[:, 2] > 150) & (col[:, 0] < 100)).sum()
        if red + blue < 100:
            continue
        # the marker: the picture's first pixel a few rows down - the
        # border round it is colour 0, black
        top = np.where((col[:, 0] > 150) | (col[:, 2] > 150))[0][0]
        row = f[top + 4].astype(int)
        lit = np.where(row.sum(axis=1) > 100)[0]
        if len(lit) == 0:
            continue
        k = tuple(255 if v > 200 else 0 for v in row[lit[0]])
        phase = MARKS.get(k)
        if phase is None:
            continue
        if red > 20 and blue > 20:
            split[phase] += 1
        else:
            whole[phase] += 1
    for i in range(4):
        print("%-22s %4d frames split, %4d whole" % (NAMES[i], split[i],
                                                    whole[i]))


main()
