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
#include <utmp.h>

#include "../../ldmutils.h"
#include "../../ldmgreetercomm.h"
#include "../../logging.h"
#include "../../plugin.h"
#include "xfreerdp.h"

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
 * init_xfreerdp
 *  Callback function for initialization
 */
void
init_xfreerdp()
{
    rdpinfo = (RdpInfo *) malloc(sizeof(RdpInfo));
    bzero(rdpinfo, sizeof(RdpInfo));

    /* Get ENV value */
    rdpinfo->rdpoptions = g_strdup(getenv("RDP_OPTIONS"));
    rdpinfo->server = g_strdup(getenv("RDP_SERVER"));
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

    log_entry("xfreerdp", 6, "starting xfreerdp session to '%s' as '%s'",
              rdpinfo->server, rdpinfo->username);
    xfreerdp_session();
    log_entry("xfreerdp", 6, "closing xfreerdp session");
}

/*
 * _get_domain
 */
void
_get_domain()
{
    gchar *cmd = "value domain\n";

    rdpinfo->domain = ask_value_greeter(cmd);
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
 */
void
xfreerdp_session()
{
    gchar *cmd;

    /* The Password should not contain space(s) character(s) */
   cmd = g_strjoin(NULL, " ", "xfreerdp", " " , 
                    "/u:", rdpinfo->username, 
                    " ",
                    "/p:", rdpinfo->password, NULL);

    /* Only append the domain if it's set */
    if (g_strcmp0(rdpinfo->domain, "None") != 0) {
        cmd = g_strjoin(" ", cmd, "/d:", rdpinfo->domain, NULL);
    }

    /* If we have custom options, append them */
    if (rdpinfo->rdpoptions) {
        cmd = g_strjoin(" ", cmd, rdpinfo->rdpoptions, NULL);
    }

    /* Append Option for RDP-Server and Display Full-Screen */
    cmd = g_strconcat(cmd, " ", "/v:", rdpinfo->server, " ", "/f", NULL);

    /* Set Environment for xfreerdp INFO logging and important xfreerdp needs to set the "HOME" to /root otherwise xfreerdp is not working! */
    setenv("WLOG_LEVEL", "INFO", 1);
    setenv("WLOG_APPENDER", "SYSLOG", 1);
    setenv("HOME", "/root", 1);
    
    /* Set Enviroment??? for LIBVA_DRIVER_NAME=i965 -> older INTEL Graphics Card  OR  LIBVA_DRIVER_NAME=iHD -> newer INTEL Graphics Card */
    /* For newer verions of freerdp the hardware acceleration over ffmpeg will not use when the LIBVA_DRIVER_NAME=XXXX not set...???? */

    /* Spawning xfreerdp session */
    rdpinfo->rdppid = ldm_spawn(cmd, NULL, NULL, NULL);
    ldm_wait(rdpinfo->rdppid);

    g_free(cmd);
}
