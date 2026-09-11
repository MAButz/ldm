/*
 * ldmpty.h - talking to a child process over a pseudo-terminal.
 *
 * Copyright (c) 2026 Marc-Andre Beckmann-Butz <ma@butz.online>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#ifndef _LDMPTY_H_
#define _LDMPTY_H_

#include <glib.h>

/*
 * Returned by ldm_pty_expect() instead of an index. Negative so they
 * cannot be confused with "I saw expectation number n".
 */
#define LDM_PTY_ERROR      -1
#define LDM_PTY_TIMED_OUT  -2

/* Size of the buffer ldm_pty_expect() fills; callers must provide this much. */
#define LDM_PTY_MAXBUF     4096

/*
 * Spawn argv on a fresh pty, returning its pid and putting the master
 * side in *masterfd. Returns 0 on failure.
 */
GPid ldm_pty_spawn(gchar **argv, int *masterfd);

/*
 * Read from fd until one of the NULL-terminated list of strings appears,
 * seconds elapse, or the child dies. Everything read is accumulated in p,
 * which must be LDM_PTY_MAXBUF bytes.
 *
 * Returns the index of the string that matched, or LDM_PTY_TIMED_OUT, or
 * LDM_PTY_ERROR. A timeout is reported separately from an error because
 * the two mean different things to a caller: nothing was said, versus the
 * conversation broke.
 */
int ldm_pty_expect(int fd, char *p, int seconds, ...);

#endif /* _LDMPTY_H_ */
