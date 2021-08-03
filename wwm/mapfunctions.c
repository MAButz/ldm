#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <signal.h>
#include <stdio.h>
#include "wwm.h"

/*
 *  Start of the keyboard set functions.
 */


/*
 * move_win_up: move a window up WMDELTA pixels
 */

void
move_win_up(Client * c)
{
    if (!c)
        return;
    c->y -= WMDELTA;
    move(c, 1);
}

/*
 * move_win_down: move a window down WMDELTA pixels
 */

void
move_win_down(Client * c)
{
    if (!c)
        return;
    c->y += WMDELTA;
    move(c, 1);
}

/*
 * move_win_left: move a window left WMDELTA pixels
 */

void
move_win_left(Client * c)
{
    if (!c)
        return;
    c->x -= WMDELTA;
    move(c, 1);
}

/*
 * move_win_right: move a window right WMDELTA pixels
 */

void
move_win_right(Client * c)
{
    if (!c)
        return;
    c->x += WMDELTA;
    move(c, 1);
}

/*
 * expand_win_y: make window larger horzontaly
 */

void
expand_win_y(Client * c)
{
    if (!c)
        return;
    c->height += WMDELTA;
    resize(c, 1);
}

/*
 * contract_win_y: make window smaller horzontaly
 */

void
contract_win_y(Client * c)
{
    if (!c || c->height <= WMDELTA)
        return;
    c->height -= WMDELTA;
    resize(c, 1);
}

void
expand_win_x(Client * c)
{
    if (!c)
        return;
    c->width += WMDELTA;
    resize(c, 1);
}

void
contract_win_x(Client * c)
{
    if (!c || c->width <= WMDELTA)
        return;
    c->width -= WMDELTA;
    resize(c, 1);
}

void
kill_client(Client * c)
{
    if (!c)
        return;
    send_wm_delete(c);
}

void
raise_client(Client * c)
{
    if (!c)
        return;
    XRaiseWindow(display, c->parent);
}

void
lower_client(Client * c)
{
    if (!c)
        return;
    XLowerWindow(display, c->parent);
}

void
horizontal_toggle(Client * c)
{
    if (!c)
        return;
    maximise_horiz(c);
    resize(c, 1);
}

void
vertical_toggle(Client * c)
{
    if (!c)
        return;
    maximise_vert(c);
    resize(c, 1);
}

void
maximize_toggle(Client * c)
{
    if (!c)
        return;
    maximise_horiz(c);
    maximise_vert(c);
    resize(c, 1);
}

void
lock_window(Client * c)
{
    if (!c)
        return;
    XSetWindowBackground(display, c->parent,
                         c->vdesk == LOCKED ? fg.pixel : fc.pixel);
    XClearWindow(display, c->parent);
    c->vdesk = c->vdesk == LOCKED ? vdesk_get() : LOCKED;
}

void
new_term(Client * c)
{
    spawn(term);
}

void
next_client(Client * c)
{
    next(current);
}

void
quit_wm(Client * c)
{
    handle_signal(SIGHUP);
}

void
start_passthrough(Client * c)
{
    passthrough++;
}

void
goto_vdesk_0(Client * c)
{
    switch_vdesk(0);
}

void
goto_vdesk_1(Client * c)
{
    switch_vdesk(1);
}

void
goto_vdesk_2(Client * c)
{
    switch_vdesk(2);
}

void
goto_vdesk_3(Client * c)
{
    switch_vdesk(3);
}

void
goto_vdesk_4(Client * c)
{
    switch_vdesk(4);
}

void
goto_vdesk_5(Client * c)
{
    switch_vdesk(5);
}

void
goto_vdesk_6(Client * c)
{
    switch_vdesk(6);
}

void
goto_vdesk_7(Client * c)
{
    switch_vdesk(7);
}

void
prev_vdesk(Client * c)
{
    int v = vdesk(VDESK_PREV, VDESK_NULL);
    switch_vdesk(v);
    vdesk(VDESK_SET, v);
}

void
next_vdesk(Client * c)
{
    int v = vdesk(VDESK_NEXT, VDESK_NULL);
    switch_vdesk(v);
    vdesk(VDESK_SET, v);
}

void
top_left(Client * c)
{
    if (!c)
        return;

    c->x = c->border;
    c->y = c->border;
    move(c, 1);
}

void
top_right(Client * c)
{
    if (!c)
        return;

    c->x = xmax() - (c->width + c->border);
    c->y = c->border;
    move(c, 1);
}

void
bottom_left(Client * c)
{
    if (!c)
        return;

    c->x = c->border;
    c->y = ymax() - (c->height + c->border);
    move(c, 1);
}

void
bottom_right(Client * c)
{
    if (!c)
        return;

    c->x = xmax() - (c->width + c->border);
    c->y = ymax() - (c->height + c->border);
    move(c, 1);
}

void
toggle_focus(Client * c)
{
    nomousefocus = (nomousefocus + 1) % 2;
}

void
dummy_func(Client * c)
{
    return;
}
