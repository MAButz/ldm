/* LTSP Graphical GTK Greeter
 * Copyright (C) 2007 Francis Giraldeau, <francis.giraldeau@revolutionlinux.com>
 * Copyright 2007-2008 Scott Balneaves <sbalneav@ltsp.org>
 * Copyright 2008-2009 Ryan Niebur <ryanryan52@gmail.com>
 *
 * - Queries servers to get information about them
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#define _GNU_SOURCE

#include <fcntl.h>
#include <glib.h>
#include <net/if.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include "ldminfo.h"
#include "ldmutils.h"

static GHashTable *display_names;
static GHashTable *ldminfo_hash = NULL;

static void
generate_hash_table(void)
{
    char buffer[1024];
    FILE *file;
    char **ret;
    display_names =
        g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
    file = fopen(RC_DIR "/locales", "r");
    if (file == NULL) {
        return;
    }
    while (fgets(buffer, sizeof(buffer), file) != NULL) {
        ret = g_strsplit(buffer, " ", 2);
        g_hash_table_insert(display_names, g_strdup(ret[0]),
                            g_strdup(g_strchomp(ret[1])));
        g_strfreev(ret);
    }
    fclose(file);
}

static gchar *
get_display_name(gchar * locale)
{
    gchar *compare_to;
    char **ret;
    gchar *result;
    ret = g_strsplit(locale, ".", 2);
    compare_to = g_strdup(ret[0]);
    g_strfreev(ret);
    if (compare_to == NULL) {
        return g_strdup(locale);
    }
    result = g_hash_table_lookup(display_names, compare_to);
    if (result == NULL) {
        result = g_strdup(locale);
    }
    return result;
}

/*
 * ldminfo_free
 *  ldminfo struct freed
 */
void
ldminfo_free()
{
    g_hash_table_destroy(ldminfo_hash);
}

/*
 * ldminfo_lookup
 */
ldminfo *
ldminfo_lookup(gconstpointer key)
{
    return g_hash_table_lookup(ldminfo_hash, key);
}

/*
 * ldminfo_size
 */
int
ldminfo_size()
{
    return g_hash_table_size(ldminfo_hash);
}

/*
 * ldminfo_init
 */
void
ldminfo_init(GList ** host_list, const char *ldm_server)
{
    char **hosts_char = NULL;
    ldminfo *ldm_host_info = NULL;
    int i;

    generate_hash_table();

    /* Static hash table */
    ldminfo_hash = g_hash_table_new_full(g_str_hash, g_str_equal,
                                         g_free, g_free);
    hosts_char = g_strsplit(ldm_server, " ", -1);

    for (i = 0; hosts_char != NULL && hosts_char[i] != NULL; i++) {
        /* Initialize to default values */
        ldm_host_info = g_new0(ldminfo, 1);
        ldm_host_info->languages = NULL;
        ldm_host_info->session_names = NULL;
        ldm_host_info->sessions = NULL;
        ldm_host_info->rating = 0;
        ldm_host_info->state = SRV_DOWN;
        ldm_host_info->xsession = NULL;

        /*
         * Populate the ldminfo structure, and determine if the host
         * is up or down.
         */

        _ldminfo_query_one(hosts_char[i], ldm_host_info);

        /*
         * Insert into the hash table.
         */

        g_hash_table_insert(ldminfo_hash, g_strdup(hosts_char[i]),
                            ldm_host_info);

        /*
         * Add the host to the host list.
         */

        *host_list = g_list_append(*host_list, g_strdup(hosts_char[i]));
    }
    g_strfreev(hosts_char);
}

/*
 * Do the query for one host and fill ldminfo struct
 * Note: for right now, we're reading files in /var/run/ldm.  Francis would like
 * the host detection closer to the login and checking network availability
 * etc.  What should happen here, for gutsy+1, is to call out to an
 * external script.  This script will query ldminfo, perform ssh port testing,
 * etc. Things like nc -z hostname ssh, etc.
 */
