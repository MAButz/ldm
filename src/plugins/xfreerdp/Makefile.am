ldmplugdir = $(libdir)/ldm/
ldmplug_LTLIBRARIES = libxfreerdp.la

libxfreerdp_la_CFLAGS = $(GLIB_CFLAGS) $(GOBJECT_CFLAGS)

# libkrb5 is used to verify credentials with the KDC before starting the RDP
# client (see rdp_preauth_kerberos()). Only a build-time dependency for the
# plugin: libkrb5-3 and libgssapi-krb5-2 are already present in a Debian
# client image, so this adds no packages to the image itself.
libxfreerdp_la_LDFLAGS = $(GLIB_LIBS) $(GOBJECT_LIBS) -lkrb5
libxfreerdp_la_SOURCES = xfreerdp.c
