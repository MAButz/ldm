#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xproto.h>
#include "wwm.h"

/*
 * key_to_event:
 *
 * Turns a keysym into an event structure.
 */

void
key_to_event(KeySym key, XEvent * event, Window w, int type)
{
    event->xkey.type = type;
    event->xkey.display = display;
    event->xkey.root = RootWindow(display, DefaultScreen(display));
    event->xkey.time = CurrentTime;
    event->xkey.x = event->xkey.y = 0;
    event->xkey.x_root = event->xkey.y_root = 0;
    event->xkey.state = WMMODMASK;
    event->xkey.window = w;
    event->xkey.keycode = XKeysymToKeycode(display, key);
}

/*
 * send_key_event:
 *
 * Sends a keypress event to the current client window.
 */

void
send_key_event(KeySym key)
{
    Window w;
    XEvent event;
    int revert;

    /*
     * Grab keyboard focus
     */

    XGetInputFocus(display, &w, &revert);

    /*
     * Simulate a keypress event by passing a keydown and keyup event to the
     * application.
     */

    key_to_event(key, &event, w, KeyPress);
    XSendEvent(display, w, True, KeyPressMask, &event);

    key_to_event(key, &event, w, KeyRelease);
    XSendEvent(display, w, True, KeyReleaseMask, &event);

    /*
     * Reset passthrough.
     */

    passthrough = 0;
}