void
_ldminfo_query_one(const char *hostname, ldminfo * ldm_host_info)
{
    int filedes, numbytes;
    char buf[MAXBUFSIZE];
    char hostfile[BUFSIZ];

    snprintf(hostfile, sizeof hostfile, "/var/run/ldm/%s", hostname);

    filedes = open(hostfile, O_RDONLY);

    if ((numbytes = read(filedes, buf, MAXBUFSIZE - 1)) == -1) {
        perror("read");
        goto error;
    }

    buf[numbytes] = '\0';

    close(filedes);
    ldm_host_info->state = SRV_UP;
    _ldminfo_parse_string(buf, ldm_host_info);
    return;

  error:
    close(filedes);
    ldm_host_info->state = SRV_DOWN;
}

/*
 * split string by line and then construct the ldm_host_info
 */
void
_ldminfo_parse_string(const char *s, ldminfo * ldm_host_info)
{
    char **lines = NULL;
    int i;

    lines = g_strsplit(s, "\n", -1);

    for (i = 0; lines != NULL && lines[i] != NULL; i++) {
        if (!g_ascii_strncasecmp(lines[i], "language:", 9)) {
            gchar **val;
            val = g_strsplit(lines[i], ":", 2);
            ldm_host_info->languages =
                g_list_append(ldm_host_info->languages, g_strdup(val[1]));
            ldm_host_info->language_names =
                g_list_append(ldm_host_info->language_names,
                              get_display_name(val[1]));
            g_strfreev(val);
        } else if (!g_ascii_strncasecmp(lines[i], "session:", 8)
                   && !g_strstr_len(s, -1, "session-with-name")) {
            gchar **val;
            gchar *name;
            val = g_strsplit(lines[i], ":", 2);
            name = g_strrstr(val[1], "/");
            if (name) {
                name++;
            } else {
                name = val[1];
            }
            ldm_host_info->sessions =
                g_list_append(ldm_host_info->sessions, g_strdup(val[1]));
            ldm_host_info->session_names =
                g_list_append(ldm_host_info->session_names,
                              g_strdup(name));
            g_strfreev(val);
        } else if (!g_ascii_strncasecmp(lines[i], "session-with-name:", 8)) {
            gchar **val;
            val = g_strsplit(lines[i], ":", 3);
            ldm_host_info->sessions =
                g_list_append(ldm_host_info->sessions, g_strdup(val[2]));
            ldm_host_info->session_names =
                g_list_append(ldm_host_info->session_names,
                              g_strdup(val[1]));
            g_strfreev(val);
        } else if (!g_ascii_strncasecmp(lines[i], "rating:", 7)) {
            gchar **val;
            val = g_strsplit(lines[i], ":", 2);
            ldm_host_info->rating = atoi(val[1]);
            g_strfreev(val);
        } else if (!g_ascii_strncasecmp(lines[i], "xsession:", 9)) {
            gchar **val;
            val = g_strsplit(lines[i], ":", 2);
            ldm_host_info->xsession = g_strdup(val[1]);
            g_strfreev(val);
        } else {
            /* Variable not supported */
        }
    }
    g_strfreev(lines);
}


/*
 * ldm_getenv_bool
 *  Return if env variable is set to true or false
 *      name -- env. variable name
 */
int
ldm_getenv_bool(const char *name)
{
    char *env = getenv(name);

    if (env) {
        if (*env == 'y' || *env == 't' || *env == 'T' || *env == 'Y')
            return 1;
    }
    return 0;
}

/*
 * ldm_getenv_bool_default
 *  Return if env variable is set to true or false
 *      name -- env. variable name
 *      default_value -- int to return as default [0,1]
 */
int
ldm_getenv_bool_default(const char *name, const int default_value)
{
    char *env = getenv(name);

    if (env != NULL) {
        if (*env == 'y' || *env == 't' || *env == 'T' || *env == 'Y') {
            return 1;
        } else {
            return 0;
        }
    }    
    return default_value;
}

/*
 * ldm_getenv_int
 *  Return an int, will return default_value if not set
 */
int
ldm_getenv_int(const char *name, int default_value)
{
    char *env = getenv(name);

    if (env) {
        return atoi(env);
    }
    return default_value;
}

/*
 * ldm_getenv_str_default
 *  Return a string, will return default_value if not set
 *  No malloc()s, caller should strdup the result if needed.
 */
const char *
ldm_getenv_str_default(const char *name, const char *default_value)
{
    char *env = getenv(name);

    if (env) {
        return env;
    }
    return default_value;
}
