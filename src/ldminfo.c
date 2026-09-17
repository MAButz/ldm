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

#include <glib.h>
#include <net/if.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include "ldminfo.h"
#include "ldmutils.h"

static GHashTable *ldminfo_hash = NULL;

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

    /* Static hash table */
    ldminfo_hash = g_hash_table_new_full(g_str_hash, g_str_equal,
                                         g_free, g_free);
    hosts_char = g_strsplit(ldm_server, " ", -1);

    for (i = 0; hosts_char != NULL && hosts_char[i] != NULL; i++) {
        /*
         * Empty, and it stays that way unless something fills it. There is
         * no query here any more: what filled these lists was ldminfod on
         * port 9571, which this fork no longer ships. Whether a server is
         * reachable is answered by connecting to it, not by a second
         * daemon that had to be reachable first.
         */
        ldm_host_info = g_new0(ldminfo, 1);
        ldm_host_info->languages = NULL;
        ldm_host_info->language_names = NULL;
        ldm_host_info->session_names = NULL;
        ldm_host_info->sessions = NULL;
        ldm_host_info->xsession = NULL;

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
