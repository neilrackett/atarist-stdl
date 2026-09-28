#!/usr/bin/env python3
# STDL - Planar Display Library for Atari ST
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Compare two SPRCOST logs (tests/hatari/sprcost.c): per-case ratio
# new/old, and the geometric mean over every case - the only number
# to quote. Beside it, each run's own noise floor: the geometric
# spread between its pass A and pass B, which ran the same binary.
# A difference inside the floors is not a difference.
#
#   tests/hatari/sccmp.py OLD.log NEW.log [column] [filter]
#
# column is "cpu" (default) or "lib"; filter keeps only case labels
# containing that text, e.g. "restore" or "spr ".
import math
import re
import sys

LINE = re.compile(r'^SC:([ABO]) (.+?)\s+(?:lib=\s*([\d.]+) )?cpu=\s*([\d.]+)')


def load(path, col):
    runs = {}
    for line in open(path, errors='replace'):
        m = LINE.match(line.strip())
        if not m:
            continue
        p, label, lib, cpu = m.groups()
        v = float(lib) if (col == 'lib' and lib) else float(cpu)
        runs.setdefault(p, {})[label.strip()] = v
    return runs


def gmean(xs):
    xs = [x for x in xs if x > 0]
    return math.exp(sum(math.log(x) for x in xs) / len(xs)) if xs else 1.0


def floor(runs, keep):
    a, b = runs.get('A', {}), runs.get('B', {})
    rs = [max(a[k], b[k]) / min(a[k], b[k]) for k in a
          if k in b and keep(k) and min(a[k], b[k]) > 0]
    return gmean(rs)


def main():
    old_p, new_p = sys.argv[1], sys.argv[2]
    col = sys.argv[3] if len(sys.argv) > 3 else 'cpu'
    filt = sys.argv[4] if len(sys.argv) > 4 else ''
    old, new = load(old_p, col), load(new_p, col)
    keep = (lambda k: filt in k)
    ratios = []
    for label in old.get('A', {}):
        if not keep(label):
            continue
        o = [old[p][label] for p in 'AB' if label in old.get(p, {})]
        n = [new[p][label] for p in 'AB' if label in new.get(p, {})]
        if not o or not n:
            continue
        om, nm = sum(o) / len(o), sum(n) / len(n)
        r = nm / om if om > 0 else 1.0
        ratios.append(r)
        print('%-30s %10.1f %10.1f  %6.3f' % (label, om, nm, r))
    if not ratios:
        print('no matching cases')
        return
    print('-' * 62)
    print('geometric mean new/old over %d cases: %.4f' %
          (len(ratios), gmean(ratios)))
    print('same-binary floor (A vs B): old %.4f, new %.4f' %
          (floor(old, keep), floor(new, keep)))


if __name__ == '__main__':
    main()
