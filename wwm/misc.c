#include <stdlib.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xproto.h>
#include <X11/Xatom.h>

#include "wwm.h"

void
spawn(char *cmd)
{
    pid_t pid;

    if (!(pid = fork())) {
        setsid();
        if (!fork())
            execlp(cmd, cmd, NULL);
        else
            exit(0);
    }

    if (pid > 0)
        wait(NULL);
}

void
handle_signal(int signo)
{
    if (signo == SIGCHLD) {
        wait(NULL);
        return;
    }

    /*
     * Quit Nicely
     */

#if 0
    while (head_client)
        remove_client(head_client, QUITTING);

    XSetInputFocus(display, PointerRoot, RevertToNone, CurrentTime);
    XInstallColormap(display, COLORMAP);
    XCloseDisplay(display);
#endif
    exit(0);
}

int
handle_xerror(Display * dpy, XErrorEvent * e)
{
    Client *c = find_client(e->resourceid);

    if (ignore_xerror)
        return (0);

    if (e->error_code == BadAccess &&
        e->request_code == X_ChangeWindowAttributes)
        exit(1);

    if (c)
        remove_client(c, NOT_QUITTING);

    return (0);
}

int
handle_xexit(Display * dpy)
{
    exit(0);
}

int
vdesk(int vdesk_command, int vdesk_option)
{
    static int my_vdesk = 0;

    switch (vdesk_command) {
    case VDESK_GET:
        return (my_vdesk);
    case VDESK_SET:
        my_vdesk = vdesk_option;
        return (my_vdesk);
    case VDESK_NEXT:
        return ((my_vdesk + 1) % VDESK_MAX);
    case VDESK_PREV:
        return ((my_vdesk ? my_vdesk - 1 : (VDESK_MAX - 1)) % VDESK_MAX);
    default:
        return (my_vdesk);
    }
}
