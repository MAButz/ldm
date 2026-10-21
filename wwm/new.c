#include <stdlib.h>
#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xproto.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>
#include "wwm.h"

void
make_new_client(Window w)
{
    Client *c;
    XWindowAttributes attr;
    long dummy;
    XWMHints *hints;
    volatile Window initializing;
    char *name;

    /*
     * Allocate a new client structure
     */

    /* Zeroed: ignore_unmap and focus were read before ever being set. */
    if (!(c = calloc(1, sizeof(Client))))
        return;

    /*
     * Exclusive access to the xserver
     */

    XGrabServer(display);

    initializing = w;
    XFetchName(display, initializing, &name);
    XSync(display, False);

    if (initializing == None) {

        /*
         * Window disappeared of it's own accord
         */

        free(c);
        XSync(display, False);
        XUngrabServer(display);
        return;
    }

    if (name)
        XFree(name);

    /*
     * Insert client into linked list
     */

    c->next = head_client;
    head_client = c;
    c->window = w;

    /*
     * Release the server as soon as we can.
     */

    XSync(display, False);
    XUngrabServer(display);

    /*
     * Allocate the sizing hints.  Windows like xterm want certain increments.
     * If the client doesnt have them, set 1 pixel limits.
     */

    c->size = XAllocSizeHints();

    XGetWMNormalHints(display, c->window, c->size, &dummy);

    if (!c->size->width_inc)
        c->size->width_inc = 1;
    if (!c->size->height_inc)
        c->size->height_inc = 1;

    /*
     * Get the window attribues, and poulate the client structure.
     */

    XGetWindowAttributes(display, c->window, &attr);

    c->x = attr.x;
    c->y = attr.y;
    c->width = attr.width;
    c->height = attr.height;
    c->border = DEF_BW;
    c->oldw = c->oldh = 0;
    c->vdesk = vdesk_get();

    /*
     * Check to make sure that windows are sized properly on map.
     */

    hints = XGetWMHints(display, w);

    /*
     * A window that says it takes no input - GTK's accept-focus FALSE, which
     * the greeter sets on its bottom bar and the backdrops of further
     * monitors - is never given the focus. Every new window used to get it,
     * so whether the login prompt ended up with it depended on which arrived
     * last, the bar or the pointer's EnterNotify on the prompt. When the bar
     * won, typing went nowhere, and since there is no click to focus here
     * nothing at the screen got the prompt back.
     */
    c->input = !(hints && (hints->flags & InputHint) && !hints->input);

    if (attr.map_state != IsViewable) {
        init_position(c);
        if (hints && hints->flags & StateHint)
            set_wm_state(c, hints->initial_state);
    }

    /*
     * Free the hints structure
     */

    if (hints)
        XFree(hints);

    /*
     * client is initialised
     */

    change_gravity(c, GRAVITATE);
    reparent(c);

    /*
     * shape the window if needed.
     */

    if (have_shape) {
        XShapeSelectInput(display, c->window, ShapeNotifyMask);
        set_shape(c);
    }

    focus_client(c, RAISE);                      /* new windows get focus */

    /*
     * Reparenting a window that is already mapped unmaps it, and that
     * UnmapNotify arrives after we return. Ignore it - counted only now,
     * because focus_client() above goes through unhide(), which zeroes the
     * count. Counted before, as it was, the window was let go again at once:
     * windows that existed when wwm started were never managed.
     */
    if (attr.map_state == IsViewable)
        c->ignore_unmap++;
}

void
init_position(Client * c)
{
    int x, y;
    int xmax = xmax();
    int ymax = ymax();
    int spacing = c->border * 2;

    if (c->width < MINSIZE)
        c->width = MINSIZE;
    if (c->height < MINSIZE)
        c->height = MINSIZE;
    if (c->width > (xmax - spacing))
        c->width = (xmax - spacing);
    if (c->height > (ymax - spacing))
        c->height = (ymax - spacing);

    /*
     * Prefer a position the window already has - either because the app
     * explicitly requested one via WM_NORMAL_HINTS' USPosition, or
     * because it was already placed via a real X call before/while being
     * mapped (c->x/c->y already hold that, copied from
     * XGetWindowAttributes() in make_new_client()). Only synthesize a
     * placement for windows that are still sitting at the X server's
     * untouched default of (0,0), i.e. genuinely never positioned.
     * (evilwm, which this was forked from, does the equivalent by
     * preferring attr.x/attr.y whenever the window is already viewable
     * or has USPosition set; upstream also has mouse-follow placement
     * here, which was disabled below without ever being replaced - the
     * "x/y = xmax/2, ymax/2" is not a mouse position, it's a hardcoded
     * mid-screen stand-in.)
     */
    if ((c->size->flags & USPosition) || c->x != 0 || c->y != 0) {
        /* keep c->x/c->y as already set */
    } else {
        x = xmax / 2;
        y = ymax / 2;
        c->x = (x * (xmax - c->border - c->width)) / xmax;
        c->y = (y * (ymax - c->border - c->height)) / ymax;
    }

    /*
     * Final sanity check.
     */

    if (c->x < 0 || c->y < 0 || c->x > xmax || c->y > ymax)
        c->x = c->y = c->border;
}

void
reparent(Client * c)
{
    XSetWindowAttributes p_attr;
    int screen = DefaultScreen(display);
    Window root = RootWindow(display, screen);

    XSelectInput(display, c->window, EnterWindowMask | PropertyChangeMask);

    p_attr.override_redirect = True;
    /* No border, no background: see set_frame_colour() */
    p_attr.background_pixmap = None;
    p_attr.background_pixel = bg.pixel;
    p_attr.event_mask =
        ChildMask | ButtonPressMask | ExposureMask | EnterWindowMask;
    c->parent =
        XCreateWindow(display, root, c->x - c->border,
                      c->y - c->border, c->width + (2 * c->border),
                      c->height + (2 * c->border), 0, DefaultDepth(display,
                                                                   screen),
                      CopyFromParent, DefaultVisual(display, screen),
                      CWOverrideRedirect | CWEventMask |
                      (c->border == 0 ? CWBackPixmap : CWBackPixel),
                      &p_attr);

    XAddToSaveSet(display, c->window);
    XSetWindowBorderWidth(display, c->window, 0);
    XReparentWindow(display, c->window, c->parent, c->border, c->border);

    send_config(c);
}
