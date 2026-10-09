/*
 * rdpsound.c - is there a sound server for xfreerdp's /sound and /microphone?
 *
 * FreeRDP's pulse backends do not degrade. If /sound:sys:pulse or
 * /microphone:sys:pulse is given and no PulseAudio server answers, the
 * channel fails to load ("pulse entry returns error 12") and xfreerdp ends
 * the whole connection right after the MCS exchange - no session, and
 * nothing on the screen but the greeter coming back. Measured against
 * xfreerdp 3.15.0: with a reachable server the connection stands, without
 * one it ends, for either option alone.
 *
 * That is how a thin client image without a sound server took every RDP
 * session down with it, and how a sound server running for root in
 * /run/user/0 still did when nothing told xfreerdp to look there.
 *
 * So before starting xfreerdp, ldm looks for the server the way libpulse
 * would, plus the one place libpulse would not look on its own:
 *
 *   PULSE_SERVER                      if set, as given
 *   $XDG_RUNTIME_DIR/pulse/native     the per-user server
 *   /run/user/<uid>/pulse/native      the same, when XDG_RUNTIME_DIR is unset -
 *                                     which it is for the greeter, started by
 *                                     systemd without a login session
 *   /run/pulse/native                 a system-wide server (pulseaudio --system)
 *
 * A socket counts only if something accepts a connection on it; a leftover
 * socket file from a server that died is not a server. Found, ldm passes it
 * on as PULSE_SERVER. Not found, the session starts without sound and
 * microphone, and the log says why - losing the sound is better than losing
 * the desk.
 */

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#include "rdpsound.h"

gboolean
rdp_sound_is_option(const gchar *arg)
{
    static const gchar *names[] = { "sound", "audio", "microphone", "mic", NULL };
    gint i;

    if (arg == NULL || (arg[0] != '/' && arg[0] != '+'))
        return FALSE;

    for (i = 0; names[i] != NULL; i++) {
        gsize n = strlen(names[i]);

        /* "/audio-mode:0" is not a channel and must stay: only the bare
         * name, or the name followed by its options. */
        if (strncmp(arg + 1, names[i], n) == 0
            && (arg[1 + n] == '\0' || arg[1 + n] == ':'))
            return TRUE;
    }
    return FALSE;
}

/* Does something accept connections on this unix socket? */
static gboolean
socket_answers(const gchar *path)
{
    struct sockaddr_un addr;
    int fd;
    gboolean ok;

    if (path == NULL || strlen(path) >= sizeof(addr.sun_path))
        return FALSE;

    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return FALSE;

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strcpy(addr.sun_path, path);
    ok = connect(fd, (struct sockaddr *) &addr, sizeof(addr)) == 0;
    close(fd);
    return ok;
}

gchar *
rdp_sound_find_server(gchar **looked)
{
    GString *tried = g_string_new(NULL);
    const gchar *env;
    gchar *path;
    gchar *found = NULL;

    env = g_getenv("PULSE_SERVER");
    if (env != NULL && *env != '\0') {
        /* The first entry of the list decides, as for libpulse. A unix
         * socket can be checked; anything else (tcp:...) is taken as
         * configured - checking it would mean speaking the protocol. */
        gchar **entries = g_strsplit_set(env, " \t", 2);
        const gchar *first = entries[0];
        const gchar *sock = g_str_has_prefix(first, "unix:") ? first + 5
                            : (first[0] == '/' ? first : NULL);

        g_string_append_printf(tried, "PULSE_SERVER=%s", env);
        if (sock == NULL || socket_answers(sock))
            found = g_strdup(env);
        g_strfreev(entries);
        goto done;
    }

    env = g_getenv("XDG_RUNTIME_DIR");
    if (env != NULL && *env != '\0') {
        path = g_build_filename(env, "pulse", "native", NULL);
        g_string_append(tried, path);
        if (socket_answers(path))
            found = g_strconcat("unix:", path, NULL);
        g_free(path);
        if (found)
            goto done;
    }

    path = g_strdup_printf("/run/user/%u/pulse/native", (guint) getuid());
    if (tried->len)
        g_string_append(tried, ", ");
    g_string_append(tried, path);
    if (socket_answers(path))
        found = g_strconcat("unix:", path, NULL);
    g_free(path);
    if (found)
        goto done;

    g_string_append(tried, ", /run/pulse/native");
    if (socket_answers("/run/pulse/native"))
        found = g_strdup("unix:/run/pulse/native");

done:
    if (looked)
        *looked = g_string_free(tried, FALSE);
    else
        g_string_free(tried, TRUE);
    return found;
}
