void __attribute__((constructor)) initialize();

typedef struct ssh_info {
    gchar *ctl_socket;
    gchar *isguest;
    gchar *lang;
    gchar *password;
    gchar *override_port;
    gchar *server;
    gchar *session;
    gchar *xsession;
    gchar *sshoptions;
    gchar *username;
    gint sshfd;
    gint sshslavefd;
    GPid sshpid;
} SSHInfo;

/* Member functions */
void _set_env();

void *eater();
void close_ssh(void);
void get_auth(void);
void start_ssh(void);
void get_guest(void);
void init_ssh(void);
void ssh_chat(gint);
void ssh_endsession(void);
void ssh_session(void);
void ssh_tty_init();
void ssh_hashpass(void);

int expect(int, char*,int,...);

extern volatile sig_atomic_t child_exited;
