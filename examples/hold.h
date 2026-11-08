/*
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: CC0-1.0
 *
 * hold.h: keep a finished program's results where they can be read.
 *
 * A program that runs to its end by itself goes straight back to the
 * desktop on a real machine, and what it printed goes with it: a check
 * run from the desktop and come back to later shows nothing. Call
 * hold_on_exit() first thing in main, and every way out - main's
 * return, exit(), an error - says "Press a key." and waits for one,
 * once STDL has given the keyboard back to TOS.
 *
 * What a program prints while it has a screen of its own is gone once
 * STDL gives the desktop back (TRIPCHK's results were, in Hatari), so
 * hold_on_exit("NAME") also keeps a copy of everything printed to
 * stdout and stderr - which still reach the console at once, as before
 * - and writes it to NAME.LOG beside the program at the end, the
 * prompt saying so. hold_on_exit(NULL) holds without a log.
 *
 * A program started with a command line (by a test driver, or another
 * program, as tests/hatari/mousebk.c starts JOYCHK) is neither held
 * nor logged. Nor does Hatari's tests/hatari/run.sh need a key: it
 * ends the emulator itself once its script is done, and a program's
 * output reaches its console log as it is printed.
 *
 * Define HOLD_NO_STDL before including it in a program that links no
 * libstdl.a (it then leaves the keyboard to the program, which must
 * have given it back before it returns).
 */

#ifndef STDL_EXAMPLES_HOLD_H
#define STDL_EXAMPLES_HOLD_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mint/basepage.h>
#include <mint/osbind.h>
#ifndef HOLD_NO_STDL
#include <stdl/stdl.h>
#endif

/* mintlib's, declared only under _GNU_SOURCE, which a program has not
 * asked for by the time it includes this; the type is there either way */
extern FILE *fopencookie(void *cookie, const char *modes,
                         __io_functions funcs);

static const char *hold_name;
static FILE *hold_out, *hold_err;       /* the console's own streams */
static char *hold_copy;
static size_t hold_len, hold_room;

static __ssize_t hold_write(void *cookie, const char *buf, size_t n)
{
    FILE *to = (FILE *)cookie;

    fwrite(buf, 1, n, to);
    if (to == hold_err) {
        fflush(to);             /* stderr was unbuffered: keep it so */
    }
    if (hold_len + n > hold_room) {
        size_t room = hold_room ? hold_room * 2 : 4096;
        char *more;

        while (room < hold_len + n) {
            room *= 2;
        }
        more = realloc(hold_copy, room);
        if (more == NULL) {
            return (__ssize_t)n;        /* printed, just not kept */
        }
        hold_copy = more;
        hold_room = room;
    }
    memcpy(hold_copy + hold_len, buf, n);
    hold_len += n;
    return (__ssize_t)n;
}

static int hold_fileno(void *cookie)
{
    return fileno((FILE *)cookie);
}

static void hold_wait(void)
{
#ifndef HOLD_NO_STDL
    STDL_Quit();                /* the keyboard back to TOS, if not yet */
#endif
    if (hold_name != NULL) {
        char path[16];
        FILE *f;

        fflush(stdout);
        fflush(stderr);
        stdout = hold_out;
        stderr = hold_err;
        strncpy(path, hold_name, 8);
        path[8] = '\0';
        strcat(path, ".LOG");
        f = fopen(path, "w");
        if (f != NULL) {
            fwrite(hold_copy, 1, hold_len, f);
            fclose(f);
            printf("\r\nWritten to %s.", path);
        }
    }
    printf("\r\nPress a key.\r\n");
    fflush(stdout);
    while (Cconis() != 0) {     /* keys pressed while it ran */
        Cconin();
    }
    Cconin();
}

/* one of the two console streams, copied as well as printed */
static FILE *hold_tee(FILE *to, int mode)
{
    const __io_functions io = { NULL, hold_write, NULL, NULL, hold_fileno };
    FILE *f = fopencookie(to, "w", io);

    if (f == NULL) {
        return to;
    }
    setvbuf(f, NULL, mode, mode == _IONBF ? 0 : 512);
    return f;
}

static __inline__ void hold_on_exit(const char *name)
{
    if (_base->p_cmdlin[0] != 0) {      /* started with a command line */
        return;
    }
    if (name != NULL) {
        hold_name = name;
        hold_out = stdout;
        hold_err = stderr;
        stdout = hold_tee(hold_out, _IOLBF);
        stderr = hold_tee(hold_err, _IONBF);
    }
    atexit(hold_wait);
}

#endif /* STDL_EXAMPLES_HOLD_H */
