/*
 * Kerberos pre-authentication, and changing a password that has expired.
 *
 * Why this exists at all: xrdp has no server-side NLA, so it validates
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
 *    reported and do stop the login - those would break it anyway, and
 *    saying so early is the whole point.
 */

#include <config.h>
#include <glib.h>
#include <libintl.h>
#include <stdlib.h>
#include <string.h>

#include <krb5.h>

#include "ldmgreetercomm.h"
#include "ldminfo.h"
#include "ldmkrb5.h"
#include "logging.h"

/* How many times to ask for a new password before giving up on the change. */
#define LDM_KRB5_CHANGE_TRIES 3

/*
 * Passwords do not linger in memory here. g_free alone would leave the
 * cleartext in the freed block for whatever allocates it next, and this
 * process goes on to start a session.
 */
static void
ldm_krb5_wipe(gchar *secret)
{
    if (secret == NULL)
        return;
    memset(secret, 0, strlen(secret));
    g_free(secret);
}

/* set_message() wants a non-const pointer and copies what it is given. */
static void
ldm_krb5_say(const gchar *text)
{
    gchar *copy = g_strdup(text);

    set_message(copy);
    g_free(copy);
}

/*
 * Ask the greeter for something typed blind, with our own wording.
 *
 * The greeter's protocol already has everything this needs - "prompt" sets
 * the label, "passwd" takes an answer with the entry hidden - so changing a
 * password needs no new commands and works with every existing theme. The
 * prompt is Pango markup and is settable, the same way the username and
 * password prompts are.
 */
static gchar *
ldm_krb5_ask_secret(const char *env_name, const gchar *fallback_text)
{
    gchar *fallback;
    const gchar *prompt;
    gchar *cmd;
    gchar *answer;

    fallback = g_strconcat("<b>", fallback_text, "</b>", NULL);
    prompt = ldm_getenv_str_default(env_name, fallback);
    cmd = g_strconcat("prompt ", prompt, "\npasswd\n", NULL);
    answer = ask_value_greeter(cmd);
    g_free(cmd);
    g_free(fallback);

    return answer;
}

/*
 * What the password server said, in its own words where it gave any.
 *
 * result_string comes from the KDC and is not NUL terminated, which is why
 * it is copied out by length. Active Directory puts the policy that was
 * violated in there - too short, too simple, changed too recently, reused -
 * and its wording is better than anything invented here, because it is the
 * rule the domain actually enforces.
 */
static gchar *
ldm_krb5_kpasswd_reason(int result_code, krb5_data *result_string)
{
    if (result_string != NULL && result_string->length > 0 &&
        result_string->data != NULL) {
        unsigned int len = result_string->length;
        unsigned int i;

        /*
         * Cut at the first NUL, then trim, rather than copying the whole
         * length: what comes back is a protocol field, not a C string, and
         * MIT kadmind puts a NUL after the sentence with more bytes behind
         * it. Copied wholesale, the text ends up with a NUL in the middle,
         * every later concatenation stops there, and the closing markup tag
         * falls off - so the greeter is handed "<b>New password is too
         * short." and shows the user nothing at all instead of the reason
         * their password was refused. Measured, not guessed.
         */
        for (i = 0; i < len; i++) {
            if (result_string->data[i] == 0) {
                len = i;
                break;
            }
        }
        while (len > 0 && g_ascii_isspace(result_string->data[len - 1]))
            len--;
        if (len > 0)
            return g_strndup(result_string->data, len);
    }

    switch (result_code) {
    case KRB5_KPASSWD_SOFTERROR:
        return g_strdup(gettext("The new password does not meet the rules "
                                "of this domain."));
    case KRB5_KPASSWD_ACCESSDENIED:
        return g_strdup(gettext("This account may not change its own "
                                "password."));
    case KRB5_KPASSWD_AUTHERROR:
    case KRB5_KPASSWD_HARDERROR:
    case KRB5_KPASSWD_MALFORMED:
    default:
        return g_strdup(gettext("The password server refused the change."));
    }
}

/*
 * Change a password that the KDC has just refused as expired.
 *
 * This rests on one exception in the protocol: a KDC issues a ticket for the
 * service kadmin/changepw even when the password has expired. That ticket is
 * good for nothing else, and it is what makes the change possible from a
 * login screen at all - without it the user is told to change a password on
 * a machine that offers no way to change it.
 *
 * Returns the new password on success, and the caller has to use it from
 * then on. On failure it returns NULL and sets *why to something worth
 * showing.
 */
