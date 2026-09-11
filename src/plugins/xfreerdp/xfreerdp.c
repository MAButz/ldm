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

#include <krb5.h>

#include "../../ldmutils.h"
#include "../../ldmpty.h"
#include "../../ldmgreetercomm.h"
#include "../../ldmplugin.h"
#include "../../logging.h"
#include "../../plugin.h"
#include "xfreerdp.h"

int screen;

LdmBackend *descriptor;
RdpInfo *rdpinfo;

/*
 * xfreerdp's exit codes, from FreeRDP's xf_exit_code_t. Only the low range is
 * stable across releases and documented; the 128+ range varies between
 * versions, which is why unknown values are reported numerically below rather
 * than guessed at.
 */
/* Printed by the pre-auth probe to prove the login got all the way through. */
#define RDP_PREAUTH_SENTINEL               "LTSPROCKS"

#define XF_EXIT_SUCCESS                    0
#define XF_EXIT_DISCONNECT                 1
#define XF_EXIT_LOGOFF                     2
#define XF_EXIT_IDLE_TIMEOUT               3
#define XF_EXIT_LOGON_TIMEOUT              4
#define XF_EXIT_CONN_REPLACED              5
#define XF_EXIT_OUT_OF_MEMORY              6
#define XF_EXIT_CONN_DENIED                7
#define XF_EXIT_CONN_DENIED_FIPS           8
#define XF_EXIT_USER_PRIVILEGES            9
#define XF_EXIT_FRESH_CREDENTIALS_REQUIRED 10
#define XF_EXIT_DISCONNECT_BY_USER         11

/*
 * rdp_preauth_kerberos
 *
 * Check the credentials against the Kerberos KDC before starting the RDP
 * client, and return a message to show the user on failure (NULL when the
 * credentials are good, or when the check could not be performed at all).
 *
 * Why this is needed at all: xrdp has no server-side NLA, so it validates
 * credentials only *inside* the session. A wrong password therefore does not
 * end the connection - xrdp opens its own login dialog inside the session and
 * waits there. The RDP client stays connected, learns nothing, and ldm never
 * regains control, so the user is left in a dialog that ldm cannot annotate
 * and cannot escape from except by disconnecting. Verified against xrdp
 * 0.10.1: after "AUTHFAIL" in its log the connection stayed up until the
 * client was killed a minute later.
 *
 * Asking the KDC first turns that dead end into a precise message in the
 * greeter, and a wrong password never reaches the session server.
 *
 * Notes on what this deliberately does *not* do:
 *  - It does not make the client a domain member: there is no machine
 *    account and no keytab involved, only the user's own credentials. The
 *    client gains nothing the user does not already have.
 *  - The ticket is discarded immediately. We want a yes/no answer, not
 *    credentials to keep, so nothing is written to a credential cache.
 *  - It fails *open* on configuration problems (no realm known, Kerberos not
 *    usable): a lab that has not set this up should not lose the ability to
 *    log in. Credential and reachability problems, by contrast, are
 *    reported - those would break the login anyway, and saying so early is
 *    the whole point.
 */
