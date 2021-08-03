ldmplugdir = $(libdir)/ldm/
ldmplug_LTLIBRARIES = libssh.la

libssh_la_CFLAGS = $(GLIB_CFLAGS) $(GOBJECT_CFLAGS)

libssh_la_LDFLAGS = -lcrypt $(GLIB_LIBS) $(GOBJECT_LIBS)
libssh_la_SOURCES = ssh.c
