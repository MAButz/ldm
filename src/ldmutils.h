#ifndef LDMUTILS_H
#define LDMUTILS_H

void close_wm(void);
void get_ipaddr(void);
void handle_sigchld(int);
void ldm_wait(pid_t pid);
int ldm_getenv_bool(const char*);
pid_t ldm_spawn (gchar *command, gint *rfd, gint *wfd, void (*setup)());

void rc_files(gchar *);
#endif
