#include <arpa/inet.h>
#include <errno.h>
#include <glib.h>
#include <glib-object.h>
#include <libintl.h>
#include <net/if.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <syslog.h>
#include <sys/wait.h>

#include "ldm.h"
#include "ldmutils.h"
#include "logging.h"

/*
 * rc_files
 *
 * Run startup commands.
 */
void
rc_files(char *action)
{
    GPid rcpid;
    gchar *command;

    command =
        g_strjoin(" ", "/bin/sh", RC_DIR "/ldm-script", action, NULL);

    rcpid = ldm_spawn(command, NULL, NULL, NULL);

    ldm_wait(rcpid);
    g_free(command);
}

/*
 * get_ipaddr
 *  Get ip address of host
 */
void
get_ipaddr()
{
    int numreqs = 10;
    struct ifconf ifc;
    struct ifreq *ifr;          /* netdevice(7) */
    struct ifreq info;
    struct sockaddr_in *sa;

    int skfd, n = 0;

    skfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (skfd < 0)
        die("ldm", "socket");

    /*
     * Get a list of all the interfaces.
     */

    ifc.ifc_buf = NULL;

    while (TRUE) {
        ifc.ifc_len = sizeof(struct ifreq) * numreqs;
        ifc.ifc_buf = (char *) realloc(ifc.ifc_buf, ifc.ifc_len);
        if (ifc.ifc_buf == NULL)
            die("ldm", "out of memory");

        if (ioctl(skfd, SIOCGIFCONF, &ifc) < 0) {
            log_entry("ldm", 4, "SIOCGIFCONF");
            goto out;
        }

        if (ifc.ifc_len == (int) sizeof(struct ifreq) * numreqs) {
            /* assume it overflowed and try again */
            numreqs += 10;
            continue;
        }

        break;
    }

    /*
     * Look for the first interface that has an IP address, is not
     * loopback, and is up.
     */

    ifr = ifc.ifc_req;
    for (n = 0; n < ifc.ifc_len; n += sizeof(struct ifreq)) {
        if (ifr->ifr_addr.sa_family != AF_INET)
            continue;

        strcpy(info.ifr_name, ifr->ifr_name);
        if (ioctl(skfd, SIOCGIFFLAGS, &info) < 0) {
            log_entry("ldm", 4, "SIOCGIFFLAGS");
            goto out;
        }

        if (!(info.ifr_flags & IFF_LOOPBACK) && (info.ifr_flags & IFF_UP)) {
            sa = (struct sockaddr_in *) &ifr->ifr_addr;
            ldm.ipaddr = g_strdup(inet_ntoa(sa->sin_addr));
            break;
        }

        ifr++;
    }

  out:
    if (ifc.ifc_buf)
        free(ifc.ifc_buf);

    if (n == ifc.ifc_len)
        die("ldm", "no configured interface found");
}

/*
 * ldm_spawn:
 *
 * Execute commands.  Prints nice error message if failure.
 */
GPid
ldm_spawn(gchar * command, gint * rfd, gint * wfd,
          GSpawnChildSetupFunc setup)
{
    GPid pid;
    GError *error = NULL;
    GSpawnFlags flags = G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD;
    gint argc;
    gchar **argv = NULL;

    g_shell_parse_argv(command, &argc, &argv, NULL);

    if (!wfd)
        flags |= G_SPAWN_STDOUT_TO_DEV_NULL;

    g_spawn_async_with_pipes(NULL,               /* Working directory: inherit */
                             argv,               /* Arguments, null term */
                             NULL,               /* Environment, inherit from parent */
                             flags,              /* Flags, set above */
                             setup,              /* Child setup function: passed to us */
                             NULL,               /* No user data */
                             &pid,               /* child pid */
                             wfd,                /* Pointer to in file descriptor */
                             rfd,                /* Pointer to out file descriptor */
                             NULL,               /* No stderr */
                             &error);            /* GError handler */

    g_strfreev(argv);

    if (error) {
        log_entry("ldm", 3, "ldm_spawn failed to execute: %s",
                  error->message);
    } else {
        log_entry("ldm", 7, "ldm_spawn: pid = %d", pid);
    }

    return pid;
}

/*
 * handle_sigchld
 *
 * Handle sigchld's for ldm processes.  Empty function,
 * since we wait for things to happen in order via
 * ldm_wait
 */
void
handle_sigchld(int signo)
{
    /* do nothing */
    child_exited = TRUE;
}

/*
 * ldm_wait
 *
 * wait for child process
 */
void
ldm_wait(GPid pid)
{
    siginfo_t info;
    log_entry("ldm", 7, "waiting for process: %d", pid);
    do {
        int res;
        res = waitid(P_PID, pid, &info, WEXITED | WSTOPPED);
        if (res == -1) {
            int temp;
            temp = errno;
            log_entry("ldm", 4, "waitid returned an error: %s",
                      strerror(errno));
            if (temp == ECHILD) {
                break;
            }
        } else {
            if (info.si_pid == pid) {
                /*
                 * The process we were waiting for exited,
                 * so break out of the loop.
                 */
                break;
            } else {
                log_entry("ldm", 4,
                          "unexpected terminated process, pid: %d",
                          info.si_pid);
                unexpected_child = TRUE;
            }
        }
    } while (TRUE);

    if (info.si_code == CLD_EXITED) {
        log_entry("ldm", 7, "process %d exited with status %d",
                  info.si_pid, WEXITSTATUS(info.si_status));
    } else if (info.si_code == CLD_KILLED) {
        log_entry("ldm", 7, "process %d killed by signal %d", info.si_pid,
                  info.si_status);
    }
}

/*
 * close_wm
 *  Close window manager by SIGKILL
 */
void
close_wm()
{
    if (!(ldm.wmpid)) {
        return;
    }

    log_entry("ldm", 7, "closing window manager");
    if (kill(ldm.wmpid, SIGKILL) < 0) {
        log_entry("ldm", 3, "sending SIGKILL to window manager failed");
    }
    ldm_wait(ldm.wmpid);
    ldm.wmpid = 0;
}
