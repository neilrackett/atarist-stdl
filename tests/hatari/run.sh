#!/bin/bash
# STDL - Planar Display Library for Atari ST
# Copyright (C) 2026 Neil Rackett
# SPDX-License-Identifier: LGPL-2.1-or-later
#
# On-target test driver: boots a .TOS program in Hatari with the
# console captured, then executes a ;-separated command script
# against the emulator's command fifo.
#
#   tests/hatari/run.sh NAME PROG.TOS BOOT_WAIT "cmd;cmd;..."
#
# Commands:
#   sleep N       wait N seconds (fractions ok)
#   waitfor STR   poll the console log for STR (stderr prints are
#                 immediate; stdout is line-buffered)
#   waitfile STR PATH  poll a file (e.g. a log the program writes on
#                 the GEMDOS drive) for STR - deterministic sync for
#                 programs whose output does not reach the console
#   shot          screenshot -> tests/hatari/out/shots_NAME/
#   key VAL       press+release an ST scancode (0x01 = ESC) or a
#                 single alphanumeric character
#   keydown VAL / keyup VAL
#   click         double left mouse click
#   text STR      type a string
#   fifo CMD...   send a raw command to Hatari's fifo (e.g.
#                 'fifo hatari-shortcut recsound' to toggle sound
#                 recording; the output file comes from Hatari's
#                 SoundFileName config, ./hatari.wav by default)
#
# Console output lands in tests/hatari/out/NAME.log and is echoed
# at the end. When the script is done the emulator gets a TERM, and
# a -9 ten seconds later if it is still there (said on stderr).
# SDL_VIDEODRIVER=dummy runs it with no window, where no key typed
# on the desktop can reach it. Environment overrides:
#   HATARI  emulator binary   (default: mac app bundle path)
#   TOS     TOS/EmuTOS image  (required if no usable default)
#   FF      fast-forward on|off (default on; use off when taking
#           timed screenshots)
#   MACHINE hatari machine type (default ste: an 8MHz CPU, so the
#           timing is honest, with the blitter and DMA audio there
#           to exercise. Use st for the correctness floor, and
#           megaste to measure what 16MHz adds - and as the only
#           setting under which STDL_UseMegaSteSpeedup does
#           anything: Hatari honours writes to $FFFF8E21 and really
#           does change clock, so speed-dependent code is testable
#           here, not only on hardware. Do not pass --cpuclock as
#           well, which pins it). tt and falcon want a 512K TOS
#           (EmuTOS's etos512*.img, or TOS 3.06 and 4.04), and get
#           --mmu off, and on a falcon --fpu none, after --machine,
#           which LATE can still change
#   MONITOR hatari monitor: mono, rgb, vga or tv (default Hatari's
#           own, rgb in a fresh configuration). A Falcon wants both
#           vga and rgb: they give it different modes, RGB's line
#           384 pixels with the picture centred
#   SOUND   off, on, or a frequency in Hz (default off). "on" means
#           44100. Recording is a separate step: send
#           'fifo hatari-shortcut recsound' to start and again to
#           stop, and the file lands wherever szYMCaptureFileName
#           points in the Hatari config - which must be somewhere
#           writable, so pass EXTRA="--configfile <copy>" with that
#           key rewritten rather than assuming the default.
#   EXTRA   extra hatari options, placed before this script's own
#           (so a --configfile in it cannot undo them)
#   LATE    extra hatari options placed after them, just before the
#           program: for what --machine resets, such as --fpu 68882,
#           and TT-RAM, "--ttram 4 --addr24 off" (refused without the
#           --addr24 off, which would leave Hatari waiting on a question)
set -u
NAME=$1
PROG=$2
BOOT_WAIT=$3
SCRIPT=$4

HERE="$(cd "$(dirname "$0")" && pwd)"
OUT=$HERE/out
HATARI=${HATARI:-/Applications/Hatari.app/Contents/MacOS/hatari}
FF=${FF:-on}
MACHINE=${MACHINE:-ste}
SOUND=${SOUND:-off}
# Hatari's --sound takes off or a frequency, and rejects "on" - which
# is what this script's own documentation said for a long time, so a
# run asking for sound died in argument parsing with an empty log.
if [ "$SOUND" = "on" ]; then
    SOUND=44100
fi
FIFO=$OUT/fifo_$NAME
SHOTDIR=$OUT/shots_$NAME
LOG=$OUT/$NAME.log

# Clear last run's output BEFORE anything can exit, so a run that
# never starts cannot leave a previous log looking like its result.
# (A missing $TOS used to bail here through ${TOS:?...}; under an
# output redirection that is silent, and on a case-insensitive
# filesystem a differently-cased NAME then resolved to a month-old
# log that read as a pass.)
rm -rf "$SHOTDIR" "$FIFO" "$LOG"
mkdir -p "$SHOTDIR"

