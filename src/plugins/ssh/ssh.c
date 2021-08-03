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
#include "../../ldmgreetercomm.h"
#include "../../logging.h"
#include "../../plugin.h"
#include "ssh.h"

#define ERROR -1
#define TIMED_OUT -2
#define MAXEXP 4096
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

    /* Variable validation */
    if (!(sshinfo->username)) {
        log_entry("ssh", 3, "no username");
        error = TRUE;
    }

    if (!(sshinfo->password)) {
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

int
expect(int fd, char *p, int seconds, ...)
{
    fd_set set;
    struct timeval timeout;
    int i = 0, st;
    ssize_t size = 0;
    size_t total = 0;
    va_list ap;
    char buffer[BUFSIZ];
    gchar *arg;
    GPtrArray *expects;
    int loopcount = seconds;
    int loopend = 0;

    bzero(p, MAXEXP);

    expects = g_ptr_array_new();

    va_start(ap, seconds);

    while ((arg = va_arg(ap, char *)) != NULL) {
        g_ptr_array_add(expects, (gpointer) arg);
    }

    va_end(ap);

    /*
     * Set our file descriptor to be watched.
     */


    /*
     * Main loop.
     */

    while (1) {
        timeout.tv_sec = (long) 1;               /* one second timeout */
        timeout.tv_usec = 0;

        FD_ZERO(&set);
        FD_SET(fd, &set);
        st = select(FD_SETSIZE, &set, NULL, NULL, &timeout);

        if (st == -1 && errno == EINTR)
        {
            continue;                            /* interrupted by signal -> retry */
        }

        if (st < 0) {                            /* bad thing */
            break;
        }

        if (loopcount == 0) {
            break;
        }

        if (!st) {                               /* timeout */
            loopcount--;                         /* We've not seen the data we want */
            continue;
        }

        size = read(fd, buffer, sizeof buffer);
        if (size <= 0) {
            break;
        }

        if ((total + size) < MAXEXP) {
            strncpy(p + total, buffer, size);
            total += size;
        }

        if (child_exited) {
            break;                               /* someone died on us */
        }

        for (i = 0; i < expects->len; i++) {
            if (strstr(p, g_ptr_array_index(expects, i))) {
                loopend = TRUE;
                break;
            }
        }

        if (loopend) {
            break;
        }
    }

    log_entry("ldm", 7, "expect saw: %s", p);

    if (size < 0 || st < 0) {
        return ERROR;                            /* error occured */
    }
    if (loopcount == 0) {
        return TIMED_OUT;                        /* timed out */
    }
    /* Sleep a bit to make sure we notice if ssh died in the meantime */
    usleep(100000);
    if (child_exited)
    {
        return ERROR;
    }

    return i;                                    /* which expect did we see? */
}

void
ssh_chat(gint fd)
{
    int seen;
    gchar lastseen[MAXEXP];
    int first_time = 1;

    /* We've already got the password here from the mainline,  so there's
     * no delay between asking for the userid, and the ssh session asking for a
     * password.  That's why we need the "first_time" variable.  If a
     * password expiry is in the works, then subsequent password prompts
     * will cause us to go back to the greeter. */

    child_exited = FALSE;

    while (TRUE) {
        /* ASSUMPTION: ssh will send out a string that ends in ": " for an expiry */
        seen = expect(fd, lastseen, 30, SENTINEL, ": ", NULL);

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
 */
void
ssh_session(void)
{
    gchar *command;
    gchar *port = NULL;
    pthread_t pt;

    /* Check for port Override */
    if (sshinfo->override_port)
        port = g_strconcat(" -p ", sshinfo->override_port, " ", NULL);

    openpty(&(sshinfo->sshfd), &(sshinfo->sshslavefd), NULL, NULL, NULL);

    command = g_strjoin(" ", "ssh", "-Y", "-t", "-M",
                        "-S", sshinfo->ctl_socket,
                        "-o", "NumberOfPasswordPrompts=1",
                         /* ConnectTimeout should be less than the timeout ssh_chat
                          * passes to expect, so we get the error message from ssh
                          * before expect gives up
                          */
                        "-o", "ConnectTimeout=10",
                        "-l", sshinfo->username,
                        port ? port : "",
                        sshinfo->sshoptions ? sshinfo->sshoptions : "",
                        sshinfo->server,
                        "echo " SENTINEL "; exec /bin/sh -", NULL);
    log_entry("ssh", 6, "ssh_session: %s", command);

    sshinfo->sshpid = ldm_spawn(command, NULL, NULL, ssh_tty_init);

    ssh_chat(sshinfo->sshfd);

    /*
     * Spawn a thread to keep sshfd clean.
     */
    pthread_create(&pt, NULL, eater, NULL);

    if (port)
        g_free(port);
}

void
ssh_endsession(void)
{
    GPid pid;
    gchar *command;
    struct stat stbuf;

    if (!stat(sshinfo->ctl_socket, &stbuf)) {
        /* socket still exists, so we need to shut down the ssh link */

        command =
            g_strjoin(" ", "ssh", "-S", sshinfo->ctl_socket, "-O", "exit",
                      sshinfo->server, NULL);
        log_entry("ssh", 6, "closing ssh session: %s", command);
        pid = ldm_spawn(command, NULL, NULL, NULL);
        ldm_wait(pid);
        close(sshinfo->sshfd);
        ldm_wait(sshinfo->sshpid);
        sshinfo->sshpid = 0;
        g_free(command);
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
    gchar salt[] = "$6$...............$";
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
        */
        shad_fp = fopen(hashloc, "w");
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
