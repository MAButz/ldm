#include <config.h>
#include <ctype.h>
#include <fcntl.h>
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
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <utmp.h>
#include <crypt.h>
#include <errno.h>

#include "../../ldm.h"
#include "../../ldminfo.h"
#include "../../ldmutils.h"
#include "../../ldmpty.h"
#include "../../ldmgreetercomm.h"
#include "../../logging.h"
#include "../../plugin.h"
#include "ssh.h"

#define SENTINEL "LTSPROCKS"

LdmBackend *descriptor;
SSHInfo *sshinfo;

void __attribute__ ((constructor)) initialize()
{
    descriptor = (LdmBackend *) malloc(sizeof(LdmBackend));
    bzero(descriptor, sizeof(LdmBackend));

    descriptor->name = "ssh";
    descriptor->description = "ssh plugin";
    descriptor->auth_cb = get_auth;
    descriptor->clean_cb = close_ssh;
    descriptor->guest_cb = get_guest;
    descriptor->init_cb = init_ssh;
    descriptor->start_cb = start_ssh;
    ldm_init_plugin(descriptor);
}

/*
 * init_ssh
 *  Callback function for initialization
 */
void
init_ssh()
{
    sshinfo = (SSHInfo *) malloc(sizeof(SSHInfo));
    bzero(sshinfo, sizeof(SSHInfo));

    /* Get ENV Variables */
    sshinfo->sshoptions = g_strdup(getenv("LDM_SSHOPTIONS"));
    sshinfo->override_port = g_strdup(getenv("SSH_OVERRIDE_PORT"));
}

/*
 * start_ssh
 *  Start ssh session
 */
void
start_ssh()
{
    gboolean error = FALSE;

    /*
     * Variable validation. Empty counts as absent: a zero length string
     * passes a NULL check and is not a credential. The guest path above
     * fills both in with something real, so nothing legitimate is caught
     * here.
     */
    if (!(sshinfo->username) || !*(sshinfo->username)) {
        log_entry("ssh", 3, "no username");
        error = TRUE;
    }

    if (!(sshinfo->password) || !*(sshinfo->password)) {
        log_entry("ssh", 3, "no password");
        error = TRUE;
    }

    if (!(sshinfo->server)) {
        log_entry("ssh", 3, "no server");
        error = TRUE;
    }

    if (!(sshinfo->session))
        sshinfo->session = g_strdup("default");

    if (error) {
        die("ssh", "missing mandatory information");
    }

    /* Getting Xsession */
    get_Xsession(&(sshinfo->xsession), sshinfo->server);

    /* Check if we are loadbalanced */
    get_ltsp_cfg(&(sshinfo->server));

    /*
     * If we run multiple ldm sessions on multiply vty's we need separate
     * control sockets.
     */
    sshinfo->ctl_socket =
        g_strdup_printf("/var/run/ldm_socket_%d_%s", ldm.pid,
                        sshinfo->server);

    /* Setting ENV variables for plugin */
    _set_env();

    /* Execute any rc files */
    log_entry("ssh", 6, "calling rc.d pressh scripts");
    rc_files("pressh");

    ssh_session();
    log_entry("ssh", 6, "established ssh session on '%s' as '%s'",
              sshinfo->server, sshinfo->username);

    /* Greeter not needed anymore */
    close_greeter();

    log_entry("ssh", 6, "calling rc.d start scripts");
    rc_files("start");                           /* Execute any rc files */

    /* ssh_hashpass - Defaults to opt-in (Must set LDM_PASSWORD_HASH to true) */
    if (ldm_getenv_bool_default("LDM_PASSWORD_HASH", FALSE))
    {
        ssh_hashpass();
    }
    else
    {
        log_entry("hashpass", 6, "LDM_PASSWORD_HASH set to FALSE or unset, skipping hash function");
    }
    log_entry("hashpass", 6, "Freeing password as promised.");
    g_free(sshinfo->password);
    sshinfo->password = NULL;

    log_entry("ssh", 6, "starting X session");
    set_session_env(sshinfo->xsession, sshinfo->session);
}

/*
 * get_guest
 *  Callback function for setting guest login
 */
