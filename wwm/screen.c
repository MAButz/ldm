#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xproto.h>
#include <X11/cursorfont.h>
#include <stdlib.h>
#include "wwm.h"

void
draw_outline(Client * c)
{
    Window root = ROOTWINDOW;
    static int gc_initialized = 0;
    static GC invert_gc;

    if (!gc_initialized) {
        XGCValues gv;

        gc_initialized++;
        gv.function = GXinvert;
        gv.subwindow_mode = IncludeInferiors;
        gv.line_width = DEF_BW;                  /* opt_bw */

        invert_gc = XCreateGC(display, root,
                              GCFunction | GCSubwindowMode | GCLineWidth,
                              &gv);
    }

    XDrawRectangle(display, root, invert_gc, c->x - c->border,
                   c->y - c->border, c->width + c->border,
                   c->height + c->border);
}

void
get_mouse_position(int *x, int *y)
{
    Window root = ROOTWINDOW;
    Window dw1, dw2;
    int t1, t2;
    unsigned int t3;

    XQueryPointer(display, root, &dw1, &dw2, x, y, &t1, &t2, &t3);
}

void
recalculate_sweep(Client * c, int x1, int y1, int x2, int y2)
{
    int basex, basey;

    c->width = (int) abs(x1 - x2);
    c->height = (int) abs(y1 - y2);

    if (c->size->flags & PResizeInc) {
        basex = (c->size->flags & PBaseSize) ? c->size->base_width :
            (c->size->flags & PMinSize) ? c->size->min_width : 0;
        basey = (c->size->flags & PBaseSize) ? c->size->base_height :
            (c->size->flags & PMinSize) ? c->size->min_height : 0;
        c->width -= (c->width - basex) % c->size->width_inc;
        c->height -= (c->height - basey) % c->size->height_inc;
    }

    if (c->size->flags & PMinSize) {
        if (c->width < c->size->min_width)
            c->width = c->size->min_width;
        if (c->height < c->size->min_height)
            c->height = c->size->min_height;
    }

    if (c->size->flags & PMaxSize) {
        if (c->width > c->size->max_width)
            c->width = c->size->max_width;
        if (c->height > c->size->max_height)
            c->height = c->size->max_height;
    }

    c->x = (x1 <= x2) ? x1 : x1 - c->width;
    c->y = (y1 <= y2) ? y1 : y1 - c->height;
}

void
sweep(Client * c)
{
    Window root = ROOTWINDOW;
    static int resize_cursor_initialized = 0;
    static Cursor resize_curs;
    XEvent ev;
    int old_cx = c->x;
    int old_cy = c->y;

    if (!resize_cursor_initialized) {
        resize_cursor_initialized++;
        resize_curs = XCreateFontCursor(display, XC_plus);
    }

    if (!grab(root, MouseMask, resize_curs))
        return;

    XGrabServer(display);

    draw_outline(c);

    XWarpPointer(display, None, c->window, 0, 0, 0, 0, c->width,
                 c->height);

    for (;;) {
        XMaskEvent(display, MouseMask, &ev);
        switch (ev.type) {
        case MotionNotify:
            draw_outline(c);                     /* clear */
            XUngrabServer(display);
            recalculate_sweep(c, old_cx, old_cy,
                              ev.xmotion.x, ev.xmotion.y);
            XSync(display, False);
            XGrabServer(display);
            draw_outline(c);
            break;
        case ButtonRelease:
            draw_outline(c);                     /* clear */
            XUngrabServer(display);
            XUngrabPointer(display, CurrentTime);
            return;
        }
    }
}

void
drag(Client * c)
{
    Window root = ROOTWINDOW;
    static int drag_cursor_initialized = 0;
    static Cursor drag_curs;
    XEvent ev;
    int x1, y1;
    int old_cx = c->x;
    int old_cy = c->y;

    if (!drag_cursor_initialized) {
        drag_cursor_initialized++;
        drag_curs = XCreateFontCursor(display, XC_fleur);
    }

    if (!grab(root, MouseMask, drag_curs))
        return;
    get_mouse_position(&x1, &y1);
    XGrabServer(display);
    draw_outline(c);
    for (;;) {
        XMaskEvent(display, MouseMask, &ev);
        switch (ev.type) {
        case MotionNotify:
            draw_outline(c);                     /* clear */
            XUngrabServer(display);
            c->x = old_cx + (ev.xmotion.x - x1);
            c->y = old_cy + (ev.xmotion.y - y1);
            XSync(display, False);
            XGrabServer(display);
            draw_outline(c);
            break;
        case ButtonRelease:
            draw_outline(c);                     /* clear */
            XUngrabServer(display);
            XUngrabPointer(display, CurrentTime);
            return;
        default:
            break;
        }
    }
}

