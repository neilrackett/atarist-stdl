#!/bin/bash
# STDL - Planar Display Library for Atari ST
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Map a crash PC from Hatari's exception report to a symbol:
#
#   tests/hatari/map-crash.sh PROG.elf MAIN_RUNTIME PC [PC...]
#
# PROG.elf     the UNSTRIPPED linked program
# MAIN_RUNTIME the runtime address of main, in hex - have the
#              program print it once at startup:
#                  printf("main=%p\n", (void *)&main);
# PC           the PC=$xxxxx value from Hatari's
#              "Address Error"/"Bus Error" line on stderr
#
# TOS programs relocate, so runtime = file address + one constant;
# main's runtime address pins the constant and every other symbol
# follows. Local .L labels are skipped so the answer is the
# enclosing function.
set -eu
NM=${NM:-m68k-atari-mint-nm}
ELF=$1
shift

"$NM" -n "$ELF" | python3 -c '
import sys

main_rt = int(sys.argv[1], 16)
pcs = [int(a, 16) for a in sys.argv[2:]]
syms = []
for line in sys.stdin:
    p = line.split()
    if len(p) == 3 and p[1] in "Tt" and not p[2].startswith(".L"):
        syms.append((int(p[0], 16), p[2]))
syms.sort()
main_file = next(a for a, n in syms if n == "_main")
delta = main_rt - main_file
for pc in pcs:
    t = pc - delta
    best = None
    for a, n in syms:
        if a > t:
            break
        best = (a, n)
    if best:
        print("0x%x -> %s +0x%x" % (pc, best[1], t - best[0]))
    else:
        print("0x%x -> below first symbol" % pc)
' "$@"
