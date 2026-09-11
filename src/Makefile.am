SUBDIRS = plugins/ssh plugins/xfreerdp

sbin_PROGRAMS = ldm
ldmlibexecdir = $(libexecdir)/ldm
ldmplugdir = $(libdir)/ldm/
ldm_SOURCES = ldm.c ldmplugin.c ldminfo.c ldmgreetercomm.c ldmutils.c ldmpty.c plugin.c logging.c

ldm_LDADD = $(GLIB_LIBS) $(GOBJECT_LIBS)
ldm_LDFLAGS = -pthread -ldl -export-dynamic -pie @GLIB_LIBS@
ldm_CFLAGS = -fPIE
AM_CPPFLAGS = $(GLIB_CFLAGS) \
              $(GOBJECT_CFLAGS) \
              -DRC_DIR=\""$(datadir)/ldm"\" \
              -DLOCALE_DIR=\""$(datadir)/locale"\" \
              -DLDM_EXEC_DIR=\""$(ldmlibexecdir)"\" \
              -DLDM_PLUG_DIR=\""$(ldmplugdir)"\"

