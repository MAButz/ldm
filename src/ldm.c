/*
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, you can find it on the World Wide
 * Web at http://www.gnu.org/copyleft/gpl.html, or write to the Free
 * Software Foundation, Inc., 51 Franklin St, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */

#include <config.h>
#include <glib.h>
#include <glib-object.h>
#include <libintl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

#include "ldm.h"
#include "ldmgreetercomm.h"
#include "ldmplugin.h"
#include "ldminfo.h"
#include "ldmutils.h"
#include "logging.h"

GList *host_list = NULL;
volatile sig_atomic_t unexpected_child = 0;
volatile sig_atomic_t child_exited = 0;

/* stub declaration */
struct ldm_info ldm;

/*
 * get_ldm_env
 *  Setting up LDM env. variables
 */
void
get_ldm_env()
{
    gchar *greeter_path, *wm_path;

    ldminfo_init(&host_list, getenv("LDM_SERVER"));
    ldm.allowguest = ldm_getenv_bool("LDM_GUESTLOGIN");
    ldm.sound = ldm_getenv_bool("SOUND");
    ldm.sound_daemon = g_strdup(getenv("SOUND_DAEMON"));
    ldm.localdev = ldm_getenv_bool("LOCALDEV");
    ldm.override_port = g_strdup(getenv("SSH_OVERRIDE_PORT"));
    ldm.directx = ldm_getenv_bool("LDM_DIRECTX");
    ldm.nomad = ldm_getenv_bool("LDM_NOMAD");
    ldm.autologin = ldm_getenv_bool("LDM_AUTOLOGIN");
    ldm.lang = g_strdup(getenv("LDM_LANGUAGE"));
    ldm.session = g_strdup(getenv("LDM_SESSION"));

    /* Greeter Setup */
    greeter_path = g_strdup(getenv("LDM_GREETER"));
    if (!greeter_path)
        greeter_path = g_strdup("ldmgtkgreet");

    if (greeter_path[0] != '/') {
        ldm.greeter_prog =
            g_strjoin("/", LDM_EXEC_DIR, greeter_path, NULL);
        g_free(greeter_path);
    } else {
        ldm.greeter_prog = greeter_path;
    }

    wm_path = g_strdup(getenv("LDM_WINMANAGER"));
    if (!wm_path)
        wm_path = g_strdup("wwm");

    if (wm_path[0] != '/') {
        ldm.wm_prog = g_strjoin("/", LDM_EXEC_DIR, wm_path, NULL);
        g_free(wm_path);
    } else {
        ldm.wm_prog = wm_path;
    }
    ldm.authfile = g_strdup(getenv("XAUTHORITY"));
}

/*
 * main entry point
 */
