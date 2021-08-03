SUBDIRS = plugins/ssh plugins/rdesktop

sbin_PROGRAMS = ldm
ldmlibexecdir = $(libexecdir)/ldm
ldmplugdir = $(libdir)/ldm/
ldm_SOURCES = ldm.c ldmplugin.c ldminfo.c ldmgreetercomm.c ldmutils.c plugin.c logging.c

ldm_LDADD = $(GLIB_LIBS) $(GOBJECT_LIBS)
ldm_LDFLAGS = -pthread -ldl -export-dynamic @GLIB_LIBS@
AM_CPPFLAGS = $(GLIB_CFLAGS) \
              $(GOBJECT_CFLAGS) \
              -DRC_DIR=\""$(datadir)/ldm"\" \
              -DLOCALE_DIR=\""$(datadir)/locale"\" \
              -DLDM_EXEC_DIR=\""$(ldmlibexecdir)"\" \
              -DLDM_PLUG_DIR=\""$(ldmplugdir)"\"

