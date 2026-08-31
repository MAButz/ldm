#define PORT 3389

typedef struct rdp_info {
    gchar *username;
    gchar *password;
    gchar *domain;
    gchar *server;
    gchar *lang;
    gchar *rdpoptions;
    gchar *binary;              /* full path to the xfreerdp(3) executable to run */
    gint rdpfd;
    gint rdpslavefd;
    GPid rdppid;
} RdpInfo;

void __attribute__((constructor)) initialize();

/* Member functions */
void _get_domain();

void auth_xfreerdp(void);
void close_xfreerdp(void);
void init_xfreerdp(void);
void xfreerdp_session(void);
void start_xfreerdp(void);

extern volatile sig_atomic_t child_exited;
