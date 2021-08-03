#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xproto.h>
#include <X11/Xatom.h>
#include <X11/XKBlib.h>
#include <X11/extensions/shape.h>

#include "wwm.h"

void
handle_key_event(XKeyEvent * e)
{
    int i;
    Client *c = find_client(e->window);
    KeySym key = XkbKeycodeToKeysym(display, e->keycode, 0, 0);

    if (!c)
        c = current;

    /*
     * If we're currently in passthrough mode, just pass through whatever
     * key is typed and return.
     */

    if (passthrough) {
        send_key_event(key);
        return;
    }

    /*
     * Search through our keytab for the right key
     */

    for (i = 0; keytab[i].key != 0; i++)
        if (keytab[i].key == key) {
            keytab[i].f(c);                      /* dispatch the function */
            break;
        }
}

void
handle_button_event(XButtonEvent * e)
{
    // We want a minimal WM, so no mouse actions
    return;

    Window root = ROOTWINDOW;
    Client *c = find_client(e->window);

    if (e->window == root) {
        return;
        switch (e->button) {
        case Button4:                           /* Scrolly wheel up in root */
            prev_vdesk((Client *) NULL);
            return;
        case Button5:                           /* Scrolly wheel down in root */
            next_vdesk((Client *) NULL);
            return;
        }
    } else if (c) {
        switch (e->button) {
        case Button1:                           /* Left mouse = move */
            move(c, 0);
            return;
        case Button3:                           /* Right mouse = resize */
            resize(c, 0);
            return;
        }
    }
}

void
handle_shape_event(XShapeEvent * e)
{
    Client *c = find_client(e->window);

    if (c)
        set_shape(c);
}

void
handle_configure_request(XConfigureRequestEvent * e)
{
    Client *c = find_client(e->window);
    XWindowChanges wc;

    wc.sibling = e->above;
    wc.stack_mode = e->detail;

    if (c) {
        change_gravity(c, UNGRAVITATE);
        if (e->value_mask & CWX)
            c->x = e->x;
        if (e->value_mask & CWY)
            c->y = e->y;
        if (e->value_mask & CWWidth)
            c->width = e->width;
        if (e->value_mask & CWHeight)
            c->height = e->height;
        if (c->x == 0 && c->width >= xmax())
            c->x -= c->border;
        if (c->y == 0 && c->height >= ymax())
            c->y -= c->border;

        change_gravity(c, GRAVITATE);

        wc.x = c->x - c->border;
        wc.y = c->y - c->border;
        wc.width = c->width + (c->border * 2);
        wc.height = c->height + (c->border * 2);
        wc.border_width = 0;
        XConfigureWindow(display, c->parent, e->value_mask, &wc);
        send_config(c);
    }

    wc.x = c ? c->border : e->x;
    wc.y = c ? c->border : e->y;
    wc.width = e->width;
    wc.height = e->height;
    XConfigureWindow(display, e->window, e->value_mask, &wc);
}

void
handle_map_request(XMapRequestEvent * e)
{
    Client *c = find_client(e->window);

    if (c) {
        if (c->vdesk != vdesk_get())
            switch_vdesk(c->vdesk);
        unhide(c, RAISE);
    } else
        make_new_client(e->window);
}

void
handle_unmap_event(XUnmapEvent * e)
{
    Client *c = find_client(e->window);
    Client *focus_to;

    if (!c)
        return;

    if (c->ignore_unmap)
        c->ignore_unmap--;
    else {
        /*
         * Switch focus to next window on the screen, if there.
         */
        focus_to = next_client_on_vdesk(c);
        remove_client(c, NOT_QUITTING);
        if (focus_to)
            focus_client(focus_to, NO_RAISE);
    }
}

void
handle_client_message(XClientMessageEvent * e)
{
    Client *c = find_client(e->window);

    if (c &&                                     /* Client exists */
        e->message_type == xa_wm_change_state && /* Changing state */
        e->format == sz_xInternAtomReply &&      /* Data is long */
        e->data.l[0] == IconicState)             /* We're now iconic */
        hide(c);
}

void
handle_property_change(XPropertyEvent * e)
{
    Client *c = find_client(e->window);
    long dummy;

    if (c)
        if (e->atom == XA_WM_NORMAL_HINTS)
            XGetWMNormalHints(display, c->window, c->size, &dummy);
}

void
handle_enter_event(XCrossingEvent * e)
{
    Client *c = find_client(e->window);

    if (nomousefocus || !c)                      /* Ignore focus follows mouse */
        return;

    if (c->vdesk != vdesk_get() && c->vdesk != LOCKED)
        return;

    if (c != current)
        focus_client(c, NO_RAISE);
}