static gchar *
ldm_krb5_change_password(krb5_context ctx, krb5_principal princ,
                         const gchar *old_password, gchar **why)
{
    krb5_creds creds;
    krb5_get_init_creds_opt *opt = NULL;
    krb5_error_code rc;
    gchar *accepted = NULL;
    int tries;

    memset(&creds, 0, sizeof(creds));

    if (krb5_get_init_creds_opt_alloc(ctx, &opt) != 0) {
        *why = g_strdup(gettext("The password for this account has "
                                "expired."));
        return NULL;
    }

    rc = krb5_get_init_creds_password(ctx, &creds, princ,
                                      (char *) old_password, NULL, NULL, 0,
                                      "kadmin/changepw", opt);
    if (rc != 0) {
        const char *detail = krb5_get_error_message(ctx, rc);

        log_entry("ldm", 3, "kerberos: no changepw ticket: %s",
                  detail ? detail : "unknown error");
        if (detail)
            krb5_free_error_message(ctx, detail);

        /*
         * Two different failures, and telling them apart matters to whoever
         * has to act on the message. In an Active Directory the change is
         * handled by the controller holding the PDC emulator role: signing
         * in survives the loss of any one controller, changing a password
         * does not, and that deserves saying rather than being blamed on the
         * password. Anything else - a realm with no password service, a
         * principal that may not change its own - is not a reachability
         * problem and must not be reported as one, or the administrator
         * goes looking at the wrong machine.
         */
        if (rc == KRB5_KDC_UNREACH || rc == KRB5_REALM_CANT_RESOLVE)
            *why = g_strdup(gettext("The password for this account has "
                                    "expired, and the server that could "
                                    "change it cannot be reached. Ask an "
                                    "administrator."));
        else
            *why = g_strdup(gettext("The password for this account has "
                                    "expired and cannot be changed from "
                                    "here. Ask an administrator."));
        krb5_get_init_creds_opt_free(ctx, opt);
        return NULL;
    }

    /*
     * "must be changed", not "has expired": the KDC answers a password that
     * ran out and one an administrator handed over as an initial password
     * with the same error, and in the second case nothing expired at all -
     * the account carries an attribute saying the user picks their own now.
     * Measured on MIT: such an account shows "Password expiration date:
     * [never]" and "Attributes: REQUIRES_PWCHANGE", and Active Directory's
     * pwdLastSet=0 is the same thing. A sentence that is true in both cases
     * is worth more than one that is precise in only one of them.
     */
    {
        gchar *fallback = g_strconcat("<b>",
                                      gettext("This password must be changed "
                                              "before signing in."),
                                      "</b>", NULL);

        ldm_krb5_say(ldm_getenv_str_default("LDM_NEWPASSWORD_MESSAGE",
                                            fallback));
        g_free(fallback);
    }

    for (tries = 0; tries < LDM_KRB5_CHANGE_TRIES; tries++) {
        gchar *first;
        gchar *second;
        int result_code = 0;
        krb5_data result_code_string;
        krb5_data result_string;

        first = ldm_krb5_ask_secret("LDM_NEWPASSWORD_PROMPT",
                                    gettext("New password"));
        if (first == NULL || *first == '\0') {
            ldm_krb5_wipe(first);
            break;
        }

        second = ldm_krb5_ask_secret("LDM_NEWPASSWORD_AGAIN_PROMPT",
                                     gettext("New password again"));
        if (second == NULL || *second == '\0') {
            ldm_krb5_wipe(first);
            ldm_krb5_wipe(second);
            break;
        }

        if (strcmp(first, second) != 0) {
            ldm_krb5_wipe(first);
            ldm_krb5_wipe(second);
            ldm_krb5_say(gettext("<b>The two entries did not match. "
                                 "Please try again.</b>"));
            continue;
        }
        ldm_krb5_wipe(first);

        memset(&result_code_string, 0, sizeof(result_code_string));
        memset(&result_string, 0, sizeof(result_string));

        /*
         * set_password, not change_password: it is the protocol from RFC
         * 3244, which is what Active Directory speaks, and it is what kpasswd
         * itself uses. Nothing is written anywhere on this client.
         */
        rc = krb5_set_password(ctx, &creds, (char *) second, NULL,
                               &result_code, &result_code_string,
                               &result_string);

        if (rc != 0) {
            const char *detail = krb5_get_error_message(ctx, rc);

            log_entry("ldm", 3, "kerberos: set_password failed: %s",
                      detail ? detail : "unknown error");
            if (detail)
                krb5_free_error_message(ctx, detail);
            ldm_krb5_wipe(second);
            *why = g_strdup(gettext("The password could not be changed. Ask "
                                    "an administrator."));
            krb5_free_data_contents(ctx, &result_code_string);
            krb5_free_data_contents(ctx, &result_string);
            break;
        }

        if (result_code == KRB5_KPASSWD_SUCCESS) {
            accepted = second;
            krb5_free_data_contents(ctx, &result_code_string);
            krb5_free_data_contents(ctx, &result_string);
            break;
        }

        {
            gchar *reason = ldm_krb5_kpasswd_reason(result_code,
                                                    &result_string);
            /* The domain wrote this, not us: an ampersand in it would be
             * markup the greeter cannot parse. */
            gchar *safe = g_markup_escape_text(reason, -1);
            gchar *shown = g_strconcat("<b>", safe, "</b>", NULL);

            /* The reason is the domain's, and only the reason is logged. */
            log_entry("ldm", 4, "kerberos: password change rejected (%d): %s",
                      result_code, reason);
            ldm_krb5_say(shown);
            g_free(shown);
            g_free(safe);
            g_free(reason);
        }

        ldm_krb5_wipe(second);
        krb5_free_data_contents(ctx, &result_code_string);
        krb5_free_data_contents(ctx, &result_string);
    }

    if (accepted == NULL && *why == NULL)
        *why = g_strdup(gettext("The password for this account has expired "
                                "and was not changed."));

    krb5_free_cred_contents(ctx, &creds);
    krb5_get_init_creds_opt_free(ctx, opt);

    return accepted;
}

