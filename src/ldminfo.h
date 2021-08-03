#ifndef LDMINFO_H
#define LDMINFO_H

#define MAXBUFSIZE 16384

#include <glib.h>

/*
 * state enum
 */

enum {
    SRV_UP,
    SRV_DOWN
};

/*
 * Info about servers
 */

typedef struct {
    GList *languages;
    GList *language_names;
    GList *session_names;
    GList *sessions;
    gint rating;
    gint state;
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

/* Do the query for one host and fill ldminfo struct */
void _ldminfo_query_one(const char *hostname, ldminfo * ldm_host_info);

/* split string by line and then construct the ldm_host_info */
void _ldminfo_parse_string(const char *s, ldminfo * ldm_host_info);

int ldm_getenv_bool(const char *name);
int ldm_getenv_bool_default(const char *name, const int default_value);
int ldm_getenv_int(const char *name, int default_value);
const char *ldm_getenv_str_default(const char *name, const char *default_value);

ldminfo *ldminfo_lookup(gconstpointer key);

int ldminfo_size();

void ldminfo_free();
#endif
