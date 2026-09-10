#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <glib.h>
#include <glob.h>
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
    if (!desc)
        die("ldm", "unknown backend: %s", current_plugin);
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
    if (!desc)
        return;
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
    if (!desc)
        die("ldm", "unknown backend: %s", current_plugin);
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
    if (!desc)
        die("ldm", "unknown backend: %s", current_plugin);
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

    if (!desc)
        die("ldm", "unknown backend: %s", current_plugin);

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
 * _scan_plugin_dir
 *  dlopen every *.so in one directory. Returns the number of shared objects
 *  it tried to load, or -1 if the directory could not be opened at all.
 */
static int
_scan_plugin_dir(const char *dir)
{
    DIR *plugin_dir = opendir(dir);

    if (!plugin_dir)
        return -1;

    int found = 0;
    struct dirent *entry;
    while ((entry = readdir(plugin_dir))) {
        if ((entry->d_type == DT_REG || entry->d_type == DT_UNKNOWN)
            && (strstr(entry->d_name, ".so") != NULL)) {
            char *plug_name = g_strdup_printf("%s/%s", dir, entry->d_name);
            log_entry("ldm", 7, "loading: %s", plug_name);

            _load_plugin(plug_name);
            g_free(plug_name);
            found++;
        }
    }

    if (errno)
        perror(strerror(errno));
    closedir(plugin_dir);
    return found;
}

/*
 * ldm_load_plugins
 *  Load all plugins at LDM_PLUG_DIR, or from an equivalent directory if that
 *  one is not where they actually ended up.
 *
 *  LDM_PLUG_DIR is baked in at compile time from $(libdir)/ldm, so it only
 *  matches reality when ldm and its plugins were configured with the same
 *  libdir. On multiarch Debian the packaged plugins land in
 *  /usr/lib/<triplet>/ldm, while a plain "./configure --prefix=/usr" build
 *  compiles in /usr/lib/ldm - mix the two and ldm finds no plugins at all.
 *  It then does not even know its own default "ssh" backend, dies with
 *  "unknown backend", and whatever supervises it (LTSP's screen_session)
 *  respawns it forever: a login greeter that flickers past a few times per
 *  second and never appears. Searching the handful of plausible directories
 *  makes that failure impossible instead of merely unlikely.
 */
int
ldm_load_plugins()
{
    plugin_list = g_tree_new(g_strcmp);
    g_tree_ref(plugin_list);
    plugin_names = g_malloc0(sizeof(gchar *));

    if (_scan_plugin_dir(LDM_PLUG_DIR) > 0)
        return 0;

    /* Fixed alternatives first, then any multiarch libdir. */
    static const char *const candidates[] = {
        "/usr/lib/ldm",
        "/usr/local/lib/ldm",
        NULL
    };

    for (int i = 0; candidates[i]; i++) {
        if (g_strcmp0(candidates[i], LDM_PLUG_DIR) == 0)
            continue;
        if (_scan_plugin_dir(candidates[i]) > 0) {
            log_entry("ldm", 4, "no plugins in %s, used %s instead",
                      LDM_PLUG_DIR, candidates[i]);
            return 0;
        }
    }

    glob_t multiarch = { 0 };
    if (glob("/usr/lib/*/ldm", GLOB_ONLYDIR, NULL, &multiarch) == 0) {
        for (size_t i = 0; i < multiarch.gl_pathc; i++) {
            if (g_strcmp0(multiarch.gl_pathv[i], LDM_PLUG_DIR) == 0)
                continue;
            if (_scan_plugin_dir(multiarch.gl_pathv[i]) > 0) {
                log_entry("ldm", 4, "no plugins in %s, used %s instead",
                          LDM_PLUG_DIR, multiarch.gl_pathv[i]);
                globfree(&multiarch);
                return 0;
            }
        }
    }
    globfree(&multiarch);

    log_entry("ldm", 3, "no authentication plugins found (looked in %s)",
              LDM_PLUG_DIR);
    return 1;
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
