#ifndef LDMGREETERCOMM_H
#define LDMGREETERCOMM_H

#define _(text) gettext(text)

/* ldmgreetercomm API */
int ask_greeter(gchar*);
int listen_greeter(gchar**, gsize*, gsize*);
int set_message(gchar*);
void close_greeter();
gchar *ask_value_greeter(gchar*);

void set_greeter_pid(GPid);
void set_greeter_read_channel(GIOChannel*);
void set_greeter_write_channel(GIOChannel*);

#endif
