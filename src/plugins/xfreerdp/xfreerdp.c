#include <config.h>
#include <ctype.h>
#include <glib.h>
#include <libintl.h>
#include <locale.h>
#include <pthread.h>
#include <pty.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <utmp.h>

#include "../../ldmutils.h"
#include "../../ldmgreetercomm.h"
#include "../../logging.h"
#include "../../plugin.h"
#include "xfreerdp.h"

int screen;

LdmBackend *descriptor;
RdpInfo *rdpinfo;

void __attribute__ ((constructor)) initialize()
{
    descriptor = (LdmBackend *) malloc(sizeof(LdmBackend));
    bzero(descriptor, sizeof(LdmBackend));

    descriptor->name = "xfreerdp";
    descriptor->description = "xfreerdp plugin";
    descriptor->init_cb = init_xfreerdp;
    descriptor->auth_cb = auth_xfreerdp;
    descriptor->start_cb = start_xfreerdp;
    descriptor->clean_cb = close_xfreerdp;
    ldm_init_plugin(descriptor);
}

/*
 * detect_xfreerdp_binary
 *  Figure out which xfreerdp executable to run.
 *
 * FreeRDP 2.x and 3.x are packaged side by side on some distributions, and
 * their binaries have different names: the old "freerdp2-x11" package
 * ships "xfreerdp", while current Debian/Ubuntu only ship "freerdp3-x11",
 * whose binary is "xfreerdp3" - there's no "xfreerdp" at all on a stock
 * Debian 13 install. Both versions accept the same /u:/p:/d:/v: option
 * syntax, so no other changes are needed to support either one.
 *
 * RDP_XFREERDP_BIN can force a specific executable (name or full path) if
 * an admin needs to override the auto-detection.
 */
static gchar *
detect_xfreerdp_binary()
{
    const gchar *forced = getenv("RDP_XFREERDP_BIN");
    gchar *path;

    if (forced && *forced) {
        log_entry("xfreerdp", 6, "using RDP_XFREERDP_BIN override '%s'",
                  forced);
        return g_strdup(forced);
    }

    /* Prefer FreeRDP 3 (xfreerdp3) when both are installed. */
    path = g_find_program_in_path("xfreerdp3");
    if (path) {
        log_entry("xfreerdp", 6, "found FreeRDP 3 binary '%s'", path);
        return path;
    }

    path = g_find_program_in_path("xfreerdp");
    if (path) {
        log_entry("xfreerdp", 6, "found FreeRDP 2 binary '%s'", path);
        return path;
    }

    log_entry("xfreerdp", 3,
              "neither xfreerdp3 nor xfreerdp found in PATH");
    return NULL;
}

/*
 * init_xfreerdp
 *  Callback function for initialization
 */
