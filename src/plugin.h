#ifndef PLUGIN_H
#define PLUGIN_H

#include <glib.h>

#define LTSP_CLUSTER_CONFILE "/etc/ltsp/getltscfg-cluster.conf"

typedef void (*AuthCb)();
typedef void (*GuestCb)();
typedef void (*CleanCb)();
typedef void (*InitCb)();
typedef void (*StartCb)();

typedef struct {
    gchar* name;
    gchar* description;
    /* Init Callback function of the plugin */
    InitCb init_cb;
    /* Clean callback function of the plugin */
    CleanCb clean_cb;
    AuthCb auth_cb;
    GuestCb guest_cb;
    StartCb start_cb;
} LdmBackend;

typedef enum {
    STATUS_UNKNOWN = 0,
    STATUS_SUCCESS = 1,
    STATUS_BAD_CREDENTIALS = 2
} BackendStatus;

/* Standard API used by plugins */
void get_userid(gchar**);
void get_passwd(gchar**);
void get_host(gchar**);
void get_language(gchar**);
void get_session(gchar**);

/**
 * Initialize (loads) an LDM plugin
 */
void __attribute__ ((visibility("default"))) ldm_init_plugin(LdmBackend* descriptor);

/* Get xsession info from key (gconstpointer) */
void __attribute__ ((visibility("default"))) get_Xsession(gchar **, gconstpointer);

/* Set LDM_SESSION and LDM_XSESSION env variable */
void __attribute__ ((visibility("default"))) set_session_env(gchar *, gchar *);

/* Check for loadbalancing IP */
void __attribute__ ((visibility("default"))) get_ltsp_cfg(gchar **);

#endif