if [ -z "${TOS:-}" ]; then
    echo "$0: set TOS to a TOS/EmuTOS image path (see 'Testing in" \
         "Hatari' in .claude/skills/stdl/SKILL.md for a download)" >&2
    exit 2
fi
if [ ! -f "$TOS" ]; then
    echo "$0: TOS image not found: $TOS" >&2
    exit 2
fi
if [ ! -f "$PROG" ]; then
    echo "$0: program not found: $PROG" >&2
    exit 2
fi
# TT-RAM wants 32-bit addressing: without it Hatari stops at a question
# before TOS starts, which no window answers, and the program never
# runs (under EmuTOS and TOS 3.06 alike) - only the kill below ends it
case " ${EXTRA:-} ${LATE:-} " in
  *" --ttram "[1-9]*)
    case " ${EXTRA:-} ${LATE:-} " in
      *" --addr24 off "*) ;;
      *) echo "$0: --ttram needs --addr24 off as well (in LATE)" >&2
         exit 2 ;;
    esac ;;
esac

# A TT's and a Falcon's own. Hatari 2.6.1's --machine tt keeps the
# configuration's MMU setting (a configuration saying on stays on),
# and an emulated MMU cost TTCHK 46-47s of the host's time under
# fast-forward against 29-32 without, with no truer timing;
# --machine falcon already drops the FPU there, as a stock Falcon
# has none, and saying so keeps it so on other versions.
case $MACHINE in
  tt)     MACHOPTS="--mmu off" ;;
  falcon) MACHOPTS="--mmu off --fpu none" ;;
  *)      MACHOPTS="" ;;
esac

# EXTRA goes first so this script's own options win: a
# --configfile in EXTRA (the only way to move the sound capture
# file) would otherwise replace the TOS and machine chosen here.
# LATE goes last, because --machine resets the CPU's options.
# --confirm-quit false: Hatari's own default asks before it quits, so
# with a fresh configuration (a HOME of its own) the TERM at the end
# waited on that question for ever, which no window was there to
# answer - on st, ste, tt and falcon alike, and with EmuTOS and TOS.
"$HATARI" ${EXTRA:-} --tos "$TOS" --machine "$MACHINE" $MACHOPTS \
  ${MONITOR:+--monitor "$MONITOR"} --fast-forward "$FF" \
  --confirm-quit false \
  --fast-boot on --sound "$SOUND" --statusbar off \
  --conout 2 --cmd-fifo "$FIFO" \
  --screenshot-dir "$SHOTDIR" --screenshot-format png ${LATE:-} \
  "$PROG" > "$LOG" 2>"$OUT/$NAME.err" &
HPID=$!

for i in $(seq 1 50); do
  [ -p "$FIFO" ] && break
  sleep 0.2
done

sleep "$BOOT_WAIT"

IFS=';' read -ra CMDS <<< "$SCRIPT"
for cmd in "${CMDS[@]}"; do
  set -- $cmd
  case $1 in
    sleep|wait) sleep "$2" ;;
    shot)       echo "hatari-shortcut screenshot" > "$FIFO" ;;
    key)        echo "hatari-event keypress $2" > "$FIFO" ;;
    keydown)    echo "hatari-event keydown $2" > "$FIFO" ;;
    keyup)      echo "hatari-event keyup $2" > "$FIFO" ;;
    text)       shift; echo "hatari-event text $*" > "$FIFO" ;;
    click)      echo "hatari-event doubleclick" > "$FIFO" ;;
    fifo)       shift; echo "$*" > "$FIFO" ;;
    waitfor)    shift
                for i in $(seq 1 240); do
                  grep -q "$*" "$LOG" && break
                  sleep 0.5
                done ;;
    waitfile)   str=$2; shift 2
                for i in $(seq 1 240); do
                  [ -f "$*" ] && grep -q "$str" "$*" && break
                  sleep 0.5
                done ;;
    *)          echo "unknown cmd: $cmd" >&2 ;;
  esac
done

sleep 2
kill $HPID 2>/dev/null
# An emulator that outlives the TERM left this script waiting on it
# for ever (61 of them over one session). With the quit unconfirmed
# 164 of 164 runs under tt and falcon ended on it; one held by
# another of Hatari's questions (--ttram without --addr24 off asks
# one before TOS starts) gets ten seconds, then -9.
for i in $(seq 1 20); do
  kill -0 $HPID 2>/dev/null || break
  sleep 0.5
done
if kill -0 $HPID 2>/dev/null; then
  echo "$0: hatari ignored TERM for 10s: killed" >&2
  kill -9 $HPID 2>/dev/null
fi
wait $HPID 2>/dev/null
echo "=== $NAME console output ==="
cat "$LOG"
echo "=== screenshots ==="
ls "$SHOTDIR" 2>/dev/null