void
init_xfreerdp()
{
    rdpinfo = (RdpInfo *) malloc(sizeof(RdpInfo));
    if (!rdpinfo) {
        log_entry("xfreerdp", 3, "Fehler: Speicher für RdpInfo konnte nicht allokiert werden.");
        return;
    }
    bzero(rdpinfo, sizeof(RdpInfo));

    rdpinfo->binary = detect_xfreerdp_binary();

    /*
     * Safe baseline so rdpinfo->domain is never NULL even if we bail out
     * below (e.g. DISPLAY unset) before reaching the per-screen
     * RDP_DEFAULT_DOMAIN handling further down.
     */
    rdpinfo->domain = g_strdup("None");

    // Abrufen der Bildschirmnummer aus der Umgebungsvariable
    gchar *display_env = g_strdup(getenv("DISPLAY"));
    if (display_env != NULL) {
        log_entry("xfreerdp", 6, "DISPLAY Umgebungsvariable: '%s'", display_env);
        // Extrahiere die Bildschirmnummer aus der DISPLAY-Variable (z.B., ":7" -> 7)
        const char *colon = strchr(display_env, ':');
        if (colon) {
            screen = atoi(colon + 1);
        }
        log_entry("xfreerdp", 6, "Aktueller screen '%d'", screen);
    } else {
        log_entry("xfreerdp", 3, "Fehler: DISPLAY Umgebungsvariable nicht gesetzt.");
        return;
    }
    
    // Format screen number as two digits (e.g., 00, 01, 02)
    gchar *screen_formatted = g_strdup_printf("%02d", screen);
    if (screen_formatted == NULL) {
        log_entry("xfreerdp", 3, "Fehler: Display Nummer konnte nicht extrahiert werden.");
        return;
    }
    
    // Dynamische Prüfung der RDP_OPTIONS_<screen>, RDP_SERVER_<screen> und
    // RDP_DEFAULT_DOMAIN_<screen>
    gchar *screen_rdpoptions_var = g_strdup_printf("RDP_OPTIONS_%s", screen_formatted);
    gchar *screen_rdpserver_var = g_strdup_printf("RDP_SERVER_%s", screen_formatted);
    gchar *screen_rdpdomain_var = g_strdup_printf("RDP_DEFAULT_DOMAIN_%s", screen_formatted);
    if (screen_rdpoptions_var == NULL || screen_rdpserver_var == NULL ||
        screen_rdpdomain_var == NULL) {
        log_entry("xfreerdp", 3, "Fehler: Keine Umgebungsvariablen für spezifische RDP Optionen und oder Server gefunden.");
        g_free(screen_formatted);
        return;
    }

    const gchar *rdpoptions_value = getenv(screen_rdpoptions_var);
    const gchar *rdpserver_value = getenv(screen_rdpserver_var);
    const gchar *rdpdomain_value = getenv(screen_rdpdomain_var);

    if (rdpoptions_value) {
        rdpinfo->rdpoptions = g_strdup(rdpoptions_value);
        log_entry("xfreerdp", 6, "Verwende spezifische RDP_OPTIONS '%s'", rdpoptions_value);
    } else {
        rdpinfo->rdpoptions = g_strdup(getenv("RDP_OPTIONS"));
        log_entry("xfreerdp", 6, "Verwende Standard RDP_OPTIONS");
    }

    if (rdpserver_value) {
        rdpinfo->server = g_strdup(rdpserver_value);
        log_entry("xfreerdp", 6, "Verwende spezifische RDP_SERVER '%s'", rdpserver_value);
    } else {
        rdpinfo->server = g_strdup(getenv("RDP_SERVER"));
        log_entry("xfreerdp", 6, "Verwende Standard RDP_SERVER");
    }

    /*
     * Optional default domain, e.g. RDP_DEFAULT_DOMAIN="MYDOMAIN" in
     * lts.conf. Used as-is unless RDP_DOMAIN (a '|'-separated list of
     * choices, see auth_xfreerdp()) is also set, in which case the
     * greeter's domain picker takes over instead. Falls back to "None"
     * (meaning: no /d: option at all) if neither is configured.
     */
    g_free(rdpinfo->domain);
    if (rdpdomain_value) {
        rdpinfo->domain = g_strdup(rdpdomain_value);
        log_entry("xfreerdp", 6, "Verwende spezifische RDP_DEFAULT_DOMAIN '%s'", rdpdomain_value);
    } else {
        const gchar *default_domain = getenv("RDP_DEFAULT_DOMAIN");
        rdpinfo->domain = g_strdup(default_domain ? default_domain : "None");
        log_entry("xfreerdp", 6, "Verwende Standard RDP_DEFAULT_DOMAIN '%s'",
                  rdpinfo->domain);
    }

    // Speicher freigeben
    g_free(display_env);
    g_free(screen_rdpoptions_var);
    g_free(screen_rdpserver_var);
    g_free(screen_rdpdomain_var);
    g_free(screen_formatted);
}

/*
 * start_xfreerdp
 *  Callback function for starting xfreerdp session
 */
