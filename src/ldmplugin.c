#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <glib.h>
#include <glib.h>
#include <malloc.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#include "ldmplugin.h"
#include "ldmgreetercomm.h"
#include "logging.h"

GTree *plugin_list = NULL;
gchar **plugin_names = NULL;
static jmp_buf auth_jmp_buf;
static gchar *current_plugin = NULL;

static int
g_strcmp(gconstpointer a, gconstpointer b)
{
    return strcmp((char *) a, (char *) b);
}

/*
 * set_current_plugin
 *  Set current plugin name
 */
void
set_current_plugin(char *plug_name)
{
    current_plugin = plug_name;
}

/*
 * ldm_start_plugin
 *  Iterate over plugin_list and start plugin
 */
void
ldm_start_plugin()
{
    LdmBackend *desc =
        (LdmBackend *) g_tree_lookup(plugin_list, current_plugin);
    if (desc->start_cb)
        desc->start_cb();
}

/*
 * ldm_close_plugin
 *  Iterate over plugin_list and close plugin
 */
void
ldm_close_plugin()
{
    LdmBackend *desc =
        (LdmBackend *) g_tree_lookup(plugin_list, current_plugin);
    if (desc->clean_cb)
        desc->clean_cb();
}

/*
 * ldm_setup_plugin
 *  Call setup callback function of plugin
 */
void
ldm_setup_plugin()
{
    log_entry("ldm", 7, "setting up plugin: %s", current_plugin);
    LdmBackend *desc =
        (LdmBackend *) g_tree_lookup(plugin_list, current_plugin);
    if (desc->init_cb)
        desc->init_cb();
}

/*
 * ldm_guest_auth_plugin
 *  Call setup guest authentication values of plugin
 */
void
ldm_guest_auth_plugin()
{
    log_entry("ldm", 7, "guest auth plugin: %s", current_plugin);
    LdmBackend *desc =
        (LdmBackend *) g_tree_lookup(plugin_list, current_plugin);
    if (desc->guest_cb)
        desc->guest_cb();
}

/*
 * ldm_auth_plugin
 *  Call auth callback function of plugin
 *  Returns 0 on success, 1 otherwise
 */
int
ldm_auth_plugin()
{
    LdmBackend *desc =
        (LdmBackend *) g_tree_lookup(plugin_list, current_plugin);

    if (desc->guest_cb)
        ask_greeter("allowguest true\n");
    else
        ask_greeter("allowguest false\n");

    switch (setjmp(auth_jmp_buf)) {
    case AUTH_EXC_NONE:
        if (desc->auth_cb)
            desc->auth_cb();
        return 0;
    case AUTH_EXC_RELOAD_BACKEND:
        ldm_close_plugin();

        log_entry("ldm", 7, "reloading backend");
        return 1;
    case AUTH_EXC_GUEST:
        if (desc->guest_cb)
            desc->guest_cb();
        return 0;
    }
    return 1;
}

/*
 * ldm_init_plugin
 *  Init plugin function. Must be called at each plugin's init
 */
void __attribute__ ((visibility("default")))
    ldm_init_plugin(LdmBackend * descriptor)
{
    gchar **new_plugin_names;
    int plugin_names_len;

    plugin_names_len = g_strv_length(plugin_names);
    new_plugin_names = g_realloc(plugin_names,
                                 (plugin_names_len + 1) * sizeof(gchar *));
    if (new_plugin_names != plugin_names) {
        g_free(plugin_names);
        plugin_names = new_plugin_names;
    }
    plugin_names[plugin_names_len] = g_strdup(descriptor->name);
    plugin_names[plugin_names_len + 1] = NULL;

    g_tree_replace(plugin_list, descriptor->name, descriptor);
    log_entry("ldm", 7, "%s initialized", descriptor->name);
}

/*
 * _load_plugin
 *  open plugin's lib
 *      path -- plugin path
 */
void
_load_plugin(const char *path)
{
    void *handle = dlopen(path, RTLD_LAZY);
    if (handle) {
        log_entry("ldm", 7, "loaded %s", path);
        return;
    }

    log_entry("ldm", 4, "%s: Invalid LDM plugin %s", dlerror(), path);
}

/*
 * ldm_load_plugins
 *  Load all plugins at LDM_PLUG_DIR
 */
int
ldm_load_plugins()
{
    plugin_list = g_tree_new(g_strcmp);
    g_tree_ref(plugin_list);
    plugin_names = g_malloc0(sizeof(gchar *));

    DIR *plugin_dir = opendir(LDM_PLUG_DIR);

    if (!plugin_dir) {
        log_entry("ldm", 3, "unable to open plugin dir: %s", LDM_PLUG_DIR);
        return 1;
    }
    struct dirent *entry;
    while ((entry = readdir(plugin_dir))) {
        if ((entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN)
            && (strstr(entry->d_name, ".so") != NULL)) {
            int name_len =
                strlen(entry->d_name) + strlen(LDM_PLUG_DIR) + 2;
            char *plug_name = (char *) malloc(name_len);
            snprintf(plug_name, name_len, "%s/%s", LDM_PLUG_DIR,
                     entry->d_name);
            log_entry("ldm", 7, "loading: %s", plug_name);

            _load_plugin(plug_name);
            free(plug_name);
        }
    }

    if (errno)
        perror(strerror(errno));
    closedir(plugin_dir);
    return 0;
}

/*
 * Called on authentication failure from an auth callback.
 * Will unwind the stack up to the frame above the callback and handle the
 * error before retrying.
 */
void
ldm_raise_auth_except(LdmAuthException n)
{
    longjmp(auth_jmp_buf, n);
}

gchar **
ldm_get_plugins()
{
    return plugin_names;
}
