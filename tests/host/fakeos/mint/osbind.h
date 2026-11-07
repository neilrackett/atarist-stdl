/*
 * STDL - Planar Display Library for Atari ST
 * Copyright (C) 2026 Neil Rackett
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Host tests: the TOS calls event.c and Xpad's consumer
 * (lib/xpad/src/xpad.c) make, for test_joystick, which alone puts this
 * directory on its include path.
 */

#ifndef STDL_HOST_OSBIND_H
#define STDL_HOST_OSBIND_H

#include <stdint.h>

void *Kbdvbase(void);
void *Keytbl(void *unshift, void *shift, void *caps);

/*
 * The block an Xpad cookie would point at, or NULL for no provider.
 * Supexec runs xpad.c's jar walk on the machine; a jar's 32-bit slot
 * cannot hold a host's pointer (Xpad's own host stub says the same),
 * so here the block comes straight back and xpad_find() validates it
 * as it would on the machine.
 */
extern void *stdl_host_xpad;
#define Supexec(fn) ((long)(intptr_t)((void)(fn), stdl_host_xpad))

#endif