void
start_xfreerdp()
{
    gboolean error = FALSE;

    /* Variable validation */
    if (!rdpinfo->binary) {
        log_entry("xfreerdp", 3,
                  "no xfreerdp executable found (neither xfreerdp3 nor "
                  "xfreerdp in PATH, and RDP_XFREERDP_BIN isn't set)");
        error = TRUE;
    }

    if (!rdpinfo->username) {
        log_entry("xfreerdp", 3, "no username");
        error = TRUE;
    }

    if (!rdpinfo->password) {
        log_entry("xfreerdp", 3, "no password");
        error = TRUE;
    }

    if (!rdpinfo->server) {
        log_entry("xfreerdp", 3, "no server");
        error = TRUE;
    }

    if (!rdpinfo->domain) {
        log_entry("xfreerdp", 3, "no domain");
        error = TRUE;
    }

    if (error) {
        die("xfreerdp", "missing mandatory information");
    }

    /* Greeter not needed anymore */
    close_greeter();

    log_entry("xfreerdp", 6, "starting '%s' session to '%s' as '%s'",
              rdpinfo->binary, rdpinfo->server, rdpinfo->username);
    xfreerdp_session();
    log_entry("xfreerdp", 6, "closing xfreerdp session");
}

/*
 * _get_domain
 *
 * Only asks the greeter's domain preference (populated in auth_xfreerdp()
 * when RDP_DOMAIN is set) if that preference actually exists. Otherwise
 * rdpinfo->domain already holds whatever RDP_DEFAULT_DOMAIN[_NN] set in
 * init_xfreerdp() (or "None"), and there's nothing to ask the greeter -
 * doing so unconditionally would always report back "None" and clobber a
 * configured default domain.
 */
void
_get_domain()
{
    gchar *cmd = "value domain\n";

    if (getenv("RDP_DOMAIN")) {
        rdpinfo->domain = ask_value_greeter(cmd);
    }
}

/*
 * auth_xfreerdp
 *  Callback function for authentication
 */
void
auth_xfreerdp()
{
    gchar *cmd;

    /* Separator for domains : '|' */
    gchar *domains = getenv("RDP_DOMAIN");
    cmd =
        g_strconcat
        ("pref choice;domain;Domain;Select Domai_n ...;session;", domains,
         "\n", NULL);
    if (domains) {
        if (ask_greeter(cmd))
            die("xfreerdp", "%s from greeter failed", cmd);
    } else {
        log_entry("xfreerdp", 7,
                  "RDP_DOMAIN isn't defined, xfreerdp will connect on default domain");
    }

    /* Ask for UserID */
    get_userid(&(rdpinfo->username));

    /* If user clicks on guest button above, this has changed  */
    get_passwd(&(rdpinfo->password));

    /* Get hostname */
    if (!rdpinfo->server)
        get_host(&(rdpinfo->server));

    /* Get Domain (xfreerdp plugin specific) */
    _get_domain();

    /* Get Language */
    get_language(&(rdpinfo->lang));

    g_free(cmd);
}

/*
 * close_xfreerdp
 *  Callback function for closing the plugins
 */
void
close_xfreerdp()
{
    log_entry("xfreerdp", 7, "closing xfreerdp session");
    free(rdpinfo);
}

/*
 * xfreerdp_session
 *  Start a xfreerdp session to server
 *
 * Builds the command as an argv array and spawns it directly (via
 * ldm_spawnv(), no shell/word-splitting involved), instead of building a
 * single command string. That's what the old string-based version got
 * wrong: g_shell_parse_argv() (used internally by ldm_spawn()) splits on
 * whitespace, so any password or username containing a space or a quote
 * character would silently get cut into the wrong number of arguments.
 * Passing each value as its own argv element sidesteps that entirely -
 * no escaping needed, whatever's in username/password/domain just works.
 *
 * The password specifically is never put on the command line at all
 * (unlike username/domain/server, which aren't secret): any local user
 * can read another process's argv via /proc/<pid>/cmdline or `ps -ef`,
 * so a plain /p:<password> would leak it to anyone on the same client.
 * Instead we use xfreerdp's /from-stdin:force option and write the
 * password to its stdin ourselves after spawning it.
 */
