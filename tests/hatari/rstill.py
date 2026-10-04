#!/usr/bin/env python3
# STDL - Planar Display Library for Atari ST
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# How still are RASTCHK's raster stripes? Reads a capture of its hold
# (tests/hatari/rastchk.c: orange and dark-blue stripes in colour 0,
# which runs through the border), takes the frames that show them,
# and counts those whose stripes - read down the left border - differ
# from the middle one. A raster edge a line late shows as a difference.
#
#   tests/hatari/rstill.py CAPTURE.avi
#
# Needs ffmpeg/ffprobe and numpy.
import subprocess
import sys

import numpy as np


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
        f = np.frombuffer(b, np.uint8).reshape(h, w, 3)
        col = f[:, 4].astype(int)
        yield (col[:, 0] > 200) & (col[:, 1] > 100) & (col[:, 2] < 60)


hold = [o for o in frames(sys.argv[1]) if o.sum() > 100]
hold = hold[10:-3]          # the switch to the table, and the close
ref = hold[len(hold) // 2]
bad = sum(1 for o in hold if not np.array_equal(o, ref))
print("%d hold frames, %d differ from the middle one" % (len(hold), bad))