int
main(int argc, char *argv[])
{
    /* decls */
    gchar *auth_plugin = NULL;
    struct sigaction action;

    /* Init log settings */
    log_init(ldm_getenv_bool("LDM_SYSLOG"),
             ldm_getenv_int("LDM_LOGLEVEL", -1));

#ifdef ENABLE_NLS
    setlocale(LC_ALL, "");
    bindtextdomain(GETTEXT_PACKAGE, LOCALE_DIR);
    bind_textdomain_codeset(GETTEXT_PACKAGE, "UTF-8");
    textdomain(GETTEXT_PACKAGE);
#endif

#if !GLIB_CHECK_VERSION (2, 35, 3)
    g_type_init();
#endif

    /* set an empty handler for SIGCHLD so they're not ignored */
    action.sa_handler = handle_sigchld;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO;
    sigaction(SIGCHLD, &action, NULL);

    /*
     * Zero out our info struct.
     */
    bzero(&ldm, sizeof ldm);

    ldm.pid = getpid();                          /* Get our pid, to use in the command_socket */
    ldm.display = g_strdup(getenv("DISPLAY"));

    /*
     * Get our IP address.
     */
    get_ipaddr();
    log_entry("ldm", 6, "started on client with IP address: %s",
              ldm.ipaddr);

    /*
     *  Put ip address in environment so that it is available to to the greeter
     */
    setenv("LDMINFO_IPADDR", ldm.ipaddr, 1);

    /*
     * Get some of the environment variables we'll need.
     */
    get_ldm_env();

    /*
     * Load plugins
     */
    if (ldm_load_plugins())
        log_entry("ldm", 3, "unable to load plugins");

    /*
     * Begin running display manager
     */
    log_entry("ldm", 6, "calling rc.d init scripts");
    rc_files("init");                            /* Execute any rc files */

    if (!ldm.autologin) {
        /* start interactive greeter only if no autologin */
        gint rfd, wfd, rfd2, wfd2;

        log_entry("ldm", 7, "spawning window manager: %s", ldm.wm_prog);
        ldm.wmpid = ldm_spawn(ldm.wm_prog, &rfd2, &wfd2, NULL);
        log_entry("ldm", 7, "spawning greeter: %s", ldm.greeter_prog);
        ldm.greeterpid = ldm_spawn(ldm.greeter_prog, &rfd, &wfd, NULL);
        set_greeter_pid(ldm.greeterpid);
        /* create GIOChannels for the greeter */
        ldm.greeterr = g_io_channel_unix_new(rfd);
        set_greeter_read_channel(ldm.greeterr);
        ldm.greeterw = g_io_channel_unix_new(wfd);
        set_greeter_write_channel(ldm.greeterw);

        /* if a backend is forced, leave no choice */
        if (!getenv("LDM_FORCE_BACKEND")) {
            gchar *cmd;
            gchar *backends;

            /* allow choosing backend */
            backends = g_strjoinv("|", ldm_get_plugins());
            cmd =
                g_strconcat
                ("pref choice;backend;Authentication Backend;Select _Backend ...;session;",
                 backends, "\n", NULL);
            if (ask_greeter(cmd))
                die("ldm", "%s from greeter failed", cmd);
            g_free(backends);
            g_free(cmd);
        }

        do {
            /* backend precedence is LDM_FORCE_BACKEND, user choice,
             * LDM_DEFAULT_BACKEND then hardcoded constant. */
            g_free(auth_plugin);
            auth_plugin = g_strdup(getenv("LDM_FORCE_BACKEND"));
            if (!auth_plugin) {
                auth_plugin = ask_value_greeter("value backend\n");
                if (!g_strcmp0(auth_plugin, "None")) {
                    g_free(auth_plugin);
                    auth_plugin = g_strdup(getenv("LDM_DEFAULT_BACKEND"));
                    if (!auth_plugin)
                        auth_plugin = g_strdup(DEFAULT_AUTH_PLUGIN);
                }
            }

            /* Init plugin */
            set_current_plugin(auth_plugin);
            log_entry("ldm", 6, "authenticating with backend: %s",
                      auth_plugin);
            ldm_setup_plugin();
        } while (ldm_auth_plugin());
    } else {
        auth_plugin = g_strdup(getenv("LDM_DEFAULT_BACKEND"));
        if (!auth_plugin)
            auth_plugin = g_strdup(DEFAULT_AUTH_PLUGIN);

        set_current_plugin(auth_plugin);
        log_entry("ldm", 6, "guest authenticating with backend: %s",
                  auth_plugin);
        ldm_setup_plugin();

        ldm_guest_auth_plugin();
    }

    /* Closing Window Manager (wwm) */
    if (ldm.wmpid)
        close_wm();

    /* Start Plugin */
    ldm_start_plugin();

    /*
     * From here plugins has taken control. When plugin return, close everything
     */

    log_entry("ldm", 6, "X session ended");

    log_entry("ldm", 6, "calling rc.d stop scripts");
    rc_files("stop");                            /* Execute any rc files */

    /* Closing plugin */
    ldm_close_plugin();

    /* Close logging */
    log_close();

    g_free(ldm.server);
    g_free(ldm.display);
    g_free(ldm.override_port);
    g_free(ldm.authfile);
    g_free(ldm.username);
    g_free(ldm.lang);
    g_free(ldm.session);
    g_free(ldm.xsession);
    g_free(ldm.sound_daemon);
    g_free(ldm.greeter_prog);
    g_free(ldm.control_socket);
    g_free(ldm.ipaddr);
    g_list_foreach(host_list, (GFunc) g_free, NULL);
    g_list_free(host_list);

    exit(EXIT_SUCCESS);
}