/*
 * ldm_krb5_have_realm
 *
 * Is there a Kerberos realm to ask at all? RDP_PREAUTH_REALM names one
 * outright; failing that, krb5.conf may declare a default. Asked without
 * touching the network, because this only decides *which* check to run.
 */
gboolean
ldm_krb5_have_realm(void)
{
    const gchar *realm = getenv("RDP_PREAUTH_REALM");
    krb5_context ctx = NULL;
    char *default_realm = NULL;
    gboolean have;

    if (realm && *realm)
        return TRUE;

    if (krb5_init_context(&ctx) != 0)
        return FALSE;

    have = (krb5_get_default_realm(ctx, &default_realm) == 0);
    if (have)
        krb5_free_default_realm(ctx, default_realm);
    krb5_free_context(ctx);

    return have;
}

gchar *
ldm_krb5_preauth(const gchar *username, const gchar *password,
                 LdmPreauthVerdict *verdict, gchar **new_password)
{
    krb5_context ctx = NULL;
    krb5_principal princ = NULL;
    krb5_creds creds;
    krb5_get_init_creds_opt *opt = NULL;
    krb5_error_code rc;
    const gchar *realm = getenv("RDP_PREAUTH_REALM");
    gchar *principal_name = NULL;
    gchar *msg = NULL;
    LdmPreauthVerdict v = LDM_PREAUTH_UNAVAILABLE;

    /*
     * Pre-set, because every early return below means the same thing: the
     * question could not be put to anyone, so nothing was counted against
     * this client and nothing should be warned about.
     */
    if (verdict)
        *verdict = LDM_PREAUTH_UNAVAILABLE;
    if (new_password)
        *new_password = NULL;

    if (!username || !*username || !password)
        return NULL;

    memset(&creds, 0, sizeof(creds));

    if (krb5_init_context(&ctx) != 0) {
        log_entry("ldm", 4, "pre-auth skipped: no Kerberos context available");
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
            log_entry("ldm", 4,
                      "pre-auth skipped: no Kerberos realm configured "
                      "(set RDP_PREAUTH_REALM in lts.conf)");
            krb5_free_context(ctx);
            return NULL;
        }
        principal_name = g_strdup_printf("%s@%s", username, default_realm);
        krb5_free_default_realm(ctx, default_realm);
    }

    log_entry("ldm", 7, "pre-authenticating %s", principal_name);

    if (krb5_parse_name(ctx, principal_name, &princ) != 0) {
        log_entry("ldm", 4, "pre-auth skipped: cannot parse %s",
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
        log_entry("ldm", 6, "pre-auth succeeded for %s", username);
        v = LDM_PREAUTH_ACCEPTED;
        break;

    case KRB5KDC_ERR_PREAUTH_FAILED:
    case KRB5KRB_AP_ERR_BAD_INTEGRITY:
        msg = g_strdup(gettext("Wrong user name or password."));
        v = LDM_PREAUTH_REJECTED;
        break;

    case KRB5KDC_ERR_C_PRINCIPAL_UNKNOWN:
        msg = g_strdup(gettext("This user does not exist in the domain."));
        /*
         * Counted like a rejection: the KDC was asked and said no, and a
         * jail watching its log counts the attempt whether or not the name
         * existed. Typing a colleague's name wrongly three times gets the
         * client banned just the same.
         */
        v = LDM_PREAUTH_REJECTED;
        break;

    case KRB5KDC_ERR_CLIENT_REVOKED:
        msg = g_strdup(gettext("This account is locked or disabled."));
        v = LDM_PREAUTH_BARRED;
        break;

    case KRB5KDC_ERR_KEY_EXP:
        /*
         * The password has expired, or an administrator ticked "user must
         * change password at next logon", which the KDC reports the same
         * way. Either way this is the one place the user can do anything
         * about it: a thin client has no other means of changing a domain
         * password, and the session server would only repeat the refusal.
         */
        if (ldm_getenv_bool_default("RDP_PREAUTH_CHANGE_PASSWORD", 1)) {
            gchar *fresh;
            gchar *change_why = NULL;

            fresh = ldm_krb5_change_password(ctx, princ, password,
                                             &change_why);
            if (fresh != NULL) {
                krb5_error_code rc2;

                /*
                 * Changed - but the point of pre-auth is not to hand the
                 * session server something that will not work, so the new
                 * password is used for what it was meant for and asked
                 * about directly.
                 */
                memset(&creds, 0, sizeof(creds));
                rc2 = krb5_get_init_creds_password(ctx, &creds, princ,
                                                   (char *) fresh, NULL,
                                                   NULL, 0, NULL, opt);
                if (rc2 == 0) {
                    log_entry("ldm", 6,
                              "password changed and accepted for %s",
                              username);
                    krb5_free_cred_contents(ctx, &creds);
                    if (new_password != NULL) {
                        *new_password = fresh;
                    } else {
                        ldm_krb5_wipe(fresh);
                    }
                    g_free(change_why);
                    v = LDM_PREAUTH_ACCEPTED;
                    break;
                }

                log_entry("ldm", 3, "password changed for %s but the new one "
                          "was not accepted", username);
                ldm_krb5_wipe(fresh);
                g_free(change_why);
                msg = g_strdup(gettext("The password was changed, but "
                                       "signing in with it did not work. "
                                       "Try again."));
                v = LDM_PREAUTH_BARRED;
                break;
            }

            msg = change_why ? change_why
                : g_strdup(gettext("The password for this account has "
                                   "expired."));
            v = LDM_PREAUTH_BARRED;
            break;
        }

        msg = g_strdup(gettext("The password for this account has expired."));
        v = LDM_PREAUTH_BARRED;
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
        /*
         * This stops the login, deliberately. The credentials were never
         * checked by anyone, and starting a session with them would put the
         * user in xrdp's own dialog with no way back - the dead end this
         * whole check exists to avoid. Saying so plainly is the useful part:
         * the person at the screen can tell an administrator something true,
         * instead of retyping a password that was never the problem.
         */
        {
            const char *detail = krb5_get_error_message(ctx, rc);

            log_entry("ldm", 3, "pre-auth: no KDC answered for %s: %s",
                      principal_name, detail ? detail : "unknown error");
            if (detail)
                krb5_free_error_message(ctx, detail);
        }
        msg = g_strdup(gettext("The authentication server cannot be reached, "
                               "so signing in is not possible from here. The "
                               "password is not the problem - tell an "
                               "administrator."));
        break;

    default:
        {
            const char *detail = krb5_get_error_message(ctx, rc);

            log_entry("ldm", 3, "pre-auth failed for %s: %s",
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
    if (verdict)
        *verdict = v;

    g_free(principal_name);
    if (opt)
        krb5_get_init_creds_opt_free(ctx, opt);
    if (princ)
        krb5_free_principal(ctx, princ);
    krb5_free_context(ctx);

    return msg;
}
