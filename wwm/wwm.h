/* default settings */

#include "wwm-defaults.h"

/*
 * readability stuff
 */

#define WMMODMASK       Mod1Mask
#define MINSIZE         15
#define SPACE           3
#define NOT_QUITTING    0
#define QUITTING        1               /* for remove_client */
#define RAISE           1
#define NO_RAISE        0               /* for unhide() */
#define VDESK_MAX       8
#define WMDELTA         16
#define GRAVITATE       0
#define UNGRAVITATE     1
#define TRUE            1
#define FALSE           0
#define VDESK_GET       0
#define VDESK_SET       1
#define VDESK_NEXT      2
#define VDESK_PREV      3
#define VDESK_NULL      0
#define LOCKED          -1  /* Locked Window */
#define ChildMask       (SubstructureRedirectMask|SubstructureNotifyMask)
#define ButtonMask      (ButtonPressMask|ButtonReleaseMask)
#define MouseMask       (ButtonMask|PointerMotionMask)
#define ROOTWINDOW      (RootWindow(display, DefaultScreen(display)))
#define COLORMAP        (DefaultColormap(display, DefaultScreen(display)))

/*
 * #defined functions
 */

#define vdesk_get()     vdesk(VDESK_GET, VDESK_NULL)
#define grab(w, mask, curs) \
    (XGrabPointer(display, w, False, mask, GrabModeAsync, GrabModeAsync, \
    None, curs, CurrentTime) == GrabSuccess)

#define xmax() DisplayWidth  (display, DefaultScreen(display))
#define ymax() DisplayHeight (display, DefaultScreen(display))

/*
 * Structure definitions
 */

typedef struct Client Client;

struct Client
{
  Client *next;                         /* Next client in the linked list */
  Window window;                        /* Window we're managing */
  Window parent;                        /* Parent window (the border) */
  XSizeHints *size;                     /* Size hints for the window */
  int ignore_unmap;                     /* Are we ignoring unmaps */
  int x, y, width, height;              /* size */
  int oldx, oldy, oldw, oldh;           /* used when maximising */
  int border;                           /* border size */
  int vdesk;                            /* Which vdesk we're on */
  int focus;                            /* Do we think we have focus */
};

struct wmkeys {
  KeySym key;                           /* The keysym */
  void (*f) (Client *c);                /* The function that's mapped to key */
};

/*
 * Globals
 */

extern Display *display;                /* Our display */
extern Atom xa_wm_state;                /* Atoms for window management */
extern Atom xa_wm_change_state;
extern Atom xa_wm_protos;
extern Atom xa_wm_delete;
extern int nomousefocus;                /* No mouse focus flag */
extern XColor fg, bg, fc;               /* colours for fore, back, and locked */
extern int passthrough;                 /* Are we in passthrough mode */
extern Client *current;                 /* The "current" client */
extern Client *head_client;             /* The start of client linked list */
extern struct wmkeys keytab[];          /* Keymap table */
extern char *term;                      /* Terminal to launch */
extern int have_shape;                  /* Shaped windows? */
extern int ignore_xerror;               /* Ignore X errors */

/*
 * Function decls
 */

/* mapfunctions.c */

void my_grab_key(int keysym, unsigned int modifier);
void move_win_up(Client *c);
void move_win_down(Client *c);
void move_win_left(Client *c);
void move_win_right(Client *c);
void expand_win_y(Client *c);
void contract_win_y(Client *c);
void expand_win_x(Client *c);
void contract_win_x(Client *c);
void kill_client(Client *c);
void raise_client(Client *c);
void lower_client(Client *c);
void horizontal_toggle(Client *c);
void vertical_toggle(Client *c);
void maximize_toggle(Client *c);
void lock_window(Client *c);
void new_term(Client *c);
void next_client(Client *c);
void quit_wm(Client *c);
void start_passthrough(Client *c);
void goto_vdesk_0(Client *c);
void goto_vdesk_1(Client *c);
void goto_vdesk_2(Client *c);
void goto_vdesk_3(Client *c);
void goto_vdesk_4(Client *c);
void goto_vdesk_5(Client *c);
void goto_vdesk_6(Client *c);
void goto_vdesk_7(Client *c);
void prev_vdesk(Client *c);
void next_vdesk(Client *c);
void top_left(Client *c);
void top_right(Client *c);
void bottom_left(Client *c);
void bottom_right(Client *c);
void toggle_focus(Client *c);
void dummy_func(Client *c);

/* client.c */

Client *find_client(Window w);
Client *next_client_on_vdesk(Client *c);
void change_gravity(Client *c, int invert);
void remove_client(Client *c, int from_cleanup);
void send_config(Client *c);
void send_wm_delete(Client *c);
void set_wm_state(Client *c, int state);
void send_key_event(KeySym key);
int  send_xmessage(Window w, Atom a, long x);
void set_shape(Client *c);

/* events.c */

void handle_key_event (XKeyEvent *e);
void handle_button_event (XButtonEvent *e);
void handle_client_message (XClientMessageEvent *e);
void handle_configure_request (XConfigureRequestEvent *e);
void handle_enter_event (XCrossingEvent *e);
void handle_map_request (XMapRequestEvent *e);
void handle_property_change (XPropertyEvent *e);
void handle_unmap_event (XUnmapEvent *e);

/* main.c */

int  main(int argc, char *argv[]);
void scan_windows(void);
void setup_display(char *dpy);
void load_defaults(void);

/* misc.c */

void handle_signal(int signo);
int  handle_xerror(Display *dpy, XErrorEvent *e);
int  handle_xexit(Display *dpy);
void spawn(char *cmd);
int  vdesk(int vdesk_command, int vdesk_option);

/* new.c */

void init_position(Client *c);
void make_new_client(Window w);
void reparent(Client *c);

/* screen.c */

void drag(Client *c);
void draw_outline(Client *c);
void get_mouse_position(int *x, int *y);
void move(Client *c, int set);
void recalculate_sweep(Client *c, int x1, int y1, int x2, int y2);
void resize(Client *c, int set);
void maximise_vert(Client *c);
void maximise_horiz(Client *c);
void sweep(Client *c);
void unhide(Client *c, int raiseit);
void next(Client *c);
void hide(Client *c);
void focus_client(Client *c, int raiseit);
void unfocus_client(Client *c);
void switch_vdesk(int v);
