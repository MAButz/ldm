#ifndef LDM_H
#define LDM_H

#define _(text) gettext(text)
#define DEFAULT_AUTH_PLUGIN "ssh"

struct ldm_info {
    gchar    *server;
    gchar    *display;
    gchar    *fontpath;
    gchar    *override_port;
    gchar    *authfile;
    gchar    *username;
    gchar    *password;
    gchar    *lang;
    gchar    *session;
    gchar    *xsession;
    gchar    *sshoptions;
    gchar    *sound_daemon;
    gchar    *greeter_prog;
    gchar    *wm_prog;
    gchar    *control_socket;
    gchar    *ipaddr;
    gboolean allowguest;
    gboolean autologin;
    gboolean sound;
    gboolean localdev;
    gboolean directx;
    gboolean nomad;
    gint     sshfd;
    gint     sshslavefd;
    GIOChannel *greeterr;
    GIOChannel *greeterw;
    GPid     pid;
    GPid     sshpid;
    GPid     nomadpid;
    GPid     xsessionpid;
    GPid     greeterpid;
    GPid     wmpid;
};

/* ldm.c specific */
void get_ldm_env(void);
void load_guestinfo(void);

/* extern variables  */
extern volatile sig_atomic_t unexpected_child;
extern volatile sig_atomic_t child_exited;
/* FIXME No more ldm_info extern */
extern struct ldm_info ldm;

#endif