static gchar *
rdp_preauth_kerberos(const gchar *username, const gchar *password)
{
    krb5_context ctx = NULL;
    krb5_principal princ = NULL;
    krb5_creds creds;
    krb5_get_init_creds_opt *opt = NULL;
    krb5_error_code rc;
    const gchar *realm = getenv("RDP_PREAUTH_REALM");
    gchar *principal_name = NULL;
    gchar *msg = NULL;

    if (!username || !*username || !password)
        return NULL;

    memset(&creds, 0, sizeof(creds));

    if (krb5_init_context(&ctx) != 0) {
        log_entry("xfreerdp", 4,
                  "pre-auth skipped: no Kerberos context available");
        return NULL;
    }

    /*
     * An explicit realm from lts.conf wins; otherwise fall back to whatever
     * /etc/krb5.conf declares. If neither exists there is nothing to ask, so
     * skip rather than block the login.
     */
    if (realm && *realm) {
        principal_name = g_strdup_printf("%s@%s", username, realm);
    } else {
        char *default_realm = NULL;

        if (krb5_get_default_realm(ctx, &default_realm) != 0) {
            log_entry("xfreerdp", 4,
                      "pre-auth skipped: no Kerberos realm configured "
                      "(set RDP_PREAUTH_REALM in lts.conf)");
            krb5_free_context(ctx);
            return NULL;
        }
        principal_name = g_strdup_printf("%s@%s", username, default_realm);
        krb5_free_default_realm(ctx, default_realm);
    }

    log_entry("xfreerdp", 7, "pre-authenticating %s", principal_name);

    if (krb5_parse_name(ctx, principal_name, &princ) != 0) {
        log_entry("xfreerdp", 4, "pre-auth skipped: cannot parse %s",
                  principal_name);
        goto out;
    }

    if (krb5_get_init_creds_opt_alloc(ctx, &opt) != 0)
        goto out;
    /* Nothing is stored, so no cache needs to be addressed. */
    krb5_get_init_creds_opt_set_forwardable(opt, 0);
    krb5_get_init_creds_opt_set_proxiable(opt, 0);

    rc = krb5_get_init_creds_password(ctx, &creds, princ,
                                      (char *) password, NULL, NULL, 0,
                                      NULL, opt);

    switch (rc) {
    case 0:
        log_entry("xfreerdp", 6, "pre-auth succeeded for %s", username);
        break;

    case KRB5KDC_ERR_PREAUTH_FAILED:
    case KRB5KRB_AP_ERR_BAD_INTEGRITY:
        msg = g_strdup(gettext("Wrong user name or password."));
        break;

    case KRB5KDC_ERR_C_PRINCIPAL_UNKNOWN:
        msg = g_strdup(gettext("This user does not exist in the domain."));
        break;

    case KRB5KDC_ERR_CLIENT_REVOKED:
        msg = g_strdup(gettext("This account is locked or disabled."));
        break;

    case KRB5KDC_ERR_KEY_EXP:
        msg = g_strdup(gettext("The password for this account has expired."));
        break;

    case KRB5KRB_AP_ERR_SKEW:
        /*
         * Worth naming precisely: it looks exactly like a wrong password from
         * the outside, and no amount of retyping fixes it.
         */
        msg = g_strdup(gettext("This computer's clock differs too much from "
                               "the server's; ask an administrator."));
        break;

    case KRB5_KDC_UNREACH:
    case KRB5_REALM_CANT_RESOLVE:
        msg = g_strdup(gettext("The authentication server cannot be reached."));
        break;

    default:
        {
            const char *detail = krb5_get_error_message(ctx, rc);

            log_entry("xfreerdp", 3, "pre-auth failed for %s: %s",
                      username, detail ? detail : "unknown error");
            msg = g_strdup(gettext("Sign-in failed."));
            if (detail)
                krb5_free_error_message(ctx, detail);
        }
        break;
    }

    if (rc == 0)
        krb5_free_cred_contents(ctx, &creds);

  out:
    g_free(principal_name);
    if (opt)
        krb5_get_init_creds_opt_free(ctx, opt);
    if (princ)
        krb5_free_principal(ctx, princ);
    krb5_free_context(ctx);

    return msg;
}

/*
 * rdp_preauth_ssh
 *
 * The same idea as rdp_preauth_kerberos() for sites that have no domain:
 * ask a host whether it accepts these credentials, before handing them to
 * a client that cannot report a rejection.
 *
 * Where Kerberos asks a KDC about a principal, this asks sshd about a
 * local account - and by doing so it tests the *PAM stack of the machine
 * that will run the session*, not just whether a password is right. That
 * is a broader check than the Kerberos one: an expired account, a
 * pam_access rule, an sssd access rule all deny here too. This lab has
 * already spent an afternoon on exactly such a case, where the password
 * was correct and pam_acct_mgmt refused the service anyway.
 *
 * What it costs, and it is worth being clear about it: unlike Kerberos,
 * which never puts the password on the wire, this sends the password to
 * whatever answers on that address. On a thin client booting a read-only
 * image there is no persistent known_hosts, so trust-on-first-use never
 * gets past "first use" - every boot is the first time. The host key
 * therefore has to be known in advance, shipped in the image, and
 * StrictHostKeyChecking must stay on. Turning it off would not merely
 * weaken the check; it would turn it into a password collector for anyone
 * able to answer on that address.
 *
 * Fails open on anything that means "the question could not be asked" -
 * no host configured, ssh missing, the server refusing password
 * authentication outright - for the same reason the Kerberos version
 * does: a check that cannot run must not lock people out of a server that
 * works. It does NOT fail open on a host key failure, because that is the
 * one outcome the check exists to catch.
 *
 * Returns NULL to proceed, or a message to show in the greeter.
 */