void
get_guest()
{
    log_entry("ssh", 6, "setting guest login");

    /* Get credentials */
    g_free(sshinfo->username);
    g_free(sshinfo->password);

    /* Get UserID */
    sshinfo->username = g_strdup(getenv("LDM_USERNAME"));

    /* Get password */
    sshinfo->password = g_strdup(getenv("LDM_PASSWORD"));


    /* Don't ask anything from the greeter when on autologin */
    if (!ldm_getenv_bool("LDM_AUTOLOGIN")) {
        /* Get hostname */
        get_host(&(sshinfo->server));

        /* Get Language */
        get_language(&(sshinfo->lang));

        /* Get Session */
        get_session(&(sshinfo->session));
    }

    if (!sshinfo->username) {
        gchar hostname[HOST_NAME_MAX + 1];      /* +1 for \0 terminator */
        gethostname(hostname, sizeof hostname);

        sshinfo->username = g_strdup(hostname);
    }
    if (!sshinfo->password)
        sshinfo->password = g_strdup(sshinfo->username);

    {
        char **hosts_char = NULL;
        gchar *autoservers = NULL;
        gboolean good;
        int i;

        autoservers = g_strdup(getenv("LDM_GUEST_SERVER"));
        if (!autoservers)
            autoservers = g_strdup(getenv("LDM_AUTOLOGIN_SERVER"));

        if (!autoservers)
            autoservers = g_strdup(getenv("LDM_SERVER"));

        /* g_strsplit(NULL, ...) returns NULL, and hosts_char[0] below
         * would then be a NULL-pointer dereference: none of
         * LDM_GUEST_SERVER/LDM_AUTOLOGIN_SERVER/LDM_SERVER are set, so
         * there's no server to log the guest session into at all. */
        if (!autoservers)
            die("ssh",
                "guest login: no server configured (LDM_GUEST_SERVER/"
                "LDM_AUTOLOGIN_SERVER/LDM_SERVER are all unset)");

        hosts_char = g_strsplit(autoservers, " ", -1);

        good = FALSE;
        if (sshinfo->server) {
            i = 0;
            while (1) {
                if (hosts_char[i] == NULL) {
                    break;
                }
                if (!g_strcmp0(hosts_char[i], sshinfo->server)) {
                    good = TRUE;
                    break;
                }
                i++;
            }
        }

        if (good == FALSE) {
            sshinfo->server = g_strdup(hosts_char[0]);
        }
        g_strfreev(hosts_char);
        g_free(autoservers);
        return;
    }
}

/*
 * _set_env
 *  Set environment variables used by LDM and Greeter
 */
void
_set_env()
{
    setenv("LDM_SERVER", sshinfo->server, 1);
    setenv("LDM_USERNAME", sshinfo->username, 1);
    setenv("LDM_SOCKET", sshinfo->ctl_socket, 1);
}

/*
 * get_auth
 *  Callback function for authentication
 */
void
get_auth()
{
    /* Get UserID */
    get_userid(&(sshinfo->username));

    /* Get password */
    get_passwd(&(sshinfo->password));

    /* Get hostname */
    get_host(&(sshinfo->server));

    /* Get Language */
    get_language(&(sshinfo->lang));

    /* Get Session */
    get_session(&(sshinfo->session));
}

/*
 * close_ssh
 *  Callback function for closing the plugins
 */
void
close_ssh()
{
    log_entry("ssh", 7, "closing ssh session");
    ssh_endsession();

    // leave no crumbs and free memory allocated for auth values
    g_free(sshinfo->password);
    g_free(sshinfo->username);
    g_free(sshinfo->server);
    g_free(sshinfo->lang);
    g_free(sshinfo->session);
    free(sshinfo);
}


void
ssh_chat(gint fd)
{
    int seen;
    gchar lastseen[LDM_PTY_MAXBUF];
    int first_time = 1;

    /* We've already got the password here from the mainline,  so there's
     * no delay between asking for the userid, and the ssh session asking for a
     * password.  That's why we need the "first_time" variable.  If a
     * password expiry is in the works, then subsequent password prompts
     * will cause us to go back to the greeter. */

    child_exited = FALSE;

    while (TRUE) {
        /* ASSUMPTION: ssh will send out a string that ends in ": " for an expiry */
        seen = ldm_pty_expect(fd, lastseen, 30, SENTINEL, ": ", NULL);

        /* We might have a : in the data, we're looking for :'s at the
           end of the line */
        if (seen == 0) {
            return;
        }

        int i;
        g_strdelimit(lastseen, "\r\n\t", ' ');
        g_strchomp(lastseen);
        i = strlen(lastseen);

        if (seen == 1) {
            /* If it's not the first time through, or the :'s not at the
             * end of a line (password expiry or error), set the message */
            if ((!first_time) || (lastseen[i - 1] != ':')) {
                set_message(lastseen);
            }
            /* If ':' *IS* the last character on the line, we'll assume a
             * password prompt is presented, and get a password */
            if (lastseen[i - 1] == ':') {
                write(fd, sshinfo->password, strlen(sshinfo->password));
                write(fd, "\n", 1);
            }
            first_time = 0;
        } else if (seen < 0) {
            if (i > 0) {
                log_entry("ssh", 3, "ssh returned: %s", lastseen);
                set_message(lastseen);
            }
            else {
                set_message(_("No response from server, restarting..."));
            }
            sleep(5);
            close_greeter();
            die("ssh", "no response, restarting");
        }
    }
}

