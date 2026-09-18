#include <libintl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "ldminfo.h"
#include "ldmutils.h"
#include "ldmgreetercomm.h"
#include "logging.h"
#include "plugin.h"

/*
 * get_userid
 *  Ask the greeter to check autologin or get username
 */
void
get_userid(gchar ** username)
{
    gchar *cmd;
    gchar *fallback;
    const gchar *prompt;

    /*
     * The prompt is Pango markup and reaches the greeter unchanged, the same
     * way LDM_CLOCK_FORMAT does: a site that wants different wording, or a
     * colour that stands off its own background image, writes the whole
     * span. The default is the bold label the greeter has always drawn, and
     * it still goes through gettext, so nothing has to be set to keep a
     * translated greeter translated.
     */
    fallback = g_strconcat("<b>", _("Username"), "</b>", NULL);
    prompt = ldm_getenv_str_default("LDM_USERNAME_PROMPT", fallback);
    cmd = g_strconcat("prompt ", prompt, "\nuserid\n", NULL);
    *username = ask_value_greeter(cmd);

    g_free(cmd);
    g_free(fallback);
}

/*
 * get_passwd
 */
void
get_passwd(gchar ** password)
{
    gchar *cmd;
    gchar *fallback;
    gchar *message;
    const gchar *prompt;

    fallback = g_strconcat("<b>", _("Password"), "</b>", NULL);
    prompt = ldm_getenv_str_default("LDM_PASSWORD_PROMPT", fallback);
    cmd = g_strconcat("prompts ", prompt, "\npasswd\n", NULL);
    *password = (gchar *) ask_value_greeter(cmd);
    g_free(fallback);

    /*
     * set_message() copies what it is given, so the string built here has to
     * be freed by us. It was not, until now.
     */
    fallback = g_strconcat("<b>", _("Verifying password.  Please wait."),
                           "</b>", NULL);
    message = g_strdup(ldm_getenv_str_default("LDM_VERIFY_MESSAGE",
                                              fallback));
    set_message(message);
    g_free(message);
    g_free(fallback);
    g_free(cmd);
}

/*
 * get_host
 */
void
get_host(gchar ** server)
{
    gchar *cmd = "hostname\n";
    gchar *host;

    host = (gchar *) ask_value_greeter(cmd);
    if (g_ascii_strncasecmp(host, "None", 4)) {
        if (*server)
            g_free(*server);
        *server = host;
    }
}

/*
 * get_language
 */
void
get_language(gchar ** language)
{
    gchar *cmd = "language\n";
    gchar *lang;

    lang = (gchar *) ask_value_greeter(cmd);

    if (g_ascii_strncasecmp(lang, "None", 4)) {
        if (*language)
            g_free(*language);
        *language = lang;
        setenv("LDM_LANGUAGE", lang, 1);
    }
}

/*
 * get_session
 */
void
get_session(gchar ** session)
{
    gchar *cmd = "session\n";
    gchar *sess;

    sess = (gchar *) ask_value_greeter(cmd);

    if (g_ascii_strncasecmp(sess, "None", 4)) {
        if (*session)
            g_free(*session);
        *session = sess;
    }
}

/*
 * get_Xsession
 *
 * Return to variable xsession the LDM_XSESSION info
 */
void __attribute__ ((visibility("default"))) get_Xsession(gchar **
                                                          xsession,
                                                          gconstpointer
                                                          server)
{
    ldminfo *curr_host = NULL;
    gchar *tmp_xsess = NULL;
    tmp_xsess = g_strdup(getenv("LDM_XSESSION"));

    if (!tmp_xsess || strlen(tmp_xsess) == 0) {
        curr_host = ldminfo_lookup(server);
        if (curr_host) {
            tmp_xsess = curr_host->xsession;
            if (!tmp_xsess || strlen(tmp_xsess) == 0) {
                tmp_xsess = g_strdup(getenv("LDM_DEFAULT_XSESSION"));
                if (!tmp_xsess || strlen(tmp_xsess) == 0)
                    die("ldm", "no Xsession");
            }
        }
    }

    *xsession = tmp_xsess;
}

/*
 * set_session_env
 *
 * Set LDM_SESSION and LDM_XSESSION env variable with param
 * Then run xsession script
 */
void __attribute__ ((visibility("default"))) set_session_env(gchar *
                                                             xsession,
                                                             gchar *
                                                             session)
{
    if (g_strcmp0(session, "default") != 0
        || getenv("LDM_SESSION") == NULL) {
        log_entry("ldm", 7,
                  "Export LDM_SELECTED_SESSION to environment: \"%s\"",
                  session);
        setenv("LDM_SELECTED_SESSION", session, 1);
    } else {
        log_entry("ldm", 7,
                  "Using existing LDM_SESSION from environment: \"%s\"",
                  getenv("LDM_SESSION"));
    }

    setenv("LDM_XSESSION", xsession, 1);

    /* Starting xsession script */
    rc_files("xsession");
}

/*
 * get_ltsp_cfg
 *  Check for LTSP-Cluster. If true, contact loadbalancer for an IP
 *
 * *server points at a g_strdup()'d string (typically just a short
 * hostname), sized for whatever it originally held - NOT for PATH_MAX
 * bytes. Reading the load-balancer's reply straight into *server via
 * fgets(*server, PATH_MAX, fp), as this used to do, is a heap buffer
 * overflow: any reply longer than the original allocation (or a
 * compromised/misbehaving load-balancer) overwrites past the end of that
 * buffer. Read into a local, properly-sized buffer instead, and only
 * replace *server with a fresh allocation of that.
 */
void __attribute__ ((visibility("default"))) get_ltsp_cfg(gchar ** server)
{
    /* Check for LTSP-Cluster, if true, contact the loadbalancer for an IP */
    if (access(LTSP_CLUSTER_CONFILE, F_OK) == 0) {
        FILE *fp;
        fp = popen("getltscfg-cluster -l ldm", "r");
        if (fp != NULL) {
            gchar line[PATH_MAX];

            log_entry("ltsp-cluster", 6, "IP before load-balancing: %s",
                      *server);
            if (fgets(line, sizeof(line), fp) == NULL) {
                log_entry("ltsp-cluster", 4,
                          "failed to get an IP from the load-balancer");
            } else {
                g_strchomp(line);
                g_free(*server);
                *server = g_strdup(line);
            }

            log_entry("ltsp-cluster", 6, "IP after loadbalancing: %s",
                      *server);
        }
        if (pclose(fp) == -1)
            log_entry("ltsp-cluster", 3, "load-balancing failed");
    }
}
