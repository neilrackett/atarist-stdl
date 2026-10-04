#!/bin/sh
# STDL - Planar Display Library for Atari ST
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Where does the compiler call libgcc for arithmetic?
#
#   libcalls.sh [-a ALLOW] OBJ...
#
# gcc 4.6 for the 68000 has no instruction for a 32-bit multiply or
# divide, so it calls __mulsi3, __divsi3 and friends - about 270 and
# 350 cycles at 8MHz, against 70 for a muls.w and 140 for a divu.w.
# Whether it does so depends on what it can prove about the operands'
# width at that point, so the same expression compiles to one
# instruction in one function and a library call in the next, and
# nothing in the source says which. Soft float is the same story at
# a larger price (__mulsf3, __floatsisf, __fixdfsi...).
#
# This prints every call the objects make to such a helper, one line
# per function and helper with the number of call sites:
#
#   object function helper count
#
# With -a it checks them against an allowlist in the same format
# (with `#` comments) and exits non-zero if a function calls a helper
# more often than its line allows - a new call is a decision someone
# has to write down, not something a rebuild quietly adds. A line
# whose count is now higher than needed is reported but does not
# fail. Static functions keep their names; gcc's clone suffixes
# (.constprop.1, .isra.0, .part.2) are dropped so the names survive
# a rebuild.
#
# Run it in the toolchain container: `stcmd sh tools/libcalls.sh
# build/*.o`. CROSS overrides the toolchain prefix.

objdump=${CROSS:-m68k-atari-mint-}objdump
allow=
if [ "$1" = "-a" ]; then
    allow=$2
    shift 2
fi
if [ $# -eq 0 ]; then
    echo "usage: $0 [-a ALLOW] OBJ..." >&2
    exit 2
fi
# a check that cannot see the code passes everything, so it must not
# run at all
command -v "$objdump" >/dev/null || { echo "$0: no $objdump" >&2; exit 2; }
for o in "$@"; do
    [ -r "$o" ] || { echo "$0: cannot read $o" >&2; exit 2; }
done
if [ -n "$allow" ] && [ ! -r "$allow" ]; then
    echo "$0: cannot read $allow" >&2
    exit 2
fi

found=$(for o in "$@"; do
    "$objdump" -dr "$o" | awk -v o="${o##*/}" '
        # a function starts at a symbol that is not a local label
        /^[0-9a-f]+ <[^.].*>:$/ {
            fn = substr($2, 2, length($2) - 3)
            sub(/^_/, "", fn)
            sub(/\.(constprop|isra|part|clone)\.[0-9]+$/, "", fn)
        }
        # a relocation against a libgcc helper: __name3, __name2 ...
        $NF ~ /^___[a-z]+[0-9]$/ { print o, fn, substr($NF, 2) }'
done | sort | uniq -c | awk '{ print $2, $3, $4, $1 }')

if [ -z "$allow" ]; then
    [ -n "$found" ] && echo "$found"
    exit 0
fi

echo "$found" | awk -v allowfile="$allow" '
    BEGIN {
        while ((getline line < allowfile) > 0) {
            sub(/#.*/, "", line)
            if (split(line, f) == 4) {
                ok[f[1] " " f[2] " " f[3]] = f[4] + 0
                lines++
            }
        }
        # an allowlist that reads as empty is a fault, not a policy:
        # a file replaced under the toolchain container has done it
        if (lines == 0) {
            print "*** " allowfile " read as empty" > "/dev/stderr"
            unreadable = 1
            exit 2
        }
    }
    NF == 4 {
        k = $1 " " $2 " " $3
        seen[k] = 1
        if (!(k in ok)) {
            printf "*** %s: %s calls %s (%d) - not allowed\n", $1, $2, $3, $4
            bad = 1
        } else if ($4 > ok[k]) {
            printf "*** %s: %s calls %s %d times, %d allowed\n", $1, $2, $3, $4, ok[k]
            bad = 1
        } else if ($4 < ok[k]) {
            printf "%s: %s calls %s %d times now, %d allowed - lower it\n", $1, $2, $3, $4, ok[k]
        }
        n += $4
    }
    END {
        if (unreadable)
            exit 2
        for (k in ok)
            if (!(k in seen))
                printf "%s: no longer called - drop the line\n", k
        if (bad) {
            print "*** new libgcc arithmetic calls: use a 16-bit form, or add"
            print "*** the line to " allowfile " with the reason it is not hot"
            exit 1
        }
        printf "libgcc arithmetic: %d call sites, all allowed\n", n
    }'