void
ssh_tty_init(void)
{
    (void) setsid();
    if (login_tty(sshinfo->sshslavefd) < 0) {
        die("ssh", "login_tty failed");
    }
}

/*
 * ssh_session()
 * Start an ssh login to the server.
 *
 * Builds the ssh command line as an argv array and spawns it via
 * ldm_spawnv() rather than building a single string for ldm_spawn().
 * That matters here specifically because sshinfo->username and
 * sshinfo->server come straight from what was typed into the greeter's
 * login prompt, BEFORE any authentication has happened. ldm_spawn()'s
 * string form gets word-split by g_shell_parse_argv() - so a "username"
 * containing a space (e.g. "x -oProxyCommand=some-command") would be
 * split into extra ssh arguments and let anyone at the login screen make
 * ssh run an arbitrary local command as root via ProxyCommand, no valid
 * credentials required. Passing each value as its own argv element closes
 * that off entirely: whatever's typed there is just one -l argument.
 */
void
ssh_session(void)
{
    GPtrArray *argv = g_ptr_array_new();
    gchar **sshoptions_argv = NULL;
    guint i;
    gchar *logcmd;

    openpty(&(sshinfo->sshfd), &(sshinfo->sshslavefd), NULL, NULL, NULL);

    g_ptr_array_add(argv, g_strdup("ssh"));
    g_ptr_array_add(argv, g_strdup("-Y"));
    g_ptr_array_add(argv, g_strdup("-t"));
    g_ptr_array_add(argv, g_strdup("-M"));
    g_ptr_array_add(argv, g_strdup("-S"));
    g_ptr_array_add(argv, g_strdup(sshinfo->ctl_socket));
    g_ptr_array_add(argv, g_strdup("-o"));
    g_ptr_array_add(argv, g_strdup("NumberOfPasswordPrompts=1"));
    /* ConnectTimeout should be less than the timeout ssh_chat
     * passes to expect, so we get the error message from ssh
     * before expect gives up
     */
    g_ptr_array_add(argv, g_strdup("-o"));
    g_ptr_array_add(argv, g_strdup("ConnectTimeout=10"));
    g_ptr_array_add(argv, g_strdup("-l"));
    g_ptr_array_add(argv, g_strdup(sshinfo->username));

    /* Check for port override */
    if (sshinfo->override_port) {
        g_ptr_array_add(argv, g_strdup("-p"));
        g_ptr_array_add(argv, g_strdup(sshinfo->override_port));
    }

    /*
     * sshinfo->sshoptions comes from LDM_SSHOPTIONS, trusted admin
     * configuration in lts.conf (not end-user input), so shell-splitting
     * it into multiple arguments here is fine - same treatment as
     * RDP_OPTIONS in the xfreerdp plugin.
     */
    if (sshinfo->sshoptions &&
        g_shell_parse_argv(sshinfo->sshoptions, NULL, &sshoptions_argv,
                            NULL)) {
        for (i = 0; sshoptions_argv[i] != NULL; i++) {
            g_ptr_array_add(argv, sshoptions_argv[i]);
        }
        /* Ownership of the individual strings moved into argv above. */
        g_free(sshoptions_argv);
    }

    g_ptr_array_add(argv, g_strdup(sshinfo->server));
    g_ptr_array_add(argv, g_strdup("echo " SENTINEL "; exec /bin/sh -"));
    g_ptr_array_add(argv, NULL);

    logcmd = g_strjoinv(" ", (gchar **) argv->pdata);
    log_entry("ssh", 6, "ssh_session: %s", logcmd);
    g_free(logcmd);

    sshinfo->sshpid =
        ldm_spawnv((gchar **) argv->pdata, NULL, NULL, ssh_tty_init);

    ssh_chat(sshinfo->sshfd);

    /*
     * Spawn a thread to keep sshfd clean.
     */
    {
        pthread_t pt;
        pthread_create(&pt, NULL, eater, NULL);
    }

    for (i = 0; i < argv->len - 1; i++) {
        g_free(g_ptr_array_index(argv, i));
    }
    g_ptr_array_free(argv, TRUE);
}