void
xfreerdp_session()
{
    GPtrArray *argv = g_ptr_array_new();
    gchar **rdpoptions_argv = NULL;
    gint wfd;
    guint i;

    g_ptr_array_add(argv, g_strdup(rdpinfo->binary));
    g_ptr_array_add(argv, g_strconcat("/u:", rdpinfo->username, NULL));
    g_ptr_array_add(argv, g_strdup("/from-stdin:force"));

    /* Only append the domain if it's set */
    if (g_strcmp0(rdpinfo->domain, "None") != 0) {
        g_ptr_array_add(argv, g_strconcat("/d:", rdpinfo->domain, NULL));
    }

    /*
     * RDP_OPTIONS comes from lts.conf (trusted admin configuration, not
     * end-user input), so shell-splitting it into multiple arguments here
     * is fine.
     */
    if (rdpinfo->rdpoptions &&
        g_shell_parse_argv(rdpinfo->rdpoptions, NULL, &rdpoptions_argv,
                            NULL)) {
        for (i = 0; rdpoptions_argv[i] != NULL; i++) {
            g_ptr_array_add(argv, rdpoptions_argv[i]);
        }
        /* Ownership of the individual strings moved into argv above, so
         * just release the array of pointers itself. */
        g_free(rdpoptions_argv);
    }

    /* Append Option for RDP-Server and Display Full-Screen */
    g_ptr_array_add(argv, g_strconcat("/v:", rdpinfo->server, NULL));
    g_ptr_array_add(argv, g_strdup("/f"));
    g_ptr_array_add(argv, NULL);

    /* Set Environment for xfreerdp INFO logging and important xfreerdp needs to set the "HOME" to /root otherwise xfreerdp is not working! */
    setenv("WLOG_LEVEL", "INFO", 1);
    setenv("WLOG_APPENDER", "SYSLOG", 1);
    setenv("HOME", "/root", 1);

    /* Set Enviroment??? for LIBVA_DRIVER_NAME=i965 -> older INTEL Graphics Card  OR  LIBVA_DRIVER_NAME=iHD -> newer INTEL Graphics Card */
    /* For newer verions of freerdp the hardware acceleration over ffmpeg will not use when the LIBVA_DRIVER_NAME=XXXX not set...???? */

    /* Spawning xfreerdp session; wfd is our end of a pipe to its stdin,
     * used below to hand over the password out-of-band from argv. */
    rdpinfo->rdppid = ldm_spawnv((gchar **) argv->pdata, NULL, &wfd, NULL);

    {
        gchar *line = g_strdup_printf("%s\n", rdpinfo->password);
        gsize len = strlen(line);
        gsize written = 0;

        while (written < len) {
            gssize n = write(wfd, line + written, len - written);
            if (n <= 0) {
                log_entry("xfreerdp", 3,
                          "failed to write password to xfreerdp's stdin");
                break;
            }
            written += n;
        }
        /* Wipe the password from memory as soon as we're done with it. */
        memset(line, 0, len);
        g_free(line);
        close(wfd);
    }

    /*
     * Same "free the password as promised" cleanup the ssh plugin does
     * once it's no longer needed: rdpinfo->password has now been handed
     * to xfreerdp, so there's no reason to keep it (or its heap bytes)
     * around for the rest of the session.
     */
    if (rdpinfo->password) {
        memset(rdpinfo->password, 0, strlen(rdpinfo->password));
        g_free(rdpinfo->password);
        rdpinfo->password = NULL;
    }

    ldm_wait(rdpinfo->rdppid);

    for (i = 0; i < argv->len - 1; i++) {
        g_free(g_ptr_array_index(argv, i));
    }
    g_ptr_array_free(argv, TRUE);
}