static gchar *
rdp_preauth_ssh(const gchar *username, const gchar *password)
{
    const gchar *host = getenv("RDP_PREAUTH_HOST");
    const gchar *known = getenv("RDP_PREAUTH_SSH_KNOWN_HOSTS");
    GPtrArray *argv;
    gchar buf[LDM_PTY_MAXBUF];
    gchar *msg = NULL;
    GPid pid;
    int fd = -1, seen;
    guint i;

    if (!username || !*username || !password)
        return NULL;

    if (!host || !*host)
        host = getenv("RDP_SERVER");
    if (!host || !*host) {
        log_entry("ldm", 4, "rdp_preauth_ssh: no RDP_PREAUTH_HOST and no "
                  "RDP_SERVER, skipping the check");
        return NULL;
    }
    if (!known || !*known)
        known = "/etc/ssh/ssh_known_hosts";

    argv = g_ptr_array_new();
    g_ptr_array_add(argv, g_strdup("ssh"));
    /*
     * Password authentication only. Without this a client that happens to
     * carry a usable key would authenticate with it and the check would
     * pass whatever the user typed - which is worse than no check at all,
     * because it looks like one.
     */
    g_ptr_array_add(argv, g_strdup("-o"));
    g_ptr_array_add(argv, g_strdup("PreferredAuthentications=password"));
    g_ptr_array_add(argv, g_strdup("-o"));
    g_ptr_array_add(argv, g_strdup("PubkeyAuthentication=no"));
    g_ptr_array_add(argv, g_strdup("-o"));
    g_ptr_array_add(argv, g_strdup("GSSAPIAuthentication=no"));
    /* One prompt: a second one means the first was refused. */
    g_ptr_array_add(argv, g_strdup("-o"));
    g_ptr_array_add(argv, g_strdup("NumberOfPasswordPrompts=1"));
    /* Shorter than the expect timeout below, so ssh's own message wins. */
    g_ptr_array_add(argv, g_strdup("-o"));
    g_ptr_array_add(argv, g_strdup("ConnectTimeout=10"));
    /* The point of the whole exercise - see the comment above. */
    g_ptr_array_add(argv, g_strdup("-o"));
    g_ptr_array_add(argv, g_strdup("StrictHostKeyChecking=yes"));
    g_ptr_array_add(argv, g_strdup("-o"));
    g_ptr_array_add(argv, g_strconcat("UserKnownHostsFile=", known, NULL));
    /*
     * The user name goes in its own argv element rather than as user@host.
     * It comes straight from the greeter, before anything has been
     * authenticated, and ssh reads a leading "-" as an option.
     */
    g_ptr_array_add(argv, g_strdup("-l"));
    g_ptr_array_add(argv, g_strdup(username));
    g_ptr_array_add(argv, g_strdup(host));
    /*
     * Printing a sentinel proves more than an exit status would: it says
     * authentication finished *and* the account can run something, which
     * is what the session is about to need anyway.
     */
    g_ptr_array_add(argv, g_strdup("echo " RDP_PREAUTH_SENTINEL));
    g_ptr_array_add(argv, NULL);

    pid = ldm_pty_spawn((gchar **) argv->pdata, &fd);

    for (i = 0; i < argv->len - 1; i++)
        g_free(g_ptr_array_index(argv, i));
    g_ptr_array_free(argv, TRUE);

    if (pid <= 0) {
        log_entry("ldm", 4, "rdp_preauth_ssh: could not start ssh, skipping");
        return NULL;
    }

    /* First: a password prompt, or an answer without one. */
    seen = ldm_pty_expect(fd, buf, 20, "assword:", RDP_PREAUTH_SENTINEL,
                          "Permission denied", "Host key verification failed",
                          NULL);
    if (seen == 0) {
        /*
         * Checked, unlike the equivalent in the ssh backend: if the write
         * fails there is no point waiting twenty seconds for an answer to a
         * question that was never asked.
         */
        if (write(fd, password, strlen(password)) < 0
            || write(fd, "\n", 1) < 0) {
            log_entry("ldm", 4, "rdp_preauth_ssh: could not send the "
                      "password, skipping the check");
            close(fd);
            kill(pid, SIGTERM);
            ldm_wait(pid);
            return NULL;
        }
        seen = ldm_pty_expect(fd, buf, 20, RDP_PREAUTH_SENTINEL,
                              "Permission denied",
                              "Host key verification failed", "assword:",
                              NULL);
        /* Shift so the cases below read the same either way. */
        if (seen == 0)
            seen = 1;
        else if (seen == 1)
            seen = 2;
        else if (seen == 2)
            seen = 3;
        else if (seen == 3)
            seen = 2;                            /* asked again == refused */
    }

    if (seen == 1) {
        msg = NULL;                              /* the sentinel: accepted */
    } else if (seen == 2) {
        /*
         * "Permission denied (publickey)" means the server never offered
         * password authentication, so nothing was tested. Saying "wrong
         * password" there would be a lie, and a confusing one, because the
         * same credentials work in the session a moment later.
         */
        if (strstr(buf, "publickey")) {
            log_entry("ldm", 4, "rdp_preauth_ssh: %s refuses password "
                      "authentication, skipping the check", host);
            msg = NULL;
        } else {
            msg = g_strdup(gettext("Wrong user name or password."));
        }
    } else if (seen == 3) {
        /*
         * Not failing open here is the whole point: an unknown or changed
         * host key is the case this check exists to catch, and proceeding
         * would send the password to whatever answered.
         */
        msg = g_strdup(gettext("The session server could not be verified. "
                               "Tell your administrator before trying again."));
    } else if (strstr(buf, "Connection refused")
               || strstr(buf, "No route to host")
               || strstr(buf, "Connection timed out")
               || seen == LDM_PTY_TIMED_OUT) {
        msg = g_strdup(gettext("Cannot reach the authentication server."));
    } else {
        /* Something unexpected. Log it and let the connection proceed. */
        log_entry("ldm", 4, "rdp_preauth_ssh: inconclusive (%d), skipping: %s",
                  seen, buf);
        msg = NULL;
    }

    /* Nothing here waits for a session; close the door behind us. */
    if (fd >= 0)
        close(fd);
    kill(pid, SIGTERM);
    ldm_wait(pid);

    memset(buf, 0, sizeof buf);
    return msg;
}

