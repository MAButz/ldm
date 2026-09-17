/*
 * ldmpty.c - talking to a child process over a pseudo-terminal.
 *
 * Some programs will only read a password from a terminal, by design: ssh
 * is the obvious one. Driving them means giving them a pty and watching
 * what they print, which is what ldm_pty_expect() does.
 *
 * This lived inside the ssh backend until the RDP backend needed the same
 * thing for its optional pre-authentication. It is shared rather than
 * copied because the interesting part is not the fifty lines of select()
 * loop but the handful of decisions inside it - that a timeout is not an
 * error, that a dead child has to be noticed even when it printed
 * something first, and that the caller wants to know *which* of several
 * strings it saw.
 *
 * Built into the ldm binary, which is linked -export-dynamic, so the
 * plugins resolve these symbols the same way they already resolve
 * log_entry() and child_exited.
 *
 * Copyright (c) 2026 Marc-Andre Beckmann-Butz <ma@butz.online>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 */

#include <config.h>
#include <errno.h>
#include <glib.h>
#include <pty.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#include <utmp.h>

#include "ldm.h"
#include "ldmutils.h"
#include "ldmpty.h"
#include "logging.h"

/*
 * The slave side of the pty being set up, for the child-side callback
 * below. A file-static is enough: ldm drives one such conversation at a
 * time, between the greeter collecting credentials and the session
 * starting.
 */
static int ldm_pty_slavefd = -1;

/*
 * Runs in the child, after fork and before exec. login_tty() makes the
 * slave side the controlling terminal, which is the whole point - a
 * program that asks "is this a terminal?" has to be able to answer yes,
 * and ssh refuses to read a password otherwise.
 */
static void
ldm_pty_child_init(gpointer user_data)
{
    (void) user_data;
    (void) setsid();
    if (login_tty(ldm_pty_slavefd) < 0) {
        _exit(127);
    }
}

/*
 * Spawn argv on a fresh pty. Returns the pid, with the master side in
 * *masterfd for ldm_pty_expect() and write() to work on.
 *
 * The parent closes the slave side deliberately: keeping it open means
 * the master never sees EOF when the child dies, and a caller waiting for
 * output would sit there until its timeout instead of being told. The ssh
 * backend keeps its own slave fd open on purpose, because its session
 * outlives this kind of one-shot exchange - which is why it still opens
 * its own pty rather than calling this.
 */
GPid
ldm_pty_spawn(gchar **argv, int *masterfd)
{
    int master = -1, slave = -1;
    GPid pid;

    if (openpty(&master, &slave, NULL, NULL, NULL) < 0) {
        log_entry("ldm", 3, "ldm_pty_spawn: openpty failed: %s",
                  strerror(errno));
        return 0;
    }

    ldm_pty_slavefd = slave;
    /*
     * Start from a clean slate. child_exited is one global, set by the
     * SIGCHLD handler for *any* child of ldm and cleared by nobody, while
     * the loop below reads it as "our child died". One earlier child was
     * therefore enough to poison every later conversation: the pre-auth
     * host key check runs ssh-keygen -F immediately before this, so by the
     * time ssh had a pty the flag was already set, and ldm_pty_expect()
     * returned LDM_PTY_ERROR on the first read - whatever ssh had actually
     * said. The visible result was a pre-authentication that quietly
     * declined to decide and let every login through to the RDP client.
     *
     * Cleared before the fork, not after, so a child that dies immediately
     * still sets it.
     */
    child_exited = 0;
    pid = ldm_spawnv(argv, NULL, NULL, ldm_pty_child_init);
    close(slave);
    ldm_pty_slavefd = -1;

    if (pid <= 0) {
        close(master);
        return 0;
    }

    *masterfd = master;
    return pid;
}

int
ldm_pty_expect(int fd, char *p, int seconds, ...)
{
    fd_set set;
    struct timeval timeout;
    int i = 0, st;
    ssize_t size = 0;
    size_t total = 0;
    va_list ap;
    char buffer[BUFSIZ];
    gchar *arg;
    GPtrArray *expects;
    int loopcount = seconds;
    int loopend = 0;

    bzero(p, LDM_PTY_MAXBUF);

    expects = g_ptr_array_new();

    va_start(ap, seconds);

    while ((arg = va_arg(ap, char *)) != NULL) {
        g_ptr_array_add(expects, (gpointer) arg);
    }

    va_end(ap);

    /*
     * Set our file descriptor to be watched.
     */


    /*
     * Main loop.
     */

    while (1) {
        timeout.tv_sec = (long) 1;               /* one second timeout */
        timeout.tv_usec = 0;

        FD_ZERO(&set);
        FD_SET(fd, &set);
        st = select(FD_SETSIZE, &set, NULL, NULL, &timeout);

        if (st == -1 && errno == EINTR)
        {
            continue;                            /* interrupted by signal -> retry */
        }

        if (st < 0) {                            /* bad thing */
            break;
        }

        if (loopcount == 0) {
            break;
        }

        if (!st) {                               /* timeout */
            loopcount--;                         /* We've not seen the data we want */
            continue;
        }

        size = read(fd, buffer, sizeof buffer);
        if (size <= 0) {
            break;
        }

        if ((total + size) < LDM_PTY_MAXBUF) {
            strncpy(p + total, buffer, size);
            total += size;
        }

        /*
         * Compare before looking at child_exited, because for a one-shot
         * exchange the answer and the death arrive together: ssh prints
         * "Permission denied (publickey,password)." and exits in the same
         * breath. Checking the flag first threw that answer away and
         * reported an error instead - which the RDP pre-authentication read
         * as "could not ask", so it let the login through.
         */
        for (i = 0; i < (int) expects->len; i++) {
            if (strstr(p, g_ptr_array_index(expects, i))) {
                loopend = TRUE;
                break;
            }
        }

        if (loopend) {
            break;
        }

        if (child_exited) {
            break;                               /* died without answering */
        }
    }

    log_entry("ldm", 7, "pty expect saw: %s", p);

    /*
     * A match is an answer, whatever happened afterwards. Everything below
     * is about conversations that produced none.
     */
    if (loopend) {
        return i;
    }

    if (size < 0 || st < 0) {
        return LDM_PTY_ERROR;                            /* error occured */
    }
    if (loopcount == 0) {
        return LDM_PTY_TIMED_OUT;                        /* timed out */
    }
    /* Sleep a bit to make sure we notice if ssh died in the meantime */
    usleep(100000);
    if (child_exited)
    {
        return LDM_PTY_ERROR;
    }

    return i;                                    /* which expect did we see? */
}
