#include <stdlib.h>
#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xproto.h>
#include <X11/Xatom.h>
#include <X11/extensions/shape.h>

#include "wwm.h"

/*
 * used all over the place.  return the client that has specified window as
 * either window or parent
 */

Client *
find_client(Window w)
{
    Client *c;

    for (c = head_client; c; c = c->next)
        if (w == c->parent || w == c->window)
            return (c);
    return (NULL);
}

Client *
next_client_on_vdesk(Client * c)
{
    Client *newc = c, *marker = c;
    int v = vdesk_get();

    if (!c)
        return NULL;

    do
        newc = (newc->next ? newc->next : head_client);
    while (newc != marker && newc->vdesk != v);

    return ((newc == marker) ? NULL : newc);
}

void
set_wm_state(Client * c, int state)
{
    long data[2];

    data[0] = (long) state;
    data[1] = None;                              /* icon window */

    XChangeProperty(display, c->window, xa_wm_state, xa_wm_state,
                    sz_xInternAtomReply, PropModeReplace,
                    (unsigned char *) data, 2);
}

void
send_config(Client * c)
{
    XConfigureEvent ce;

    ce.type = ConfigureNotify;
    ce.event = c->window;
    ce.window = c->window;
    ce.x = c->x;
    ce.y = c->y;
    ce.width = c->width;
    ce.height = c->height;
    ce.border_width = c->border;
    ce.above = None;
    ce.override_redirect = False;

    XSendEvent(display, c->window, False, StructureNotifyMask,
               (XEvent *) & ce);
    XSync(display, False);
}

void
remove_client(Client * c, int from_cleanup)
{
    Window root = ROOTWINDOW;
    Client *p;

    XGrabServer(display);
    ignore_xerror = True;

    if (!from_cleanup) {
        set_wm_state(c, WithdrawnState);
        XRemoveFromSaveSet(display, c->window);
    }

    change_gravity(c, UNGRAVITATE);
    XSetWindowBorderWidth(display, c->window, 1);
    XReparentWindow(display, c->window, root, c->x, c->y);

    if (c->parent)
        XDestroyWindow(display, c->parent);

    if (head_client == c)
        head_client = c->next;
    else
        for (p = head_client; p && p->next; p = p->next)
            if (p->next == c)
                p->next = c->next;

    if (c->size)
        XFree(c->size);
    if (current == c)
        current = NULL;                          /* an enter event should set this up again */
    free(c);

    XSync(display, False);
    ignore_xerror = False;
    XUngrabServer(display);
}

void
change_gravity(Client * c, int invert)
{
    int dx = 0, dy = 0;
    int gravity = (c->size->flags & PWinGravity) ?
        c->size->win_gravity : NorthWestGravity;

    switch (gravity) {
    case CenterGravity:
    case StaticGravity:
    case NorthWestGravity:
    case NorthGravity:
    case WestGravity:
        dx = c->border;
        dy = c->border;
        break;
    case NorthEastGravity:
    case EastGravity:
        dx = -(c->border);
        dy = c->border;
        break;
    case SouthEastGravity:
        dx = -(c->border);
        dy = -(c->border);
        break;
    case SouthGravity:
    case SouthWestGravity:
        dx = c->border;
        dy = -(c->border);
        break;
    }

    if (invert) {
        dx = -dx;
        dy = -dy;
    }

    c->x += dx;
    c->y += dy;
}

void
send_wm_delete(Client * c)
{
    int i, n, found = 0;
    Atom *protocols;

    if (!c)
        return;

    if (XGetWMProtocols(display, c->window, &protocols, &n)) {
        for (i = 0; i < n; i++)
            if (protocols[i] == xa_wm_delete)
                found++;
        XFree(protocols);
    }

    if (found)
        send_xmessage(c->window, xa_wm_protos, xa_wm_delete);
    else
        XKillClient(display, c->window);
}

int
send_xmessage(Window w, Atom a, long x)
{
    XEvent ev;

    ev.type = ClientMessage;
    ev.xclient.window = w;
    ev.xclient.message_type = a;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = x;
    ev.xclient.data.l[1] = CurrentTime;

    return (XSendEvent(display, w, False, NoEventMask, &ev));
}

void
set_shape(Client * c)
{
    int i, b;
    unsigned int u;             /* dummies */
    int bounding_shaped;
    Status retval;

    if (!have_shape || !c)
        return;

    if (c->window == None || c->parent == None)
        return;

    /*
     * Logic to decide if we have a shaped window cribbed from fvwm-2.5.10.
     * Previous method (more than one rectangle returned from
     * XShapeGetRectangles) worked _most_ of the time.
     */

    retval =
        XShapeQueryExtents(display, c->window, &bounding_shaped, &i, &i,
                           &u, &u, &b, &i, &i, &u, &u);
    if (retval && bounding_shaped)
        XShapeCombineShape(display, c->parent, ShapeBounding,
                           c->border, c->border, c->window, ShapeBounding,
                           ShapeSet);
}
