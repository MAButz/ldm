/*
 * ldmlockout.c - counting rejected sign-ins across a client's screens.
 *
 * Why this exists. Sites protect RDP with fail2ban: a few wrong passwords
 * and the *client* is blocked at the network level for some minutes. From
 * the seat that is baffling - the screen that worked a moment ago now
 * fails in some other way entirely - and it cannot be explained by asking
 * the server, because being unable to talk to the server is precisely the
 * state we are in. Whatever the greeter says about it, it has to know by
 * itself.
 *
 * What it can honestly count, and what it cannot. Measured against xrdp:
 * when a password is rejected, the RDP client neither exits nor reports
 * it - the session stays open showing xrdp's own login window, and the
 * retries happen in there, where ldm sees nothing at all. So failed RDP
 * logins are simply not observable from here. What *is* observable is the
 * optional pre-authentication (RDP_PREAUTH): it asks a KDC or an sshd
 * before the RDP client is started, and it gets a straight answer. Those
 * are the rejections counted here, and they are worth counting even
 * though a rejected pre-auth never reaches the RDP server - because they
 * reach something else that counts them too. RDP_PREAUTH=ssh sends them
 * to sshd, where an sshd jail bans the client (with banaction_allports,
 * on every port). RDP_PREAUTH=krb5 sends them to the domain controller,
 * where the account lockout policy is keeping its own tally. Warning
 * before either of those trips is the useful part.
 *
 * Why a file, and why in /run. A client runs one ldm per screen -
 * SCREEN_05 through SCREEN_08 in this lab - and they are separate
 * processes with no memory of each other. The thing doing the banning
 * counts per source address, and all of those screens share one. A
 * counter held in a process would therefore count a quarter of what the
 * server counts, which is the same as not counting. /run is tmpfs, shared
 * by every process on the client and gone at reboot.
 *
 * That last part is a real limitation and not a bug to be fixed here: a
 * ban outlives a reboot, our count does not. After a reboot the greeter is
 * back to knowing nothing, and says nothing rather than guessing.
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
#include <fcntl.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "ldmlockout.h"
#include "logging.h"

#define LDM_LOCKOUT_DIR "/run/ldm/lockout"

/*
 * Keep the file bounded. Nothing here needs more history than the largest
 * window anyone would configure, and a greeter left at a login prompt for
 * a week should not accumulate a megabyte of timestamps.
 */
#define LDM_LOCKOUT_MAX_ENTRIES 64

/*
 * Build the path for target. The name goes into a filename, so everything
 * that is not plainly safe becomes an underscore - target is an RDP server
 * name or a realm from lts.conf, which is admin-supplied rather than user
 * input, but a path built from a string is worth making unable to escape
 * its directory regardless of where the string came from.
 */
static gchar *
lockout_path(const gchar *target)
{
    gchar *safe, *path;
    gsize i;

    if (!target || !*target)
        target = "default";

    safe = g_strdup(target);
    for (i = 0; safe[i]; i++) {
        if (!g_ascii_isalnum(safe[i]) && safe[i] != '.' && safe[i] != '-')
            safe[i] = '_';
    }

    /*
     * Once the slashes are gone the only sanitised names that still name
     * something other than a plain file are "." and ".." - they would
     * resolve to the lockout directory and its parent. Every other dotted
     * name is an ordinary filename, so only these two are replaced.
     */
    if (g_strcmp0(safe, ".") == 0 || g_strcmp0(safe, "..") == 0) {
        g_free(safe);
        safe = g_strdup("default");
    }

    path = g_build_filename(LDM_LOCKOUT_DIR, safe, NULL);
    g_free(safe);
    return path;
}

static gboolean
lockout_make_dir(void)
{
    if (g_mkdir_with_parents(LDM_LOCKOUT_DIR, 0700) != 0) {
        log_entry("ldm", 4, "lockout: cannot create %s: %s",
                  LDM_LOCKOUT_DIR, g_strerror(errno));
        return FALSE;
    }
    return TRUE;
}

/*
 * Read the file into a newly allocated array of timestamps, dropping
 * anything older than cutoff. Returns the number kept, and NULL in *out
 * when there is nothing to report.
 */