/*
 * rdp_exit_message
 *
 * Turn xfreerdp's exit status into something the person at the screen can act
 * on, and NULL when the session simply ended.
 *
 * Without this the greeter reports every outcome identically, while the part
 * that says what went wrong stays in the RDP server's logs - where a user
 * cannot see it and an admin only looks after being told there is a problem.
 * Three separate faults in this lab (a keyboard layout that mistyped the
 * password, an sssd access rule that denied the service, and a load balancer
 * pair that both claimed the same address) all presented as the same blank
 * failure, which is what made them slow to tell apart.
 *
 * Note the deliberate asymmetry: a wrong password is worth naming precisely,
 * whereas "denied" is reported as denied without speculating about why - the
 * RDP protocol carries a status code, not the server's reasoning.
 */
static const gchar *
rdp_exit_message(int status)
{
    switch (status) {
    case XF_EXIT_SUCCESS:
    case XF_EXIT_DISCONNECT:
    case XF_EXIT_LOGOFF:
    case XF_EXIT_DISCONNECT_BY_USER:
        /* A session that ended normally is not something to report. */
        return NULL;

    case XF_EXIT_IDLE_TIMEOUT:
        return gettext("Session closed: idle for too long.");

    case XF_EXIT_LOGON_TIMEOUT:
        return gettext("The server did not complete the logon in time.");

    case XF_EXIT_CONN_REPLACED:
        return gettext("This session was taken over by another connection.");

    case XF_EXIT_OUT_OF_MEMORY:
        return gettext("The session server ran out of memory.");

    case XF_EXIT_CONN_DENIED:
    case XF_EXIT_CONN_DENIED_FIPS:
        return gettext("The session server refused the connection.");

    case XF_EXIT_USER_PRIVILEGES:
        return gettext("This account is not allowed to log on remotely.");

    case XF_EXIT_FRESH_CREDENTIALS_REQUIRED:
        return gettext("Wrong user name or password.");

    case -1:
        /* ldm_wait() reports -1 when the client was killed by a signal. */
        return gettext("The remote desktop client was terminated.");

    default:
        return NULL;
    }
}

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

    /*
     * Optionally verify the credentials before handing them to the RDP
     * client, which cannot report a rejection. Off unless RDP_PREAUTH is set
     * in lts.conf: RDP_PREAUTH=True asks a KDC, RDP_PREAUTH=ssh asks an sshd.
     * See rdp_preauth_kerberos() and rdp_preauth_ssh() for what each buys and
     * what each costs.
     */
    {
        const gchar *preauth = getenv("RDP_PREAUTH");
        gchar *why = NULL;

        /*
         * "ssh" picks the sshd probe, anything true-ish (the original
         * spelling was RDP_PREAUTH=True) picks Kerberos. Keeping True
         * meaning Kerberos matters: it is what existing lts.conf files
         * say, and silently changing what they do would be worse than
         * having no second method at all.
         */
        if (preauth && g_ascii_strcasecmp(preauth, "ssh") == 0)
            why = rdp_preauth_ssh(rdpinfo->username, rdpinfo->password);
        else if (ldm_getenv_bool("RDP_PREAUTH")
                 || (preauth && g_ascii_strncasecmp(preauth, "krb", 3) == 0)
                 || (preauth && g_ascii_strcasecmp(preauth, "kerberos") == 0))
            why = rdp_preauth_kerberos(rdpinfo->username, rdpinfo->password);

        if (why) {
            set_message(why);
            g_free(why);

            /* Wipe the rejected password rather than carry it around. */
            if (rdpinfo->password) {
                memset(rdpinfo->password, 0, strlen(rdpinfo->password));
                g_free(rdpinfo->password);
                rdpinfo->password = NULL;
            }

            /*
             * Unwind back to ldm's auth loop, which redisplays the greeter -
             * with the message above still shown - instead of starting a
             * session that we already know will not authenticate.
             */
            ldm_raise_auth_except(AUTH_EXC_RELOAD_BACKEND);
        }
    }

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

    {
        int status = ldm_wait(rdpinfo->rdppid);
        const gchar *msg = rdp_exit_message(status);

        if (msg) {
            /* Shown in the greeter, which is the only place the user looks. */
            log_entry("xfreerdp", 3, "xfreerdp exited with status %d: %s",
                      status, msg);
            set_message((gchar *) msg);
        } else if (status > XF_EXIT_DISCONNECT_BY_USER) {
            /*
             * Codes above the documented range differ between FreeRDP
             * releases, so report the number instead of inventing a meaning
             * for it - a wrong explanation costs more time than none.
             */
            gchar *unknown = g_strdup_printf(
                gettext("Connection failed (remote desktop client code %d)."),
                status);

            log_entry("xfreerdp", 3, "xfreerdp exited with status %d", status);
            set_message(unknown);
            g_free(unknown);
        } else {
            log_entry("xfreerdp", 6, "xfreerdp exited with status %d", status);
        }
    }

    for (i = 0; i < argv->len - 1; i++) {
        g_free(g_ptr_array_index(argv, i));
    }
    g_ptr_array_free(argv, TRUE);
}
