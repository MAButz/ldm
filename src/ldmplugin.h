#ifndef LDMPLUGIN_H
#define LDMPLUGIN_H

#include "plugin.h"

/* ldmplugin.c declarations */
void __attribute__ ((visibility("default"))) ldm_init_plugin(LdmBackend* descriptor);

typedef enum {
    AUTH_EXC_NONE = 0,
    AUTH_EXC_RELOAD_BACKEND = 1,
    AUTH_EXC_GUEST,
    AUTH_EXC_UNKNOWN,
} LdmAuthException;

int ldm_load_plugins(void);
int ldm_auth_plugin();
void ldm_guest_auth_plugin();
void ldm_setup_plugin();
void ldm_start_plugin();
void ldm_close_plugin();
void ldm_raise_auth_except();
gchar** ldm_get_plugins();

/* ldmplugin.c member function decl */
void _load_plugin(const char*);
void set_current_plugin(char *plug_name);

#endif
