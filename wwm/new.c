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

    if (!(c = malloc(sizeof(Client))))
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

    if (attr.map_state == IsViewable)
        c->ignore_unmap++;
    else {
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

    if (c->size->flags & USPosition) {
        c->x = c->size->x;
        c->y = c->size->y;
    } else {
//        get_mouse_position(&x, &y);
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
    p_attr.background_pixel = bg.pixel;
    p_attr.event_mask =
        ChildMask | ButtonPressMask | ExposureMask | EnterWindowMask;
    c->parent =
        XCreateWindow(display, root, c->x - c->border,
                      c->y - c->border, c->width + (2 * c->border),
                      c->height + (2 * c->border), 0, DefaultDepth(display,
                                                                   screen),
                      CopyFromParent, DefaultVisual(display, screen),
                      CWOverrideRedirect | CWBackPixel | CWEventMask,
                      &p_attr);

    XAddToSaveSet(display, c->window);
    XSetWindowBorderWidth(display, c->window, 0);
    XReparentWindow(display, c->window, c->parent, c->border, c->border);

    send_config(c);
}
