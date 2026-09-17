#ifndef LDMINFO_H
#define LDMINFO_H

#include <glib.h>

/*
 * What ldm knows about each server named in LDM_SERVER.
 *
 * The lists were once filled by querying ldminfod on port 9571, which
 * reported the server's sessions, locales and a load rating. That daemon is
 * gone: load balancing belongs in front of the session servers - HAProxy in
 * this fork's design - and not in a greeter that collected a rating and then
 * never sorted by it. The struct stays because the greeter's session and
 * language choosers are built on it; the lists are filled from lts.conf and
 * the defaults instead.
 */

typedef struct {
    GList *languages;
    GList *language_names;
    GList *session_names;
    GList *sessions;
    gchar *xsession;
} ldminfo;

/*
 * ldminfo.c
 */

/*
 * Init the hash table : key=char hostnames, values=struct *ldminfo
 * ldm_server is the LDM_SERVER variable, a list of hostnames separated by space
 */
void ldminfo_init(GList ** host_list, const char *ldm_server);

int ldm_getenv_bool(const char *name);
int ldm_getenv_bool_default(const char *name, const int default_value);
int ldm_getenv_int(const char *name, int default_value);
const char *ldm_getenv_str_default(const char *name, const char *default_value);

ldminfo *ldminfo_lookup(gconstpointer key);

int ldminfo_size();

void ldminfo_free();
#endif