void
ssh_endsession(void)
{
    GPid pid;
    struct stat stbuf;

    if (!stat(sshinfo->ctl_socket, &stbuf)) {
        /* socket still exists, so we need to shut down the ssh link */
        gchar *argv[] = { "ssh", "-S", sshinfo->ctl_socket, "-O", "exit",
            sshinfo->server, NULL
        };

        log_entry("ssh", 6, "closing ssh session: ssh -S %s -O exit %s",
                  sshinfo->ctl_socket, sshinfo->server);
        pid = ldm_spawnv(argv, NULL, NULL, NULL);
        ldm_wait(pid);
        close(sshinfo->sshfd);
        ldm_wait(sshinfo->sshpid);
        sshinfo->sshpid = 0;
    }
}

/*
 * ssh_hashpass()
 * Set up password hash for client /etc/shadow using /dev/urandom
 * rather than g_rand() due to developer recommendations at:
 * https://developer.gnome.org/glib/stable/glib-Random-Numbers.html
 */
void
ssh_hashpass(void)
{
    FILE *rand_fp;
    FILE *shad_fp;
    /* 16 placeholder chars between the $6$ and the closing $, one for
     * each byte read from /dev/urandom below - the loop that fills them
     * in previously wrote 16 bytes into a 15-dot template, clobbering
     * the closing '$' (harmless only because glibc's crypt() happens to
     * cap SHA-512 salts at 16 chars even without one). */
    gchar salt[] = "$6$................$";
    gchar buf[16];
    const gchar seedchars[] =
        "./0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    gchar *shadowentry;
    const gchar hashloc[] = "/run/ltsp/shadow.sed";
    size_t i = 0;
    log_entry("hashpass", 6, "LDM_PASSWORD_HASH set to true, setting hash");
    rand_fp = fopen("/dev/urandom", "r");
    if (rand_fp == NULL)
    {
        log_entry("hashpass", 7, "Unable to read from /dev/urandom - Skipping HASH function");
    }
    else
    {
        fread(buf, sizeof buf, 1, rand_fp);
        fclose(rand_fp);
        for (; i < sizeof buf; i++)
        {
            salt[3 + i] = seedchars[buf[i] % (sizeof seedchars - 1)];
        }
        shadowentry = crypt(sshinfo->password, salt);
        log_entry("hashpass", 6, "hash created");
        /* generate dynamic file for writing hash to.
        * Will remove anything in its way.
        * This will be removed during rc.d script run.
        *
        * Opened via open()+fdopen() with an explicit 0600 mode instead of
        * plain fopen(), which would create it umask-dependent (typically
        * world-readable, 0644): this file briefly holds a crypt() hash of
        * the user's password before the rc.d script consumes it, so any
        * other local process being able to read it in the meantime would
        * let it be offline-cracked.
        */
        {
            int shad_fd = open(hashloc, O_WRONLY | O_CREAT | O_TRUNC, 0600);
            shad_fp = (shad_fd >= 0) ? fdopen(shad_fd, "w") : NULL;
            if (shad_fd >= 0 && shad_fp == NULL)
                close(shad_fd);
        }
        if (shad_fp == NULL)
        {
            log_entry("hashpass", 7, "Unable to open %s for hash entry.",
                      hashloc);
        }
        else
        {
            fprintf(shad_fp,
                    "# Generated by LTSP, for LDM rc.d script manipulation\n$s@:[^:]*:@:%s:@",
                    shadowentry);
            fclose(shad_fp);
        }
    }
}

void *
eater()
{
    fd_set set;
    struct timeval timeout;
    int st;
    char buf[BUFSIZ];

    while (1) {
        if (sshinfo->sshfd == 0) {
            pthread_exit(NULL);
            break;
        }

        timeout.tv_sec = (long) 1;               /* one second timeout */
        timeout.tv_usec = 0;
        FD_ZERO(&set);
        FD_SET(sshinfo->sshfd, &set);
        st = select(FD_SETSIZE, &set, NULL, NULL, &timeout);
        if (st > 0) {
            read(sshinfo->sshfd, buf, sizeof buf);
        }
    }
}