static gint
lockout_read(const gchar *path, time_t cutoff, time_t **out)
{
    gchar *contents = NULL;
    gchar **lines;
    time_t *stamps;
    time_t now = time(NULL);
    gint kept = 0;
    guint i;

    if (out)
        *out = NULL;

    if (!g_file_get_contents(path, &contents, NULL, NULL))
        return 0;

    lines = g_strsplit(contents, "\n", -1);
    stamps = g_new0(time_t, g_strv_length(lines) + 1);

    for (i = 0; lines[i]; i++) {
        gint64 ts;

        if (!*lines[i])
            continue;
        ts = g_ascii_strtoll(lines[i], NULL, 10);
        if (ts <= 0)
            continue;
        /*
         * A timestamp from the future means the clock moved since it was
         * written - NTP settling after boot does this on hardware without
         * a working RTC. Keeping it would hold a lockout open for as long
         * as the skew lasts, so drop it.
         */
        if ((time_t) ts > now)
            continue;
        if ((time_t) ts >= cutoff)
            stamps[kept++] = (time_t) ts;
    }

    g_strfreev(lines);
    g_free(contents);

    if (kept > 0 && out)
        *out = stamps;
    else
        g_free(stamps);

    return kept;
}

void
ldm_lockout_record(const gchar *target, const gchar *user)
{
    gchar *path, *line;
    gint fd;
    gint count;

    if (!lockout_make_dir())
        return;

    path = lockout_path(target);

    /*
     * O_APPEND with a single short write is what makes this safe between
     * screens: each greeter appends one line, and the kernel does not
     * interleave them. No locking, and nothing to clean up if a greeter
     * is killed mid-write.
     */
    fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
    if (fd < 0) {
        log_entry("ldm", 4, "lockout: cannot write %s: %s", path,
                  g_strerror(errno));
        g_free(path);
        return;
    }

    line = g_strdup_printf("%lld\n", (long long) time(NULL));
    if (write(fd, line, strlen(line)) < 0) {
        log_entry("ldm", 4, "lockout: short write to %s: %s", path,
                  g_strerror(errno));
    }
    g_free(line);
    close(fd);

    log_entry("ldm", 5, "lockout: recorded a rejection of '%s' by %s",
              user ? user : "(unknown user)", target);

    /*
     * Compact when the file has grown past the cap. Done after the append
     * rather than before, so the record is never lost to the rewrite.
     */
    count = lockout_read(path, 0, NULL);
    if (count > LDM_LOCKOUT_MAX_ENTRIES) {
        time_t *stamps = NULL;
        gint n = lockout_read(path, 0, &stamps);

        if (stamps) {
            GString *rebuilt = g_string_new(NULL);
            gint first = n - LDM_LOCKOUT_MAX_ENTRIES;
            gint i;

            for (i = first > 0 ? first : 0; i < n; i++)
                g_string_append_printf(rebuilt, "%lld\n",
                                       (long long) stamps[i]);
            g_file_set_contents(path, rebuilt->str, rebuilt->len, NULL);
            g_chmod(path, 0600);
            g_string_free(rebuilt, TRUE);
            g_free(stamps);
        }
    }

    g_free(path);
}

gint
ldm_lockout_count(const gchar *target, gint window, time_t *newest)
{
    gchar *path;
    time_t *stamps = NULL;
    time_t cutoff;
    gint n, i;

    if (newest)
        *newest = 0;

    if (window <= 0)
        return 0;

    path = lockout_path(target);
    cutoff = time(NULL) - window;
    n = lockout_read(path, cutoff, &stamps);
    g_free(path);

    if (stamps) {
        if (newest) {
            for (i = 0; i < n; i++) {
                if (stamps[i] > *newest)
                    *newest = stamps[i];
            }
        }
        g_free(stamps);
    }

    return n;
}

void
ldm_lockout_clear(const gchar *target)
{
    gchar *path = lockout_path(target);

    if (g_unlink(path) == 0)
        log_entry("ldm", 6, "lockout: cleared the tally for %s", target);
    else if (errno != ENOENT)
        log_entry("ldm", 4, "lockout: cannot clear %s: %s", path,
                  g_strerror(errno));

    g_free(path);
}