void
move(Client * c, int set)
{
    if (!set)
        drag(c);

    XMoveWindow(display, c->parent, c->x - c->border, c->y - c->border);
    send_config(c);
    XRaiseWindow(display, c->parent);
}

void
resize(Client * c, int set)
{
    if (!set)
        sweep(c);

    XMoveResizeWindow(display, c->parent, c->x - c->border,
                      c->y - c->border, c->width + (c->border * 2),
                      c->height + (c->border * 2));
    XMoveResizeWindow(display, c->window, c->border, c->border,
                      c->width, c->height);
    send_config(c);
    XRaiseWindow(display, c->parent);
}

void
maximise_horiz(Client * c)
{
    if (c->oldw) {
        c->x = c->oldx;
        c->width = c->oldw;
        c->oldw = 0;
    } else {
        c->oldx = c->x;
        c->oldw = c->width;
        recalculate_sweep(c, c->border, c->y,
                          (xmax() - c->border), c->y + c->height);
    }
}

void
maximise_vert(Client * c)
{
    if (c->oldh) {
        c->y = c->oldy;
        c->height = c->oldh;
        c->oldh = 0;
    } else {
        c->oldy = c->y;
        c->oldh = c->height;
        recalculate_sweep(c, c->x, c->border, c->x + c->width,
                          (ymax() - c->border));
    }
}

void
hide(Client * c)
{
    c->ignore_unmap = 2;
    XUnmapWindow(display, c->parent);
    XUnmapWindow(display, c->window);
    set_wm_state(c, IconicState);
}

void
unhide(Client * c, int raiseit)
{
    c->ignore_unmap = 0;
    XMapWindow(display, c->window);
    raiseit ? XMapRaised(display, c->parent) : XMapWindow(display,
                                                          c->parent);
    set_wm_state(c, NormalState);
}

/*
 * Next focuses the next client on this vdesk
 */

void
next(Client * c)
{
    Client *newc;

    newc = next_client_on_vdesk((c ? c : head_client));

    if (!newc)
        return;                                  /* couldn't find a next on this screen */

    focus_client(newc, RAISE);
}


/*
 * switch_vdesk changes to a different vdesk
 */

void
switch_vdesk(int v)
{
    Client *c;
    int vcurr = vdesk_get();

    if (v == vcurr)
        return;

    for (c = head_client; c; c = c->next) {
        if (c->vdesk == vcurr) {
            hide(c);
        } else if (c->vdesk == v) {
            unhide(c, NO_RAISE);
            if (c->focus)
                focus_client(c, RAISE);
        }
    }

    vdesk(VDESK_SET, v);
}

void
focus_client(Client * c, int raiseit)
{
    Client *cp;

    for (cp = head_client; cp; cp = cp->next)
        if ((cp->vdesk == vdesk_get() || cp->vdesk == LOCKED) && cp->focus)
            unfocus_client(cp);
    unhide(c, raiseit);
    XSetInputFocus(display, c->window, RevertToPointerRoot, CurrentTime);
    XSetWindowBackground(display, c->parent,
                         (c->vdesk == LOCKED) ? fc.pixel : fg.pixel);
    XClearWindow(display, c->parent);
    XGrabButton(display, AnyButton, WMMODMASK, c->parent, False,
                ButtonMask, GrabModeAsync, GrabModeSync, None, None);
    c->focus = 1;
    current = c;
}


void
unfocus_client(Client * c)
{
    XSetInputFocus(display, PointerRoot, RevertToPointerRoot, CurrentTime);
    XSetWindowBackground(display, c->parent, bg.pixel);
    XClearWindow(display, c->parent);
    c->focus = 0;
    current = NULL;
}
