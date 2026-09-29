#ifndef LDMKRB5_H
#define LDMKRB5_H

#include <glib.h>

/*
 * What a pre-auth attempt actually established. The message alone cannot
 * say: "Wrong user name or password." and "The authentication server cannot
 * be reached." are both a non-NULL return, but only the first one is an
 * attempt that some server counted against this client.
 *
 * The distinction is the whole basis of the lockout warning in the session
 * plugin, so it is carried explicitly rather than inferred from the text - a
 * translated string is not a protocol.
 *
 * The enum lives here rather than in the plugin because the ssh probe there
 * answers in the same terms, and two spellings of the same four answers is
 * how they drift apart.
 */
typedef enum {
    LDM_PREAUTH_ACCEPTED = 0,   /* the credentials are good */
    LDM_PREAUTH_REJECTED,       /* a server refused these credentials */
    LDM_PREAUTH_BARRED,         /* the account itself is locked or expired */
    LDM_PREAUTH_UNAVAILABLE     /* the question could not be asked */
} LdmPreauthVerdict;

/* Is there a realm to ask at all? Decided without touching the network. */
gboolean ldm_krb5_have_realm(void);

/*
 * Ask the KDC whether these credentials are good, before a session is
 * started with them.
 *
 * Returns NULL when they are, or when the question could not be put to
 * anyone; otherwise a message for the greeter, which the caller owns.
 *
 * new_password may be NULL. When it is not, and the account's password had
 * expired and was changed here, it receives the new one - the caller has to
 * use that from then on, because the old one is no longer the account's
 * password. The caller owns it.
 */
gchar *ldm_krb5_preauth(const gchar *username, const gchar *password,
                        LdmPreauthVerdict *verdict, gchar **new_password);

#endif
