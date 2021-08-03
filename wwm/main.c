#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xproto.h>
#include <X11/Xatom.h>
#include <X11/Xresource.h>
#include <X11/Xlib.h>
#include <X11/extensions/shape.h>

#include "wwm.h"

void handle_shape_event(XShapeEvent * e);

/*
 * Globals definitions
 */

Client *head_client = NULL;     /* First client */
Client *current = NULL;         /* "current" client */
Display *display;               /* Our display */
Atom xa_wm_state;               /* atoms for window manager functions */
Atom xa_wm_change_state;
Atom xa_wm_protos;
Atom xa_wm_delete;
XColor fg, bg, fc;              /* Forground, Background, and locked colours */
int nomousefocus = 0;           /* focus follows mouse status */
int passthrough = 0;            /* current passthrough state */
char *term = NULL;              /* terminal to launch */
int have_shape = 0;             /* Do we support shaped windows? */
int ignore_xerror = 0;          /* Ignore X errors */

struct wmkeys keytab[] = {
    {0, dummy_func}
};

int
main(int argc, char *argv[])
{
    struct sigaction act;
    static char *dpy;
    XEvent ev;
    int shape_event;

    dpy = getenv("DISPLAY");                     /* manage the display in $DISPLAY */

    if (!dpy)
        dpy = strdup(DEF_DISPLAY);

    act.sa_handler = handle_signal;
    sigemptyset(&act.sa_mask);
#ifdef SA_NOCLDSTOP
    act.sa_flags = SA_NOCLDSTOP;                 /* don't care about STOP, CONT */
#else
    act.sa_flags = 0;
#endif
    sigaction(SIGTERM, &act, NULL);
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGHUP, &act, NULL);
    sigaction(SIGCHLD, &act, NULL);

    setup_display(dpy);

    term = strdup(DEF_TERM);

    if (!DEF_DL)
        scan_windows();

    /*
     * Scan for the shape extention if it exists.
     */

    {
        int e_dummy;
        have_shape = XShapeQueryExtension(display, &shape_event, &e_dummy);
    }

    int x = xmax() / 2;
    int y = ymax() / 2;
    XWarpPointer(display, None, ROOTWINDOW, 0, 0, 0, 0, x, y);

    /*
     * main event loop here
     */

    for (;;) {
        XNextEvent(display, &ev);
        switch (ev.type) {
        case KeyPress:
            handle_key_event(&ev.xkey);
            break;
        case ButtonPress:
            handle_button_event(&ev.xbutton);
            break;
        case ConfigureRequest:
            handle_configure_request(&ev.xconfigurerequest);
            break;
        case MapRequest:
            handle_map_request(&ev.xmaprequest);
            break;
        case ClientMessage:
            handle_client_message(&ev.xclient);
            break;
        case EnterNotify:
            handle_enter_event(&ev.xcrossing);
            break;
        case PropertyNotify:
            handle_property_change(&ev.xproperty);
            break;
        case UnmapNotify:
            handle_unmap_event(&ev.xunmap);
            break;
        default:
            if (have_shape && ev.type == shape_event) {
                handle_shape_event((XShapeEvent *) & ev);
            }
        }
    }

    return (1);                                  /* ?!?  shouldn't get here */
}

void
setup_display(char *dpy)
{
    XSetWindowAttributes attr;
    XColor dummy;
    XModifierKeymap *modmap;
    int i, j, screen;
    Window root;
    Colormap colormap;
    unsigned int numlockmask = 0;

    /*
     * Set some of the defaults
     */


    /*
     * Open the display
     */

    display = XOpenDisplay(dpy);
    if (!display)                                /* couldn't open display */
        exit(1);

    XSetErrorHandler(handle_xerror);
    XSetIOErrorHandler(handle_xexit);

    screen = DefaultScreen(display);
    root = RootWindow(display, screen);
    colormap = DefaultColormap(display, screen);

    /*
     * Set up our window management atoms.
     */

    xa_wm_state = XInternAtom(display, "WM_STATE", False);
    xa_wm_change_state = XInternAtom(display, "WM_CHANGE_STATE", False);
    xa_wm_protos = XInternAtom(display, "WM_PROTOCOLS", False);
    xa_wm_delete = XInternAtom(display, "WM_DELETE_WINDOW", False);

    XAllocNamedColor(display, colormap, DEF_FG, &fg, &dummy);
    XAllocNamedColor(display, colormap, DEF_BG, &bg, &dummy);
    XAllocNamedColor(display, colormap, DEF_FC, &fc, &dummy);

    /*
     * find out which modifier is NumLock - we'll use this when grabbing
     * every combination of modifiers we can think ofi
     */

    modmap = XGetModifierMapping(display);

    for (i = 0; i < 8; i++)
        for (j = 0; j < modmap->max_keypermod; j++)
            if (modmap->modifiermap[i * modmap->max_keypermod + j] ==
                XKeysymToKeycode(display, XK_Num_Lock))
                numlockmask = (1 << i);

    XFreeModifiermap(modmap);

    attr.event_mask = KeyPressMask | ChildMask | PropertyChangeMask |
        EnterWindowMask | ButtonMask;
    XChangeWindowAttributes(display, root, CWEventMask, &attr);

    for (i = 0; keytab[i].key != 0; i++) {
        my_grab_key(keytab[i].key, WMMODMASK);
        my_grab_key(keytab[i].key, LockMask | WMMODMASK);
        if (numlockmask) {
            my_grab_key(keytab[i].key, numlockmask | WMMODMASK);
            my_grab_key(keytab[i].key, numlockmask | LockMask | WMMODMASK);
        }
    }
}

void
my_grab_key(int keycode, unsigned int modifiers)
{
    Window root = RootWindow(display, DefaultScreen(display));
    XGrabKey(display, XKeysymToKeycode(display, keycode),
             modifiers, root, True, GrabModeAsync, GrabModeAsync);
}

void
scan_windows(void)
{
    unsigned int i, nwins;
    Window dw1, dw2, *wins;
    XWindowAttributes attr;
    Window root = RootWindow(display, DefaultScreen(display));

    XQueryTree(display, root, &dw1, &dw2, &wins, &nwins);
    for (i = 0; i < nwins; i++) {
        XGetWindowAttributes(display, wins[i], &attr);
        if (!attr.override_redirect && attr.map_state == IsViewable)
            make_new_client(wins[i]);
    }
    XFree(wins);
}
